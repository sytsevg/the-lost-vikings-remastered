#!/usr/bin/env python3
"""A small 65816 disassembler for reading the SNES game's code in the user's ROM image (docs/formats/snes.md).

  tools/dis65816.py OFFSET COUNT [M X]   COUNT instructions from file offset OFFSET (hex); M, X = 1 when the
                                           accumulator / the index registers are 8 bits wide there (default 0 0)

Each line: the SNES address (bank 80, the image is LoROM), the file offset, the bytes, the instruction. The widths
follow REP / SEP in a straight line from the start, which is good enough to read one routine.
"""
import pathlib
import sys
ROM=sorted((pathlib.Path(__file__).resolve().parent.parent / "gamedata" / "original").glob("*.sfc"))[0].read_bytes()
M="imp imm8 immM immX dp dpx dpy dpi dpix dpiy dpil dpily abs absx absy absl abslx ind indx indl rel rell sr sriy mv".split()
T={}
def add(mode,**ops):
    for n,c in ops.items(): T[c]=(n.rstrip('_'),mode)
# group by mode
add('imp',BRK=0x00,PHP=0x08,ASL=0x0A,PHD=0x0B,CLC=0x18,INC=0x1A,TCS=0x1B,PLP=0x28,ROL=0x2A,PLD=0x2B,SEC=0x38,DEC=0x3A,TSC=0x3B,RTI=0x40,PHA=0x48,LSR=0x4A,PHK=0x4B,CLI=0x58,PHY=0x5A,TCD=0x5B,RTS=0x60,PLA=0x68,ROR=0x6A,RTL=0x6B,SEI=0x78,PLY=0x7A,TDC=0x7B,DEY=0x88,TXA=0x8A,PHB=0x8B,TYA=0x98,TXS=0x9A,TXY=0x9B,TAY=0xA8,TAX=0xAA,PLB=0xAB,CLV=0xB8,TSX=0xBA,TYX=0xBB,INY=0xC8,DEX=0xCA,WAI=0xCB,CLD=0xD8,PHX=0xDA,STP=0xDB,INX=0xE8,NOP=0xEA,XBA=0xEB,SED=0xF8,PLX=0xFA,XCE=0xFB)
for base,n in ((0x00,'ORA'),(0x20,'AND'),(0x40,'EOR'),(0x60,'ADC'),(0x80,'STA'),(0xA0,'LDA'),(0xC0,'CMP'),(0xE0,'SBC')):
    for off,mode in ((0x01,'dpix'),(0x03,'sr'),(0x05,'dp'),(0x07,'dpil'),(0x09,'immM'),(0x0D,'abs'),(0x0F,'absl'),(0x11,'dpiy'),(0x12,'dpi'),(0x13,'sriy'),(0x15,'dpx'),(0x17,'dpily'),(0x19,'absy'),(0x1D,'absx'),(0x1F,'abslx')):
        if not (n=='STA' and off==0x09): T[base+off]=(n,mode)
for n,cs in (('ASL',(0x06,0x0E,0x16,0x1E)),('ROL',(0x26,0x2E,0x36,0x3E)),('LSR',(0x46,0x4E,0x56,0x5E)),('ROR',(0x66,0x6E,0x76,0x7E)),('DEC',(0xC6,0xCE,0xD6,0xDE)),('INC',(0xE6,0xEE,0xF6,0xFE))):
    for c,mode in zip(cs,('dp','abs','dpx','absx')): T[c]=(n,mode)
for c,n,mode in ((0x04,'TSB','dp'),(0x0C,'TSB','abs'),(0x14,'TRB','dp'),(0x1C,'TRB','abs'),(0x24,'BIT','dp'),(0x2C,'BIT','abs'),(0x34,'BIT','dpx'),(0x3C,'BIT','absx'),(0x89,'BIT','immM'),
 (0x64,'STZ','dp'),(0x74,'STZ','dpx'),(0x9C,'STZ','abs'),(0x9E,'STZ','absx'),(0x84,'STY','dp'),(0x8C,'STY','abs'),(0x94,'STY','dpx'),(0x86,'STX','dp'),(0x8E,'STX','abs'),(0x96,'STX','dpy'),
 (0xA0,'LDY','immX'),(0xA4,'LDY','dp'),(0xAC,'LDY','abs'),(0xB4,'LDY','dpx'),(0xBC,'LDY','absx'),(0xA2,'LDX','immX'),(0xA6,'LDX','dp'),(0xAE,'LDX','abs'),(0xB6,'LDX','dpy'),(0xBE,'LDX','absy'),
 (0xC0,'CPY','immX'),(0xC4,'CPY','dp'),(0xCC,'CPY','abs'),(0xE0,'CPX','immX'),(0xE4,'CPX','dp'),(0xEC,'CPX','abs'),
 (0x10,'BPL','rel'),(0x30,'BMI','rel'),(0x50,'BVC','rel'),(0x70,'BVS','rel'),(0x90,'BCC','rel'),(0xB0,'BCS','rel'),(0xD0,'BNE','rel'),(0xF0,'BEQ','rel'),(0x80,'BRA','rel'),(0x82,'BRL','rell'),
 (0x20,'JSR','abs'),(0x22,'JSL','absl'),(0x4C,'JMP','abs'),(0x5C,'JML','absl'),(0x6C,'JMP','ind'),(0x7C,'JMP','indx'),(0xDC,'JML','indl'),(0xFC,'JSR','indx'),
 (0xC2,'REP','imm8'),(0xE2,'SEP','imm8'),(0x02,'COP','imm8'),(0x42,'WDM','imm8'),(0x44,'MVP','mv'),(0x54,'MVN','mv'),(0x62,'PER','rell'),(0xD4,'PEI','dp'),(0xF4,'PEA','abs')):
    T[c]=(n,mode)
def snes(off): return f"{0x80|off>>15:02X}:{0x8000|off&0x7FFF:04X}"
def dis(off,count,m=0,x=0):
    out=[]
    for _ in range(count):
        op=ROM[off]; n,mode=T.get(op,('???','imp'))
        size={'imp':1,'imm8':2,'immM':2 if m else 3,'immX':2 if x else 3,'dp':2,'dpx':2,'dpy':2,'dpi':2,'dpix':2,'dpiy':2,'dpil':2,'dpily':2,'sr':2,'sriy':2,'rel':2,'abs':3,'absx':3,'absy':3,'ind':3,'indx':3,'indl':3,'rell':3,'mv':3,'absl':4,'abslx':4}[mode]
        b=ROM[off+1:off+size]; v=int.from_bytes(b,'little') if b else 0
        fmt={'imp':'','imm8':f'#${v:02X}','immM':f'#${v:0{2*len(b)}X}','immX':f'#${v:0{2*len(b)}X}','dp':f'${v:02X}','dpx':f'${v:02X},X','dpy':f'${v:02X},Y','dpi':f'(${v:02X})','dpix':f'(${v:02X},X)','dpiy':f'(${v:02X}),Y','dpil':f'[${v:02X}]','dpily':f'[${v:02X}],Y','sr':f'${v:02X},S','sriy':f'(${v:02X},S),Y','abs':f'${v:04X}','absx':f'${v:04X},X','absy':f'${v:04X},Y','ind':f'(${v:04X})','indx':f'(${v:04X},X)','indl':f'[${v:04X}]','absl':f'${v:06X}','abslx':f'${v:06X},X','mv':f'${v:04X}'}.get(mode,'')
        if mode=='rel': t=off+2+(v-256 if v>127 else v); fmt=f'${0x8000|t&0x7FFF:04X}'
        if mode=='rell': t=off+3+(v-65536 if v>32767 else v); fmt=f'${0x8000|t&0x7FFF:04X}'
        out.append(f"{snes(off)} ({off:05X})  {ROM[off:off+size].hex():8}  {n} {fmt}")
        if n=='REP': m=m and not v&0x20; x=x and not v&0x10
        if n=='SEP': m=m or bool(v&0x20); x=x or bool(v&0x10)
        off+=size
    return out
if __name__=='__main__':
    off=int(sys.argv[1],16); n=int(sys.argv[2]); m=int(sys.argv[3]) if len(sys.argv)>3 else 0; x=int(sys.argv[4]) if len(sys.argv)>4 else 0
    print("\n".join(dis(off,n,m,x)))
