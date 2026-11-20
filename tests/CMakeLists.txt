# Regression tests: ctest --test-dir build --output-on-failure
#
# - runner.*   : cosim_runner drives the plug-in without ngspice (fast)
# - args.*     : configuration errors are reported, not crashes
# - ngspice.*  : end-to-end run of ngspice/full_test.cir (if ngspice found)
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
kisimavr_test(runner.adc_pwm_high FIXTURE firmware
    COMMAND ${RUN} -t 0.02 -a 0,5,5 ${SO} -- hex ${FW}/adc_pwm.hex out PD6 PB5 adc 0
    EXPECT [[out0: [0-9]+ changes, [0-9]+ rising edges, mean period 0\.128000 ms]]
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

# ---- Generated models ---------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    # kicad/atmega328p_full.lib must be what the generator writes today.
    kisimavr_test(models.full_lib_up_to_date WORKDIR ${PROJECT_SOURCE_DIR}/tools
        COMMAND ${Python3_EXECUTABLE} make_atmega328p_lib.py --relative
                --hex ../firmware/full_demo.hex --so ../build/mcu_cosim.so
                -o ${CMAKE_CURRENT_BINARY_DIR}/atmega328p_full.lib
        EXPECT [[written]])
    add_test(NAME models.full_lib_compare
        COMMAND ${CMAKE_COMMAND} -E compare_files
                ${CMAKE_CURRENT_BINARY_DIR}/atmega328p_full.lib
                ${PROJECT_SOURCE_DIR}/kicad/atmega328p_full.lib)
    set_tests_properties(models.full_lib_up_to_date PROPERTIES FIXTURES_SETUP full_lib)
    set_tests_properties(models.full_lib_compare PROPERTIES FIXTURES_REQUIRED full_lib)
endif()

# ---- End to end through ngspice -------------------------------------------------
# ngspice/full_test.cir loads ../build/mcu_cosim.so: only meaningful when the
# build directory is <source>/build.
find_program(NGSPICE ngspice)
if(NGSPICE AND PROJECT_BINARY_DIR STREQUAL "${PROJECT_SOURCE_DIR}/build")
    # Button with internal pull-up, ADC ramp on A0 and UART, all in SPICE.
    kisimavr_test(ngspice.full FIXTURE firmware TIMEOUT 300
        WORKDIR ${PROJECT_SOURCE_DIR}/ngspice
        COMMAND ${NGSPICE} -b full_test.cir
        EXPECT [[UART0> ATmega328P full model]]
               [[UART0> A0 = [0-9]+  BTN = 0]]
               [[UART0> A0 = [0-9]+  BTN = 1]]
        REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]])
    set_tests_properties(ngspice.full PROPERTIES LABELS ngspice)
elseif(NGSPICE)
    message(STATUS "ngspice test disabled: build directory is not <source>/build")
endif()
