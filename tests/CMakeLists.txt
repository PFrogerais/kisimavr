# Regression tests: ctest --test-dir build --output-on-failure
#
# - unit       : config / I2C devices / paths / vcd modules, without simavr
# - runner.*   : cosim_runner drives the plug-in without ngspice (fast)
# - args.*     : configuration errors are reported, not crashes
# - ngspice.*  : end-to-end runs of ngspice/full_test.cir, i2c_test.cir and
#                uart_rx_test.cir (two MCUs)
# - models.*   : the repository models match their generator
#
# The demo firmwares are built first by the "firmware" fixture (avr-gcc).

set(FW  ${PROJECT_SOURCE_DIR}/firmware)
set(RUN $<TARGET_FILE:cosim_runner>)
set(SO  $<TARGET_FILE:mcu_cosim>)

# kisimavr_test(name [WORKDIR dir] [TIMEOUT s] [FIXTURE f]
#               COMMAND ... [EXPECT re...] [REJECT re...])
function(kisimavr_test name)
    cmake_parse_arguments(T "" "WORKDIR;TIMEOUT;FIXTURE" "COMMAND;EXPECT;REJECT" ${ARGN})
    if(NOT T_WORKDIR)
        set(T_WORKDIR ${PROJECT_SOURCE_DIR})
    endif()
    if(NOT T_TIMEOUT)
        set(T_TIMEOUT 120)
    endif()

    # One script per test: bracket arguments keep regexes and paths verbatim.
    set(script "")
    foreach(var IN ITEMS COMMAND EXPECT REJECT)
        set(items "")
        foreach(item IN LISTS T_${var})
            string(APPEND items " [==[${item}]==]")
        endforeach()
        string(APPEND script "set(${var}${items})\n")
    endforeach()
    string(APPEND script
        "set(CMD \${COMMAND})\n"
        "set(WORKDIR [==[${T_WORKDIR}]==])\n"
        "set(TIMEOUT ${T_TIMEOUT})\n"
        "include([==[${CMAKE_CURRENT_SOURCE_DIR}/run_and_check.cmake]==])\n")
    set(file ${CMAKE_CURRENT_BINARY_DIR}/${name}.cmake)
    file(GENERATE OUTPUT ${file} CONTENT "${script}")

    add_test(NAME ${name} COMMAND ${CMAKE_COMMAND} -P ${file})
    set_tests_properties(${name} PROPERTIES TIMEOUT ${T_TIMEOUT})
    if(T_FIXTURE)
        set_tests_properties(${name} PROPERTIES FIXTURES_REQUIRED ${T_FIXTURE})
    endif()
endfunction()

# ---- Unit tests of the simavr-independent modules ------------------------------
add_executable(unit_tests unit_tests.cpp)
target_link_libraries(unit_tests PRIVATE kisimavr_core)
target_compile_options(unit_tests PRIVATE ${KISIMAVR_WARNINGS})
add_test(NAME unit COMMAND unit_tests ${CMAKE_CURRENT_BINARY_DIR}/unit_scratch)

# ---- Configuration errors (rejected before the firmware is read) ------------
kisimavr_test(args.missing_hex
    COMMAND ${RUN} -t 0.001 ${SO} -- out PB5
    EXPECT [[missing firmware]] [[setup failed]])
kisimavr_test(args.unreadable_hex
    COMMAND ${RUN} -t 0.001 ${SO} -- hex /nonexistent/fw.hex out PB5
    EXPECT [[cannot open firmware '/nonexistent/fw\.hex']] [[setup failed]])
kisimavr_test(args.invalid_pin
    COMMAND ${RUN} -t 0.001 ${SO} -- hex ${FW}/blink.hex out PZ9
    EXPECT [[invalid pin name 'PZ9']] [[setup failed]])
kisimavr_test(args.invalid_i2c_device
    COMMAND ${RUN} -t 0.001 ${SO} -- hex ${FW}/blink.hex i2c lm75 eeprom 0x48
    EXPECT [[two I2C slaves at address 0x48]] [[setup failed]])
kisimavr_test(args.invalid_adc_channel
    COMMAND ${RUN} -t 0.001 ${SO} -- hex ${FW}/adc_pwm.hex adc 16
    EXPECT [[invalid ADC channel '16']] [[setup failed]])

# ---- Plug-in driven by cosim_runner ------------------------------------------
find_program(AVR_GCC avr-gcc)
find_program(MAKE_PROGRAM NAMES gmake make)
if(NOT AVR_GCC OR NOT MAKE_PROGRAM)
    message(STATUS "avr-gcc or make not found: firmware tests disabled")
    return()
endif()

add_test(NAME firmware COMMAND ${MAKE_PROGRAM} -C ${FW})
set_tests_properties(firmware PROPERTIES FIXTURES_SETUP firmware)

# The "uartin" file is read after the firmware: needs a built firmware.
kisimavr_test(args.bad_uartin FIXTURE firmware
    COMMAND ${RUN} -t 0.001 ${SO} -- hex ${FW}/blink.hex
            uartin ${CMAKE_CURRENT_SOURCE_DIR}/data/uart_bad.txt
    EXPECT [[uart_bad\.txt', line 2: invalid time '2x']] [[setup failed]])
kisimavr_test(args.missing_port FIXTURE firmware
    COMMAND ${RUN} -t 0.001 ${SO} -- hex ${FW}/blink.hex out PL0
    EXPECT [[port L does not exist on atmega328p]] [[setup failed]])

# Timer0 interrupt toggles PB5 at 1 kHz; PB0 copies PD2 (square wave, 4 ms).
kisimavr_test(runner.blink FIXTURE firmware
    COMMAND ${RUN} -t 0.02 -p 0.004 ${SO} -- hex ${FW}/blink.hex in PD2 out PB5 PB0
    EXPECT [[out0: [0-9]+ changes, [0-9]+ rising edges, mean period 1\.000[0-9]* ms]]
           [[out1: [0-9]+ changes, [0-9]+ rising edges, mean period (3\.99|4\.00)[0-9]* ms]]
    REJECT [[setup failed]] [[MCU stopped]])

# PWM on PD6 at 16 MHz / 8 / 256 = 7812.5 Hz; LED on PB5 once ADC0 >= 2.5 V.
# Median, not mean: simavr does not buffer OCR0A until BOTTOM, so an update
# in the middle of a period may add an isolated extra pulse.
kisimavr_test(runner.adc_pwm_high FIXTURE firmware
    COMMAND ${RUN} -t 0.02 -a 0,5,5 ${SO} -- hex ${FW}/adc_pwm.hex out PD6 PB5 adc 0
    EXPECT [[out0: [0-9]+ changes, [0-9]+ rising edges, mean period [0-9.]+ ms \([0-9.]+ Hz\), median period 0\.128[0-9]* ms]]
           [[out1: [0-9]+ changes, 1 rising edges]]
    REJECT [[setup failed]])
kisimavr_test(runner.adc_pwm_low FIXTURE firmware
    COMMAND ${RUN} -t 0.02 -a 0,2,5 ${SO} -- hex ${FW}/adc_pwm.hex out PD6 PB5 adc 0
    EXPECT [[out1: [0-9]+ changes, 0 rising edges]]
    REJECT [[setup failed]])

# UART0 text, ADC value (ramp 1 -> 4 V over 5 ms: ~1.87 V at 1.45 ms) and
# TXD frames rebuilt on PD1.
kisimavr_test(runner.uart FIXTURE firmware
    COMMAND ${RUN} -t 0.005 -a 1,4,5 ${SO} -- hex ${FW}/uart_adc.hex out PD1 adc 0
    EXPECT [[UART0> Hello STI2D !]]
           [[UART0> ADC0 = 3[78][0-9] \(18[0-9][0-9] mV\)]]
           [[out0: [1-9][0-9]* changes]]
    REJECT [[setup failed]])

# I2C: EEPROM written then read back, LM75 at 25 degC, open-drain SDA/SCL
# rebuilt on PC4/PC5 (out1..3 = PC4 level/oe/pu, out4..6 = PC5).
kisimavr_test(runner.i2c FIXTURE firmware
    COMMAND ${RUN} -t 0.006 ${SO} -- hex ${FW}/i2c_lm75.hex out PD1 io PC4 PC5
            i2c lm75 eeprom
    EXPECT [[I2C slave lm75 at 0x48]]
           [[I2C> 0x50 W 00 53 54 49 32 44]]
           [[I2C> 0x50 W 00 \| 0x50 R 53 54 49 32 44]]
           [[I2C> 0x48 W 00 \| 0x48 R 19 00]]
           [[UART0> EEPROM : STI2D]]
           [[UART0> T = 25\.0 C]]
           [[out2: [1-9][0-9]* changes]]
           [[out5: [1-9][0-9]* changes]]
    REJECT [[setup failed]] [[MCU stopped]])
# Same firmware with no slave on the bus: every address is NACKed.
kisimavr_test(runner.i2c_absent FIXTURE firmware
    COMMAND ${RUN} -t 0.004 ${SO} -- hex ${FW}/i2c_lm75.hex out PD1
    EXPECT [[I2C> 0x48 W \(NACK\)]] [[UART0> LM75 absent]]
    REJECT [[setup failed]] [[I2C slave]])

# UART reception from a timed file: commands, answers, LED on PB5 (out1).
kisimavr_test(runner.uart_rx_file FIXTURE firmware
    COMMAND ${RUN} -t 0.006 ${SO} -- hex ${FW}/uart_cmd.hex out PD1 PB5
            uartin ${CMAKE_CURRENT_SOURCE_DIR}/data/uart_commands.txt
    EXPECT [[UART0 input: 3 event]]
           [[UART0< LED ON]] [[UART0> OK LED ON]]
           [[UART0< Bonjour]] [[UART0> Recu : Bonjour]]
           [[UART0> OK LED OFF]]
           [[out1: 2 changes, 1 rising edges]]
    REJECT [[setup failed]] [[lost]] [[framing error]])
# UART reception from frames on the RXD pin (runner -u: 8N1 at the baud
# rate of the firmware, 16 MHz / 8 / 17 = 117647 bauds).
kisimavr_test(runner.uart_rx_pin FIXTURE firmware
    COMMAND ${RUN} -t 0.004 -u [[117647,LED\sON\n]] ${SO} -- hex ${FW}/uart_cmd.hex
            in PD0 out PD1 PB5
    EXPECT [[UART0< LED ON]] [[UART0> OK LED ON]] [[out1: 1 changes, 1 rising edges]]
    REJECT [[setup failed]] [[framing error]])

# Arduino UNO demo: banner, command on the RXD pin, blinking stopped
# (without the command, D13 would toggle every millisecond: ~8 changes).
kisimavr_test(runner.uno_demo FIXTURE firmware
    COMMAND ${RUN} -t 0.008 -u [[117647,BLINK\sOFF\n]] ${SO} -- hex ${FW}/uno_demo.hex
            in PD0 out PD1 PB5
    EXPECT [[UART0> Arduino UNO pret]] [[UART0< BLINK OFF]] [[UART0> OK BLINK OFF]]
           [[out1: [1-3] changes]]
    REJECT [[setup failed]] [[framing error]])

# ---- Generated models ---------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    # Each repository model must be what the generator writes today.
    # kisimavr_model_test(name repository_file generator_args...)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/models)
    function(kisimavr_model_test name file)
        get_filename_component(base ${file} NAME)
        set(out ${CMAKE_CURRENT_BINARY_DIR}/models/${base})
        kisimavr_test(models.${name}_up_to_date WORKDIR ${PROJECT_SOURCE_DIR}/tools
            COMMAND ${Python3_EXECUTABLE} make_atmega328p_lib.py --relative
                    --so ../build/mcu_cosim.so ${ARGN} -o ${out}
            EXPECT [[written]])
        add_test(NAME models.${name}_compare
            COMMAND ${CMAKE_COMMAND} -E compare_files ${out} ${PROJECT_SOURCE_DIR}/${file})
        set_tests_properties(models.${name}_up_to_date PROPERTIES FIXTURES_SETUP model_${name})
        set_tests_properties(models.${name}_compare PROPERTIES FIXTURES_REQUIRED model_${name})
    endfunction()

    # Terminal of the Arduino UNO project, rebuilt from its terminal.txt.
    set(uno ${PROJECT_SOURCE_DIR}/kicad/projects/Arduino_UNO)
    kisimavr_test(models.uno_terminal_up_to_date
        COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/make_terminal_lib.py
                ${uno}/terminal.txt -o ${CMAKE_CURRENT_BINARY_DIR}/models/terminal.lib
        EXPECT [[written]])
    add_test(NAME models.uno_terminal_compare
        COMMAND ${CMAKE_COMMAND} -E compare_files ${CMAKE_CURRENT_BINARY_DIR}/models/terminal.lib
                ${uno}/terminal.lib)
    set_tests_properties(models.uno_terminal_up_to_date PROPERTIES FIXTURES_SETUP model_uno_terminal)
    set_tests_properties(models.uno_terminal_compare PROPERTIES FIXTURES_REQUIRED model_uno_terminal)

    kisimavr_model_test(full_lib kicad/atmega328p_full.lib --hex ../firmware/full_demo.hex)
    kisimavr_model_test(i2c_lib ngspice/atmega328p_i2c.lib --hex ../firmware/i2c_lm75.hex
                        --lm75temp 20 --lm75rate 1000)
    kisimavr_model_test(uart_a_lib ngspice/atmega328p_a.lib --name A
                        --hex ../firmware/uart_adc.hex --uartlog ../uart_log_a.txt --i2c=)
    kisimavr_model_test(uart_b_lib ngspice/atmega328p_b.lib --name B
                        --hex ../firmware/uart_cmd.hex --uartlog ../uart_log_b.txt --i2c=
                        --irreversible 2)
endif()

# ---- End to end through ngspice -------------------------------------------------
# ngspice/full_test.cir loads ../build/mcu_cosim.so: only meaningful when the
# build directory is <source>/build.
# d_cosim takes "sim_args" since ngspice 44: older versions ignore it.
find_program(NGSPICE ngspice)
set(NGSPICE_OK FALSE)
if(NGSPICE)
    # sim_args reach the plug-in from ngspice 44 on.
    execute_process(COMMAND ${NGSPICE} -v OUTPUT_VARIABLE ngspice_banner
                    ERROR_VARIABLE ngspice_banner TIMEOUT 20)
    string(REGEX MATCH "ngspice-([0-9]+)" _ "${ngspice_banner}")
    if(NOT CMAKE_MATCH_1 OR CMAKE_MATCH_1 LESS 44)
        message(STATUS "ngspice test disabled: ngspice >= 44 required (found '${CMAKE_MATCH_1}')")
    else()
        set(NGSPICE_OK TRUE)
    endif()
endif()
if(NGSPICE_OK AND PROJECT_BINARY_DIR STREQUAL "${PROJECT_SOURCE_DIR}/build")
    # Button with internal pull-up, ADC ramp on A0 and UART, all in SPICE.
    kisimavr_test(ngspice.full FIXTURE firmware TIMEOUT 300
        WORKDIR ${PROJECT_SOURCE_DIR}/ngspice
        COMMAND ${NGSPICE} -b full_test.cir
        EXPECT [[UART0> ATmega328P full model]]
               [[UART0> A0 = [0-9]+  BTN = 0]]
               [[UART0> A0 = [0-9]+  BTN = 1]]
        REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]])
    set_tests_properties(ngspice.full PROPERTIES LABELS ngspice)

    # I2C bus in SPICE: open-drain SDA/SCL with 4.7k pull-ups and 100 pF.
    kisimavr_test(ngspice.i2c FIXTURE firmware TIMEOUT 300
        WORKDIR ${PROJECT_SOURCE_DIR}/ngspice
        COMMAND ${NGSPICE} -b i2c_test.cir
        EXPECT [[I2C> 0x50 W 00 \| 0x50 R 53 54 49 32 44]]
               [[UART0> EEPROM : STI2D]]
               [[UART0> T = 2[0-9]\.[05] C]]
        REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]])
    set_tests_properties(ngspice.i2c PROPERTIES LABELS ngspice)

    # Two MCUs: TXD of A (uart_adc) wired to RXD of B (uart_cmd), which
    # decodes the frames from the circuit and answers. B enables its
    # receiver in the middle of the first frame of A, so (as on real chips)
    # the banner is garbled until the first pause; the next lines must be
    # received intact.
    kisimavr_test(ngspice.uart_rx FIXTURE firmware TIMEOUT 300
        WORKDIR ${PROJECT_SOURCE_DIR}/ngspice
        COMMAND ${NGSPICE} -b uart_rx_test.cir
        EXPECT [[UART0> Hello STI2D !]]
               [[UART0< ADC0 = 51[0-9] \(2[45][0-9][0-9] mV\)]]
               [[UART0> Recu : ADC0 = 51[0-9] \(2[45][0-9][0-9] mV\)]]
        REJECT [[setup failed]] [[mismatched]] [[Segmentation]])
    set_tests_properties(ngspice.uart_rx PROPERTIES LABELS ngspice)
elseif(NGSPICE_OK)
    message(STATUS "ngspice test disabled: build directory is not <source>/build")
endif()

# ---- KiCad projects (kicad-cli) -------------------------------------------------
# ERC of every demo schematic, then the Arduino UNO schematic exported as a
# SPICE netlist by KiCad itself and simulated by ngspice.
find_program(KICAD_CLI kicad-cli)
if(KICAD_CLI AND Python3_FOUND)
    file(GLOB kicad_schematics ${PROJECT_SOURCE_DIR}/kicad/projects/*/*.kicad_sch)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/kicad)
    foreach(sch IN LISTS kicad_schematics)
        get_filename_component(project ${sch} NAME_WE)
        add_test(NAME kicad.erc.${project}
            COMMAND ${CMAKE_COMMAND} -DKICAD_CLI=${KICAD_CLI} -DSCH=${sch}
                    -DREPORT=${CMAKE_CURRENT_BINARY_DIR}/kicad/${project}.rpt
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/kicad_erc.cmake)
        set_tests_properties(kicad.erc.${project} PROPERTIES LABELS kicad TIMEOUT 120)
    endforeach()

    if(NGSPICE_OK AND PROJECT_BINARY_DIR STREQUAL "${PROJECT_SOURCE_DIR}/build")
        # Demo models with this machine's paths (as "cmake --build build --target demos").
        kisimavr_test(kicad.demos_setup FIXTURE firmware
            COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/setup_demos.py --so ${SO}
            EXPECT [[terminal\.lib]])
        set_tests_properties(kicad.demos_setup PROPERTIES FIXTURES_SETUP demos LABELS kicad)

        set(uno_net ${CMAKE_CURRENT_BINARY_DIR}/kicad/Arduino_UNO.cir)
        add_test(NAME kicad.uno_netlist
            COMMAND ${KICAD_CLI} sch export netlist --format spice -o ${uno_net}
                    ${PROJECT_SOURCE_DIR}/kicad/projects/Arduino_UNO/Arduino_UNO.kicad_sch)
        set_tests_properties(kicad.uno_netlist PROPERTIES FIXTURES_SETUP uno_netlist
                             LABELS kicad TIMEOUT 120)

        kisimavr_test(ngspice.kicad_uno TIMEOUT 300
            WORKDIR ${PROJECT_SOURCE_DIR}/kicad/projects/Arduino_UNO
            COMMAND ${NGSPICE} -b ${uno_net}
            EXPECT [[UART0> Arduino UNO pret]]
                   [[UART0< Bonjour]] [[UART0> Recu : Bonjour]]
                   [[UART0< BLINK OFF]] [[UART0> OK BLINK OFF]]
                   [[UART0< BLINK ON]] [[UART0> OK BLINK ON]]
            REJECT [[setup failed]] [[mismatched]] [[framing error]] [[[Ee]rror]])
        set_tests_properties(ngspice.kicad_uno PROPERTIES LABELS "ngspice;kicad"
                             FIXTURES_REQUIRED "firmware;demos;uno_netlist")
    endif()
else()
    message(STATUS "KiCad tests disabled: kicad-cli not found")
endif()

# ---- Output files never replace a file the plug-in did not write ----------------
add_test(NAME outputs.guard
    COMMAND ${CMAKE_COMMAND}
            -DRUN=${RUN} -DSO=${SO} -DHEX=${FW}/uart_adc.hex
            -DDIR=${CMAKE_CURRENT_BINARY_DIR}/output_guard
            -P ${CMAKE_CURRENT_SOURCE_DIR}/output_guard.cmake)
set_tests_properties(outputs.guard PROPERTIES FIXTURES_REQUIRED firmware)
