# Writes OUT (a header with DYNAMICAL_GIT_SHA) from `git describe`, only when it changed, so the build stamps
# the engine's commit into envparams.txt without rebuilding every time.
# cmake -DSOURCE_DIR=<repo> -DOUT=<header> -P git_version.cmake
execute_process(COMMAND git -C "${SOURCE_DIR}" rev-parse --short=9 HEAD OUTPUT_VARIABLE sha OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE r1)
execute_process(COMMAND git -C "${SOURCE_DIR}" status --porcelain --untracked-files=no OUTPUT_VARIABLE dirty OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT r1 EQUAL 0)
    set(sha "unknown")
elseif (NOT dirty STREQUAL "")
    set(sha "${sha}+dirty")
endif()
set(content "#pragma once\n#define DYNAMICAL_GIT_SHA \"${sha}\"\n")
if (EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if (NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()
