# Settings for our own targets only (never applied to dependencies).

if(LVR_CLANG_TIDY)
    find_program(LVR_CLANG_TIDY_EXE NAMES clang-tidy REQUIRED)
    set(LVR_CLANG_TIDY_COMMAND ${LVR_CLANG_TIDY_EXE})
    if(LVR_WARNINGS_AS_ERRORS)
        list(APPEND LVR_CLANG_TIDY_COMMAND "--warnings-as-errors=*")
    endif()
endif()

# Compiler warnings, plus coverage instrumentation when LVR_COVERAGE is on (our targets only: SDL, Catch2 and glaze
# are not instrumented). Used directly by tests (which skip clang-tidy: test macros are noisy).
function(lvr_target_warnings target)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(LVR_WARNINGS_AS_ERRORS)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(LVR_COVERAGE)
        target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
        target_link_options(${target} PRIVATE -fprofile-instr-generate)
    endif()
endfunction()

# Warnings + clang-tidy. Use for everything under src/.
function(lvr_target_defaults target)
    lvr_target_warnings(${target})
    if(LVR_CLANG_TIDY)
        set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "${LVR_CLANG_TIDY_COMMAND}")
    endif()
endfunction()
