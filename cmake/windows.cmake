# The Windows build (x86-64), cross-built with llvm-mingw: the same LLVM, with libc++ and mingw-w64's headers and
# libraries for Windows (the dev container has it in /opt/llvm-mingw, dev-env/Dockerfile). The `windows` preset uses
# this file; scripts/windows.sh builds with it and runs the checks. What runs during the build (glslc) is the
# container's own; the tests and the replays run under Wine (wine.sh, beside this file).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(lvr_llvm_mingw /opt/llvm-mingw)
set(CMAKE_C_COMPILER ${lvr_llvm_mingw}/bin/x86_64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER ${lvr_llvm_mingw}/bin/x86_64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER ${lvr_llvm_mingw}/bin/x86_64-w64-mingw32-windres)

set(CMAKE_FIND_ROOT_PATH ${lvr_llvm_mingw}/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_CROSSCOMPILING_EMULATOR ${CMAKE_CURRENT_LIST_DIR}/wine.sh)
