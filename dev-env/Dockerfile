# Dev container for vikings-remaster: Claude Code + Clang toolchain.
# See docs/development.md. On the host:
#   UPDATE=1 ./run-dev-env.sh    rebuild only the claude-code layer (if a newer version exists)
#   REBUILD=1 ./run-dev-env.sh   full rebuild: newer base image, no cache
FROM node:22-trixie-slim

ARG LLVM_VERSION=23

# Base tools, build tools, and SDL3 build dependencies (Wayland only, no X11; EGL/GLES; audio; input).
# The -dev packages are what make SDL3 compile those backends in; nothing in the container opens a window or audio device.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl gnupg git ripgrep bash jq python3 \
        cmake ninja-build pkg-config ccache make libc6-dev binutils \
        libwayland-dev libxkbcommon-dev wayland-protocols libdecor-0-dev \
        libegl-dev libgles-dev libdrm-dev libgbm-dev \
        libasound2-dev libpulse-dev libpipewire-0.3-dev \
        libudev-dev libdbus-1-dev \
    && rm -rf /var/lib/apt/lists/*

# The GPU renderer: a software Vulkan driver (lavapipe) so its pictures can be made and compared here
# without a graphics card or a window, and the compiler for its shaders.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libvulkan1 mesa-vulkan-drivers vulkan-tools glslc \
    && rm -rf /var/lib/apt/lists/*

# DOSBox-X build dependencies (development tool only; built by tools/dosbox/ into build/dosbox/).
# It brings its own SDL2; curses is for the debugger build, gcc because DOSBox-X is built and tested with it.
RUN apt-get update && apt-get install -y --no-install-recommends \
        autoconf automake libtool gcc g++ libncurses-dev libpng-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# LLVM toolchain from apt.llvm.org (clang, clang-tidy, clang-format, clangd, lld, libc++, sanitizer/fuzzer runtimes).
RUN curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] http://apt.llvm.org/trixie/ llvm-toolchain-trixie-${LLVM_VERSION} main" \
        > /etc/apt/sources.list.d/llvm.list \
    && apt-get update && apt-get install -y --no-install-recommends \
        clang-${LLVM_VERSION} clang-tidy-${LLVM_VERSION} clang-format-${LLVM_VERSION} clangd-${LLVM_VERSION} \
        lld-${LLVM_VERSION} lldb-${LLVM_VERSION} llvm-${LLVM_VERSION} \
        libc++-${LLVM_VERSION}-dev libc++abi-${LLVM_VERSION}-dev libclang-rt-${LLVM_VERSION}-dev \
    && rm -rf /var/lib/apt/lists/* \
    && for tool in clang clang++ clang-tidy clang-format clangd lld ld.lld lldb llvm-symbolizer llvm-cov llvm-profdata run-clang-tidy; do \
           ln -sf "/usr/bin/${tool}-${LLVM_VERSION}" "/usr/local/bin/${tool}"; \
       done

ENV CC=clang CXX=clang++ ASAN_SYMBOLIZER_PATH=/usr/local/bin/llvm-symbolizer

# The Windows cross-build: llvm-mingw (the same LLVM, with libc++ and the mingw-w64 headers and libraries for
# Windows, UCRT), pinned by its release and checksum, in /opt/llvm-mingw. It is not put on PATH: its bin/ has a
# clang of its own; the toolchain file names its x86_64-w64-mingw32-clang++. Nothing of the Windows build runs
# here, so there is no Wine.
ARG LLVM_MINGW_RELEASE=20261117
ARG LLVM_MINGW_SHA256=e7a15a49a2ea95e9f3d8a9d0cfabe5052a4b10fb4777cc54de5a8c27b64bb701
RUN apt-get update && apt-get install -y --no-install-recommends xz-utils \
    && rm -rf /var/lib/apt/lists/* \
    && curl -fsSL -o /tmp/llvm-mingw.tar.xz \
        "https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_RELEASE}/llvm-mingw-${LLVM_MINGW_RELEASE}-ucrt-ubuntu-22.04-x86_64.tar.xz" \
    && echo "${LLVM_MINGW_SHA256}  /tmp/llvm-mingw.tar.xz" | sha256sum -c - \
    && mkdir /opt/llvm-mingw \
    && tar -xJf /tmp/llvm-mingw.tar.xz -C /opt/llvm-mingw --strip-components=1 \
    && rm /tmp/llvm-mingw.tar.xz

# Keep this the last layer: a new CLAUDE_CODE_VERSION (passed by run-dev-env.sh) invalidates
# the cache only from here, so everything above is reused.
ARG CLAUDE_CODE_VERSION=latest
RUN npm install -g @anthropic-ai/claude-code@${CLAUDE_CODE_VERSION}

WORKDIR /workspace
ENTRYPOINT ["claude"]
