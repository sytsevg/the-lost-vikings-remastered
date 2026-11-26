# Global compile/link settings. Applied before dependencies are added, so SDL3, Catch2 etc. are
# built with the same standard library, sanitizers and hardening as our own code.

# libc++ everywhere (needed for libc++ hardening; keeps the ABI consistent across all C++ code).
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
add_link_options($<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>)

if(LVR_SANITIZERS)
    list(JOIN LVR_SANITIZERS "," _lvr_sanitizers)
    add_compile_options(-fsanitize=${_lvr_sanitizers} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=${_lvr_sanitizers})
    message(STATUS "vikings-remaster: sanitizers = ${_lvr_sanitizers}")
endif()

# Single self-contained executable: libc++, libc++abi and libgcc linked in; only glibc and the
# libraries SDL dlopen()s at runtime (Wayland, EGL/Vulkan, PipeWire...) stay dynamic. On Windows (llvm-mingw) the
# same flags leave only Windows' own DLLs and its C runtime (UCRT).
if(LVR_STATIC_RUNTIME)
    if(LVR_SANITIZERS)
        message(FATAL_ERROR "LVR_STATIC_RUNTIME cannot be combined with sanitizers")
    endif()
    add_link_options($<$<LINK_LANGUAGE:CXX>:-static-libstdc++> -static-libgcc)
endif()

string(TOUPPER "${LVR_LIBCXX_HARDENING}" _lvr_hardening)
if(NOT _lvr_hardening STREQUAL "NONE")
    add_compile_definitions($<$<COMPILE_LANGUAGE:CXX>:_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_${_lvr_hardening}>)
    message(STATUS "vikings-remaster: libc++ hardening = ${LVR_LIBCXX_HARDENING}")
endif()
