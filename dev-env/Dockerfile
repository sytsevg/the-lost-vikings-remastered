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

# The GPU renderer (ADR-0006): a software Vulkan driver (lavapipe) so its pictures can be made and compared here
# without a graphics card or a window, and the compiler for its shaders.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libvulkan1 mesa-vulkan-drivers vulkan-tools glslc \
    && rm -rf /var/lib/apt/lists/*

# DOSBox-X build dependencies (development tool only, ADR-0016; built by tools/dosbox/ into build/dosbox/).
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

# Keep this the last layer: a new CLAUDE_CODE_VERSION (passed by run-dev-env.sh) invalidates
# the cache only from here, so everything above is reused.
ARG CLAUDE_CODE_VERSION=latest
RUN npm install -g @anthropic-ai/claude-code@${CLAUDE_CODE_VERSION}

WORKDIR /workspace
ENTRYPOINT ["claude"]
