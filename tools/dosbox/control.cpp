/*
 *  The control channel for vikings-remaster (ADR-0016, docs/dosbox.md): a script drives DOSBox-X through a Unix
 *  socket, one command per line, one reply per line. Copied into DOSBox-X's src/misc/ by tools/dosbox/build.sh;
 *  tools/dosbox/hooks.patch calls into it from the CPU core, the VGA, DOS exec and the build.
 *
 *  Copyright (C) 2026 the vikings-remaster authors. Part of a modified DOSBox-X, so under the same licence:
 *
 *  This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 *  Public License as published by the Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version. This program is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *  General Public License for more details.
 *
 *  Without LVR_DOSBOX_CONTROL in the environment nothing here does anything. With it (a socket path), DOSBox-X
 *  listens there at its first video frame or the first program DOS starts (MOUNT, with -c "mount ..."), whichever
 *  comes first, waits for one client and stops. While stopped nothing is emulated, and
 *  the client's commands are served:
 *
 *    run                       emulate until a stop: a breakpoint, the frame count, the program given to
 *    run frames N              'break exec'; the reply is "stopped REASON frame F cs=CS ip=IP"
 *    break add SEG:OFF         a breakpoint (real mode: at linear SEG*16+OFF) / remove it / remove all
 *    break del SEG:OFF
 *    break clear
 *    break exec NAME           stop at the first instruction of the next program NAME (e.g. VIKINGS.EXE) that
 *                              DOS starts; the stop reason is "exec LOADSEG", LOADSEG = PSP + 0x10
 *    regs                      "ok eax=... ebx=... ... cs=... ds=... es=... ss=... fs=... gs=... eip=... flags=..."
 *    set REG HEX               set a register (eax..edi, ax..di, eip/ip, flags, cs, ds, es, ss, fs, gs)
 *    read SEG:OFF LEN          "ok HEX": LEN bytes of memory (LEN hex, at most 0x10000)
 *    write SEG:OFF HEX         write bytes to memory
 *    key HEX                   raw scan-code bytes into the keyboard controller (set 1: make, make|80 = break,
 *                              E0 before extended keys), as the keyboard would send them
 *    screenshot                capture the next frame drawn as an indexed PNG into the captures folder
 *    opl start / opl stop      raw OPL capture (.dro) into the captures folder; it starts with the first note
 *    wave start / wave stop    the sound as DOSBox-X mixes it (.wav) into the captures folder, on the emulated clock
 *    quit                      exit DOSBox-X
 *
 *  Numbers are hex without a prefix. Errors reply "err MESSAGE". The breakpoints are checked by the normal core
 *  only (core=normal).
 */

#include "dosbox.h"
#include "callback.h"
#include "cpu.h"
#include "logging.h"
#include "mem.h"
#include "regs.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void KEYBOARD_AddBuffer(uint16_t data);
void CAPTURE_ScreenShotEvent(bool pressed);
void OPL_SaveRawEvent(bool pressed);
void CAPTURE_WaveEvent(bool pressed);

bool lvr_control_breaks = false; // read by the core before each instruction: any breakpoint set?

namespace {

enum class State { kUnset, kOff, kOn };

State state = State::kUnset;
int client = -1;
Bitu callback = 0;
std::string pending_reason; // why the core handed over to the callback

std::set<PhysPt> breakpoints;
PhysPt exec_break = 0; // the one-shot breakpoint at a program's entry, 0 = none
bool exec_break_set = false;
std::string exec_name;      // 'break exec NAME', upper case; empty = not armed
uint16_t exec_loadseg = 0;
PhysPt skip_once = 0;       // after a stop at a breakpoint: execute that instruction once without stopping
bool skip_armed = false;

uint64_t frame = 0;          // video frames (vertical retraces) since start-up
uint64_t frames_left = 0;    // 'run frames N'; 0 = no frame stop
bool opl_capturing = false;
bool wave_capturing = false;

std::string upper(std::string text)
{
	for (char& c : text)
		c = (char)std::toupper((unsigned char)c);
	return text;
}

std::string hex(uint32_t value, int digits)
{
	char buffer[16];
	snprintf(buffer, sizeof buffer, "%0*x", digits, (unsigned)value);
	return buffer;
}

bool parse_hex(const std::string& text, uint32_t& value)
{
	if (text.empty() || text.size() > 8)
		return false;
	char* end = nullptr;
	const unsigned long parsed = strtoul(text.c_str(), &end, 16);
	if (*end != '\0')
		return false;
	value = (uint32_t)parsed;
	return true;
}

// SEG:OFF as a real-mode linear address, or a plain linear address.
bool parse_address(const std::string& text, PhysPt& address)
{
	const size_t colon = text.find(':');
	uint32_t seg = 0, off = 0;
	if (colon == std::string::npos) {
		if (!parse_hex(text, off))
			return false;
		address = off;
		return true;
	}
	if (!parse_hex(text.substr(0, colon), seg) || !parse_hex(text.substr(colon + 1), off) || seg > 0xFFFF)
		return false;
	address = (PhysPt)((seg << 4) + off);
	return true;
}

bool parse_bytes(const std::string& text, std::vector<uint8_t>& bytes)
{
	if (text.empty() || text.size() % 2 != 0)
		return false;
	for (size_t i = 0; i < text.size(); i += 2) {
		uint32_t value = 0;
		if (!parse_hex(text.substr(i, 2), value))
			return false;
		bytes.push_back((uint8_t)value);
	}
	return true;
}

// Ends DOSBox-X at once. Not E_Exit: that asks "Are you sure to quit?", and without a terminal the question
// repeats forever (it filled a 30 GB log once).
[[noreturn]] void fail(const std::string& message)
{
	LOG_MSG("control: %s", message.c_str());
	fprintf(stderr, "control: %s\n", message.c_str());
	_exit(1);
}

void send_line(const std::string& line)
{
	const std::string out = line + "\n";
	size_t sent = 0;
	while (sent < out.size()) {
		const ssize_t n = send(client, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			fail("the client went away");
		sent += (size_t)n;
	}
}

bool read_line(std::string& line)
{
	line.clear();
	for (;;) {
		char c = 0;
		const ssize_t n = recv(client, &c, 1, 0);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			return false;
		if (c == '\n')
			return true;
		if (c != '\r')
			line += c;
	}
}

std::string registers()
{
	std::ostringstream out;
	out << "ok eax=" << hex(reg_eax, 8) << " ebx=" << hex(reg_ebx, 8) << " ecx=" << hex(reg_ecx, 8)
	    << " edx=" << hex(reg_edx, 8) << " esi=" << hex(reg_esi, 8) << " edi=" << hex(reg_edi, 8)
	    << " ebp=" << hex(reg_ebp, 8) << " esp=" << hex(reg_esp, 8) << " cs=" << hex(SegValue(cs), 4)
	    << " ds=" << hex(SegValue(ds), 4) << " es=" << hex(SegValue(es), 4) << " ss=" << hex(SegValue(ss), 4)
	    << " fs=" << hex(SegValue(fs), 4) << " gs=" << hex(SegValue(gs), 4) << " eip=" << hex(reg_eip, 8)
	    << " flags=" << hex((uint32_t)reg_flags, 8);
	return out.str();
}

bool set_register(const std::string& name, uint32_t value)
{
	struct Reg32 { const char* name32; const char* name16; uint32_t* reg; };
	const Reg32 regs32[] = {
	        {"EAX", "AX", &reg_eax}, {"EBX", "BX", &reg_ebx}, {"ECX", "CX", &reg_ecx}, {"EDX", "DX", &reg_edx},
	        {"ESI", "SI", &reg_esi}, {"EDI", "DI", &reg_edi}, {"EBP", "BP", &reg_ebp}, {"ESP", "SP", &reg_esp},
	        {"EIP", "IP", &reg_eip},
	};
	for (const Reg32& r : regs32) {
		if (name == r.name32) {
			*r.reg = value;
			return true;
		}
		if (name == r.name16) {
			*r.reg = (*r.reg & 0xFFFF0000u) | (value & 0xFFFFu);
			return true;
		}
	}
	if (name == "FLAGS") {
		reg_flags = value;
		return true;
	}
	const struct { const char* name; SegNames seg; } segs[] = {
	        {"CS", cs}, {"DS", ds}, {"ES", es}, {"SS", ss}, {"FS", fs}, {"GS", gs},
	};
	for (const auto& s : segs) {
		if (name == s.name && value <= 0xFFFF) {
			SegSet16(s.seg, (uint16_t)value); // real mode
			return true;
		}
	}
	return false;
}

void update_breaks()
{
	lvr_control_breaks = !breakpoints.empty() || exec_break_set;
}

// Serves commands until one of them resumes the emulation. at_break: stopped before the instruction at CS:IP by a
// breakpoint, which then has to run once without stopping again.
void stop(const std::string& reason, bool at_break)
{
	send_line("stopped " + reason + " frame " + std::to_string(frame) + " cs=" + hex(SegValue(cs), 4) +
	          " ip=" + hex(reg_eip, 4));
	std::string line;
	while (read_line(line)) {
		std::istringstream in(line);
		std::vector<std::string> words;
		for (std::string word; in >> word;)
			words.push_back(word);
		if (words.empty())
			continue;
		const std::string command = words[0];
		const auto arg = [&](size_t i) { return i < words.size() ? words[i] : std::string(); };

		if (command == "run") {
			frames_left = 0;
			if (arg(1) == "frames") {
				uint32_t n = 0;
				if (!parse_hex(arg(2), n) || n == 0) {
					send_line("err run frames N");
					continue;
				}
				frames_left = n;
			}
			skip_once = SegPhys(cs) + reg_eip;
			skip_armed = at_break;
			return; // the reply comes at the next stop
		}
		if (command == "break") {
			PhysPt address = 0;
			if (arg(1) == "add" && parse_address(arg(2), address)) {
				breakpoints.insert(address);
			} else if (arg(1) == "del" && parse_address(arg(2), address)) {
				breakpoints.erase(address);
			} else if (arg(1) == "clear") {
				breakpoints.clear();
			} else if (arg(1) == "exec" && !arg(2).empty()) {
				exec_name = upper(arg(2));
			} else {
				send_line("err break add|del SEG:OFF, break clear, break exec NAME");
				continue;
			}
			update_breaks();
			send_line("ok");
		} else if (command == "regs") {
			send_line(registers());
		} else if (command == "set") {
			uint32_t value = 0;
			if (!parse_hex(arg(2), value) || !set_register(upper(arg(1)), value))
				send_line("err set REG HEX");
			else
				send_line("ok");
		} else if (command == "read") {
			PhysPt address = 0;
			uint32_t length = 0;
			if (!parse_address(arg(1), address) || !parse_hex(arg(2), length) || length > 0x10000) {
				send_line("err read SEG:OFF LEN");
				continue;
			}
			std::string out = "ok ";
			out.reserve(3 + 2 * length);
			for (uint32_t i = 0; i < length; ++i)
				out += hex(mem_readb(address + i), 2);
			send_line(out);
		} else if (command == "write") {
			PhysPt address = 0;
			std::vector<uint8_t> bytes;
			if (!parse_address(arg(1), address) || !parse_bytes(arg(2), bytes)) {
				send_line("err write SEG:OFF HEX");
				continue;
			}
			for (size_t i = 0; i < bytes.size(); ++i)
				mem_writeb(address + (PhysPt)i, bytes[i]);
			send_line("ok");
		} else if (command == "key") {
			std::vector<uint8_t> bytes;
			if (!parse_bytes(arg(1), bytes)) {
				send_line("err key HEX");
				continue;
			}
			for (uint8_t b : bytes)
				KEYBOARD_AddBuffer(b);
			send_line("ok");
		} else if (command == "screenshot") {
			CAPTURE_ScreenShotEvent(true);
			send_line("ok");
		} else if (command == "opl" && (arg(1) == "start" || arg(1) == "stop")) {
			const bool want = arg(1) == "start";
			if (want != opl_capturing) {
				OPL_SaveRawEvent(true);
				opl_capturing = want;
			}
			send_line("ok");
		} else if (command == "wave" && (arg(1) == "start" || arg(1) == "stop")) {
			const bool want = arg(1) == "start";
			if (want != wave_capturing) {
				CAPTURE_WaveEvent(true);
				wave_capturing = want;
			}
			send_line("ok");
		} else if (command == "quit") {
			if (opl_capturing)
				OPL_SaveRawEvent(true);
			if (wave_capturing)
				CAPTURE_WaveEvent(true);
			send_line("ok");
			close(client);
			_exit(0);
		} else {
			send_line("err unknown command");
		}
	}
	fail("the client went away");
}

Bitu stop_callback()
{
	stop(pending_reason, true);
	return 0;
}

bool start()
{
	const char* path = getenv("LVR_DOSBOX_CONTROL");
	if (path == nullptr || *path == '\0')
		return false;
	const int server = socket(AF_UNIX, SOCK_STREAM, 0);
	sockaddr_un address = {};
	address.sun_family = AF_UNIX;
	if (server < 0 || strlen(path) >= sizeof address.sun_path)
		fail(std::string("can't make a socket at ") + path);
	strcpy(address.sun_path, path);
	unlink(path);
	if (bind(server, (const sockaddr*)&address, sizeof address) != 0 || listen(server, 1) != 0)
		fail(std::string("can't listen at ") + path + ": " + strerror(errno));
	LOG_MSG("control: waiting for a client at %s", path);
	do {
		client = accept(server, nullptr, nullptr);
	} while (client < 0 && errno == EINTR);
	close(server);
	unlink(path);
	if (client < 0)
		fail(std::string("accept failed: ") + strerror(errno));

	callback = CALLBACK_Allocate();
	CallBack_Handlers[callback] = &stop_callback;
	CALLBACK_SetDescription(callback, "vikings-remaster control");
	return true;
}

// Opens the channel at the first frame or the first program started, whichever comes first, and stops there.
void start_once()
{
	if (state != State::kUnset)
		return;
	state = start() ? State::kOn : State::kOff;
	if (state == State::kOn)
		stop("start", false);
}

} // namespace

// The core, before each instruction while lvr_control_breaks: should it stop here? Then the core returns
// LVR_ControlCallback(), and Normal_Loop runs the callback, which serves commands.
bool LVR_ControlIsBreak(PhysPt address)
{
	if (skip_armed) {
		skip_armed = false;
		if (address == skip_once)
			return false;
	}
	if (exec_break_set && address == exec_break) {
		exec_break_set = false;
		update_breaks();
		pending_reason = "exec " + hex(exec_loadseg, 4);
		return true;
	}
	if (breakpoints.count(address) != 0) {
		pending_reason = "break " + hex(SegValue(cs), 4) + ":" + hex(reg_eip, 4);
		return true;
	}
	return false;
}

Bitu LVR_ControlCallback()
{
	return callback;
}

// VGA_VerticalTimer: one more frame.
void LVR_ControlFrame()
{
	++frame;
	if (state == State::kUnset) {
		start_once();
		return;
	}
	if (state == State::kOn && frames_left != 0 && --frames_left == 0)
		stop("frames", false);
}

// DOS_Execute, load-and-go: the program NAME is loaded and starts at CS:IP.
void LVR_ControlExec(const char* name, uint16_t loadseg, uint16_t seg, uint16_t off)
{
	start_once();
	if (state != State::kOn)
		return;
	LOG_MSG("control: exec %s at %04x:%04x, load segment %04x", name, seg, off, loadseg);
	if (exec_name.empty())
		return;
	std::string base = upper(name);
	const size_t slash = base.find_last_of("\\:/");
	if (slash != std::string::npos)
		base = base.substr(slash + 1);
	if (base != exec_name)
		return;
	exec_name.clear();
	exec_loadseg = loadseg;
	exec_break = (PhysPt)((seg << 4) + off);
	exec_break_set = true;
	update_breaks();
}
