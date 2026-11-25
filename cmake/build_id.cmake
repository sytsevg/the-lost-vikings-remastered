# The build's ID for co-op online (ADR-0012): two machines play together only with the same one. A hash of the
# game's sources (everything under src/ but the tests and the fuzzers), worked out at every build, so what was changed
# since the last commit counts, and a copy without git has one too. Written only when it changes (what includes it
# isn't built again for nothing).
#
#   cmake -DSOURCE_DIR=<the repository> -DOUTPUT=<the header> -P cmake/build_id.cmake

file(GLOB_RECURSE sources RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/src/*")
list(SORT sources)
set(all "")
foreach(source IN LISTS sources)
    if(source MATCHES "/tests/" OR source MATCHES "/fuzz/")
        continue()
    endif()
    file(SHA256 "${SOURCE_DIR}/${source}" one)
    string(APPEND all "${source} ${one}\n")
endforeach()
string(SHA256 id "${all}")
string(SUBSTRING "${id}" 0 12 id)

set(header "#pragma once\n\n// Written at every build by cmake/build_id.cmake: a hash of the game's sources.\n\n#include <string_view>\n\nnamespace lvr {\ninline constexpr std::string_view kBuild = \"${id}\";\n}  // namespace lvr\n")
set(before "")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" before)
endif()
if(NOT before STREQUAL header)
    file(WRITE "${OUTPUT}" "${header}")
endif()
