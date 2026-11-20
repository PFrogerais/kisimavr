# The "uartlog" / "vcd" outputs must never replace a file the plug-in did
# not write. Run with -DRUN=<cosim_runner> -DSO=<mcu_cosim.so>
# -DHEX=<uart_adc.hex> -DDIR=<scratch directory>.

file(REMOVE_RECURSE "${DIR}")
file(MAKE_DIRECTORY "${DIR}")
set(failures "")

# Run the UART demo with the given output files; output in "out".
function(run uartlog vcd)
    execute_process(
        COMMAND "${RUN}" -t 0.002 -a 1,4,5 "${SO}" --
                hex "${HEX}" out PD1 adc 0 uartlog "${uartlog}" vcd "${vcd}"
        OUTPUT_VARIABLE out ERROR_VARIABLE out RESULT_VARIABLE rc TIMEOUT 60)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "cosim_runner failed (${rc}):\n${out}")
    endif()
    set(out "${out}" PARENT_SCOPE)
endfunction()

# expect(<if() condition, as code> <description>)
# The condition is parsed twice: write regexes without backslashes ([.], [$]).
macro(expect cond what)
    cmake_language(EVAL CODE "
        if(NOT (${cond}))
            string(APPEND failures \"  ${what}\\n---- output ----\\n\${out}\\n\")
        endif()")
endmacro()

# 1. New files are written; a second run replaces the plug-in's own files.
foreach(pass 1 2)
    run("${DIR}/uart_log.txt" "${DIR}/trace.vcd")
    file(READ "${DIR}/uart_log.txt" log)
    file(READ "${DIR}/trace.vcd" vcd)
    expect([[log MATCHES "Hello STI2D !"]] "run ${pass}: UART log written")
    expect([[vcd MATCHES "^[$]timescale"]] "run ${pass}: VCD written")
endforeach()

# 2. Wrong extension: refused, file untouched.
file(WRITE "${DIR}/main.c" "int main(void) { return 0; }\n")
run("${DIR}/main.c" "${DIR}/main.h")
file(READ "${DIR}/main.c" src)
expect([[out MATCHES "not writing UART log .*main[.]c': unexpected file extension"]]
       "UART log with .c extension refused")
expect([[out MATCHES "not writing VCD .*main[.]h': unexpected file extension"]]
       "VCD with .h extension refused")
expect([[src STREQUAL "int main(void) { return 0; }\n"]] "main.c untouched")
expect([[NOT EXISTS "${DIR}/main.h"]] "main.h not created")

# 3. Existing file not written by the plug-in: refused, untouched.
file(WRITE "${DIR}/notes.txt" "keep\tme\n")
file(WRITE "${DIR}/other.vcd" "not a trace\n")
run("${DIR}/notes.txt" "${DIR}/other.vcd")
file(READ "${DIR}/notes.txt" notes)
file(READ "${DIR}/other.vcd" other)
expect([[out MATCHES "notes[.]txt': existing file was not written by mcu_cosim"]]
       "foreign .txt refused")
expect([[out MATCHES "other[.]vcd': existing file was not written by mcu_cosim"]]
       "foreign .vcd refused")
expect([[notes STREQUAL "keep\tme\n"]] "notes.txt untouched")
expect([[other STREQUAL "not a trace\n"]] "other.vcd untouched")

# 4. Symbolic link (even to a file that would be accepted): refused.
file(WRITE "${DIR}/target.txt" "")
file(CREATE_LINK "${DIR}/target.txt" "${DIR}/link.txt" SYMBOLIC)
run("${DIR}/link.txt" "")
expect([[out MATCHES "link[.]txt': is a symbolic link"]] "symbolic link refused")

# 5. Not a regular file.
file(MAKE_DIRECTORY "${DIR}/dir.txt")
run("${DIR}/dir.txt" "")
expect([[out MATCHES "not writing UART log .*dir[.]txt'"]] "directory refused")

if(failures)
    message(FATAL_ERROR "Output guard checks failed:\n${failures}")
endif()
