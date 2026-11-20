# Run one command and check its output (stdout + stderr merged).
#
# Variables, set by the generated per-test script (see tests/CMakeLists.txt):
#   CMD      command and arguments (list)
#   WORKDIR  working directory
#   EXPECT   regular expressions that must ALL match the output
#   REJECT   regular expressions that must NOT match
#   TIMEOUT  seconds (default 120)

if(NOT DEFINED TIMEOUT)
    set(TIMEOUT 120)
endif()

execute_process(COMMAND ${CMD}
    WORKING_DIRECTORY "${WORKDIR}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE  output          # Same variable: both streams merged.
    RESULT_VARIABLE status
    TIMEOUT ${TIMEOUT})

set(failures "")
if(NOT status EQUAL 0)
    string(APPEND failures "  exit status: ${status}\n")
endif()
foreach(re IN LISTS EXPECT)
    if(NOT output MATCHES "${re}")
        string(APPEND failures "  missing:  ${re}\n")
    endif()
endforeach()
foreach(re IN LISTS REJECT)
    if(output MATCHES "${re}")
        string(APPEND failures "  rejected: ${re} (found '${CMAKE_MATCH_0}')\n")
    endif()
endforeach()

if(failures)
    message(FATAL_ERROR "Check failed:\n${failures}\n---- output ----\n${output}")
endif()
