# ERC of one KiCad schematic: fails on errors, prints the report.
# cmake -DKICAD_CLI=... -DSCH=... -DREPORT=... -P kicad_erc.cmake
execute_process(COMMAND ${KICAD_CLI} sch erc --severity-error --exit-code-violations
                        -o ${REPORT} ${SCH}
                RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
message("${out}")
if(EXISTS ${REPORT})
    file(READ ${REPORT} report)
    message("${report}")
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "ERC failed (${result}): ${SCH}")
endif()
