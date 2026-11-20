# SPICE netlist of a KiCad schematic, as the KiCad simulator builds it.
# cmake -DKICAD_CLI=... -DSCH=... -DOUT=... -P kicad_netlist.cmake
#
# "kicad-cli sch export netlist --format spice" leaves out the analysis
# command, which the simulator takes from the schematic text (".tran ..."):
# it is added here, before ".end".
execute_process(COMMAND ${KICAD_CLI} sch export netlist --format spice -o ${OUT} ${SCH}
                RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
message("${out}")
if(NOT result EQUAL 0 OR NOT EXISTS ${OUT})
    message(FATAL_ERROR "netlist export failed (${result})")
endif()
file(READ ${OUT} netlist)
if(NOT netlist MATCHES "\n\\.tran ")
    file(READ ${SCH} schematic)
    if(NOT schematic MATCHES "\\(text \"(\\.tran [^\"]*)\"")
        message(FATAL_ERROR "no .tran directive in ${SCH}")
    endif()
    set(tran "${CMAKE_MATCH_1}")
    string(REGEX REPLACE "\n\\.end[ \t\r]*\n?$" "\n${tran}\n.end\n" netlist "${netlist}")
    if(NOT netlist MATCHES "\n\\.tran ")
        string(APPEND netlist "${tran}\n.end\n")
    endif()
    file(WRITE ${OUT} "${netlist}")
endif()
message("${netlist}")
