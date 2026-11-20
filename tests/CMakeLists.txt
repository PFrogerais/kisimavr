# Regression tests: ctest --test-dir build --output-on-failure
#
# - unit       : config / I2C devices / paths / vcd modules, without simavr
# - runner.*   : cosim_runner drives the plug-in without ngspice (fast)
# - args.*     : configuration errors are reported, not crashes
# - ngspice.*  : end-to-end runs of ngspice/full_test.cir, i2c_test.cir, spi_test.cir and
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

# Raspberry Pi Pico firmware: bare metal, built with clang + lld (thumbv6m).
find_program(PICO_CLANG clang)
find_program(PICO_LLD ld.lld)
find_program(PICO_OBJCOPY llvm-objcopy)
set(PICO_TESTS FALSE)
if(PICO_CLANG AND PICO_LLD AND PICO_OBJCOPY AND KISIMAVR_RP2040)
    set(PICO_TESTS TRUE)
    add_test(NAME firmware_pico COMMAND ${MAKE_PROGRAM} -C ${FW}/pico
             PICO_CC=${PICO_CLANG} PICO_OBJCOPY=${PICO_OBJCOPY})
    set_tests_properties(firmware_pico PROPERTIES FIXTURES_SETUP firmware_pico)
    # GP25 blinks at 1 kHz, UART0 frames on GP0, a line received on GP1
    # (runner -u: frames on input 0) is answered.
    kisimavr_test(runner.pico FIXTURE firmware_pico TIMEOUT 300
        COMMAND ${RUN} -t 0.012 -u [[115200,Bonjour\n]] ${SO} -- mcu rp2040
                fw ${FW}/pico/pico_demo.bin io GP1 GP0 GP25
        EXPECT [[rp2040 \(rp2040js in QuickJS\)]] [[UART0> Pico pret]]
               [[UART0> Recu : Bonjour]] [[UART0> ADC0 = ]]
               [[out8: 2[0-9] changes, [0-9]+ rising edges, mean period (0\.99|1\.00)]]
               [[out4: [1-9][0-9]+ changes]]
        REJECT [[setup failed]] [[RP2040: ]])
    # Pico 2 (RP2350): the same demo for both core architectures, booted by
    # the A2 bootrom from its IMAGE_DEF block.
    add_test(NAME firmware_pico2 COMMAND ${MAKE_PROGRAM} -C ${FW}/pico2
             PICO_CC=${PICO_CLANG} PICO_OBJCOPY=${PICO_OBJCOPY})
    set_tests_properties(firmware_pico2 PROPERTIES FIXTURES_SETUP firmware_pico2)
    foreach(arch IN ITEMS riscv arm)
        if(arch STREQUAL "riscv")
            set(banner "RISC-V")
        else()
            set(banner "ARM")
        endif()
        kisimavr_test(runner.pico2_${arch} FIXTURE firmware_pico2 TIMEOUT 300
            COMMAND ${RUN} -t 0.012 -u [[115200,Bonjour\n]] ${SO} -- mcu rp2350
                    fw ${FW}/pico2/pico2_demo_${arch}.bin io GP1 GP0 GP25
            EXPECT "rp2350 ${arch} \\(rp2350js in QuickJS\\)" "UART0> Pico 2 pret \\(${banner}\\)"
                   [[UART0> Recu : Bonjour]] [[UART0> ADC0 = ]]
                   [[out8: 2[0-9] changes, [0-9]+ rising edges, mean period (0\.99|1\.00)]]
                   [[out4: [1-9][0-9]+ changes]]
            REJECT [[setup failed]] [[RP2350: ]])
    endforeach()
    # An RP2040 image has no IMAGE_DEF block: reported.
    kisimavr_test(runner.pico2_rp2040_image FIXTURE firmware_pico TIMEOUT 120
        COMMAND ${RUN} -t 0.0001 ${SO} -- mcu rp2350 fw ${FW}/pico/pico_demo.bin io GP25
        EXPECT [[RP2350: no IMAGE_DEF block]])
    kisimavr_test(args.pico2_bad_arch
        COMMAND ${RUN} -t 0.001 ${SO} -- mcu rp2350 fw x.uf2 arch x86 io GP25
        EXPECT [[invalid arch 'x86']] [[setup failed]])
    # I2C / SPI demos without their buses in the "io" list: NACK, zeros.
    kisimavr_test(runner.pico_i2c_nobus FIXTURE firmware_pico TIMEOUT 300
        COMMAND ${RUN} -t 0.003 ${SO} -- mcu rp2040 fw ${FW}/pico/pico_i2c.bin io GP0
        EXPECT [[UART0> LM75 : pas de reponse \(NACK\)]]
        REJECT [[setup failed]] [[RP2040: ]])
    kisimavr_test(runner.pico_spi FIXTURE firmware_pico TIMEOUT 300
        COMMAND ${RUN} -t 0.003 ${SO} -- mcu rp2040 fw ${FW}/pico/pico_spi.bin
                io GP0 GP16 GP17 GP18 GP19
        EXPECT [[UART0> SPI : envoye 02, relu 00]]
               [[out12: 4[0-9] changes]]
        REJECT [[setup failed]] [[RP2040: ]])
    kisimavr_test(args.pico_missing_fw
        COMMAND ${RUN} -t 0.001 ${SO} -- mcu rp2040 io GP25
        EXPECT [[missing firmware]] [[setup failed]])
    kisimavr_test(args.pico_bad_gpio
        COMMAND ${RUN} -t 0.001 ${SO} -- mcu rp2040 fw x.uf2 io GP30
        EXPECT [[invalid or repeated GPIO 'GP30']] [[setup failed]])
else()
    message(STATUS "Pico tests disabled: clang, ld.lld, llvm-objcopy or KISIMAVR_RP2040 missing")
endif()

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

# SPI master: 2 x 8 SCK periods per step, MOSI frames, read back logged.
kisimavr_test(runner.spi FIXTURE firmware
    COMMAND ${RUN} -t 0.003 ${SO} -- hex ${FW}/spi_595.hex out PB5 PB3 PB2 PD1 io PB4
    EXPECT [[UART0> SPI 74HC595 : chenillard]] [[SPI> MOSI 01 \| MISO]]
           [[out0: 16 changes, 8 rising edges]] [[out2: 3 changes]]
    REJECT [[setup failed]] [[during a transfer]])

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

    # Raspberry Pi Pico model.
    kisimavr_test(models.pico_up_to_date WORKDIR ${PROJECT_SOURCE_DIR}/tools
        COMMAND ${Python3_EXECUTABLE} make_pico_lib.py --relative --so ../build/mcu_cosim.so
                --fw ../firmware/pico/pico_demo.bin -o ${CMAKE_CURRENT_BINARY_DIR}/models/pico.lib
        EXPECT [[written]])
    add_test(NAME models.pico_compare
        COMMAND ${CMAKE_COMMAND} -E compare_files ${CMAKE_CURRENT_BINARY_DIR}/models/pico.lib
                ${PROJECT_SOURCE_DIR}/ngspice/pico.lib)
    set_tests_properties(models.pico_up_to_date PROPERTIES FIXTURES_SETUP model_pico)
    set_tests_properties(models.pico_compare PROPERTIES FIXTURES_REQUIRED model_pico)
    kisimavr_test(models.pico2_up_to_date WORKDIR ${PROJECT_SOURCE_DIR}/tools
        COMMAND ${Python3_EXECUTABLE} make_pico_lib.py --relative --mcu rp2350
                --so ../build/mcu_cosim.so --fw ../firmware/pico2/pico2_demo_riscv.bin
                -o ${CMAKE_CURRENT_BINARY_DIR}/models/pico2.lib
        EXPECT [[written]])
    add_test(NAME models.pico2_compare
        COMMAND ${CMAKE_COMMAND} -E compare_files ${CMAKE_CURRENT_BINARY_DIR}/models/pico2.lib
                ${PROJECT_SOURCE_DIR}/ngspice/pico2.lib)
    set_tests_properties(models.pico2_up_to_date PROPERTIES FIXTURES_SETUP model_pico2)
    set_tests_properties(models.pico2_compare PROPERTIES FIXTURES_REQUIRED model_pico2)

    # I2C slaves model.
    kisimavr_test(models.i2c_up_to_date WORKDIR ${PROJECT_SOURCE_DIR}/tools
        COMMAND ${Python3_EXECUTABLE} make_i2c_lib.py --relative --so ../build/mcu_cosim.so
                --lm75 0x48 --temp 25.5 --lcd 0x27 --log
                -o ${CMAKE_CURRENT_BINARY_DIR}/models/i2c_slaves.lib
        EXPECT [[written]])
    add_test(NAME models.i2c_compare
        COMMAND ${CMAKE_COMMAND} -E compare_files ${CMAKE_CURRENT_BINARY_DIR}/models/i2c_slaves.lib
                ${PROJECT_SOURCE_DIR}/ngspice/i2c_slaves.lib)
    set_tests_properties(models.i2c_up_to_date PROPERTIES FIXTURES_SETUP model_i2c)
    set_tests_properties(models.i2c_compare PROPERTIES FIXTURES_REQUIRED model_i2c)

    # Character LCD model.
    kisimavr_test(models.lcd1602_up_to_date WORKDIR ${PROJECT_SOURCE_DIR}/tools
        COMMAND ${Python3_EXECUTABLE} make_lcd_lib.py --relative --so ../build/mcu_cosim.so
                -o ${CMAKE_CURRENT_BINARY_DIR}/models/lcd1602.lib
        EXPECT [[written]])
    add_test(NAME models.lcd1602_compare
        COMMAND ${CMAKE_COMMAND} -E compare_files ${CMAKE_CURRENT_BINARY_DIR}/models/lcd1602.lib
                ${PROJECT_SOURCE_DIR}/ngspice/lcd1602.lib)
    set_tests_properties(models.lcd1602_up_to_date PROPERTIES FIXTURES_SETUP model_lcd1602)
    set_tests_properties(models.lcd1602_compare PROPERTIES FIXTURES_REQUIRED model_lcd1602)

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
    kisimavr_model_test(spi_lib ngspice/atmega328p_spi.lib --hex ../firmware/spi_595.hex --i2c=)
    kisimavr_model_test(lcd_mcu_lib ngspice/atmega328p_lcd.lib --hex ../firmware/lcd_demo.hex --i2c=)
    kisimavr_model_test(radar_lib ngspice/atmega328p_radar.lib --hex ../firmware/radar.hex --i2c=)
    kisimavr_model_test(ventilo_lib ngspice/atmega328p_ventilo.lib --hex ../firmware/ventilo.hex
                        --i2c=)
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
    # SPI master on the pins, 74HC595 model in SPICE: QH' comes back on MISO,
    # so each byte read is the previous one sent (the first is 00).
    kisimavr_test(ngspice.spi FIXTURE firmware TIMEOUT 300
        WORKDIR ${PROJECT_SOURCE_DIR}/ngspice
        COMMAND ${NGSPICE} -b spi_test.cir
        EXPECT [[UART0> SPI 74HC595 : chenillard]]
               [[SPI> MOSI 01 \| MISO 00]]
               [[SPI> MOSI 04 \| MISO 02]]
               [[UART0> Q = 08 +relu = 04]]
               [[UART0> Q = 80 +relu = 40]]
        REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[during a transfer]])
    set_tests_properties(ngspice.spi PROPERTIES LABELS ngspice)
    # Character LCD as a second co-simulated part, wired like LiquidCrystal.
    kisimavr_test(ngspice.lcd FIXTURE firmware TIMEOUT 300
        WORKDIR ${PROJECT_SOURCE_DIR}/ngspice
        COMMAND ${NGSPICE} -b lcd_test.cir
        EXPECT [[LCD: HD44780 16x2]]
               [=[LCD> \[Bonjour STI2D   \] \[A0 = [0-4]\.[0-9][0-9] V     \]]=]
        REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[warning]] [[Segmentation]])
    set_tests_properties(ngspice.lcd PROPERTIES LABELS ngspice)
    if(PICO_TESTS)
        # Pico in SPICE: ADC0 on a 1.055 V divider, button, LED, UART frames.
        kisimavr_test(ngspice.pico FIXTURE firmware_pico TIMEOUT 600
            WORKDIR ${PROJECT_SOURCE_DIR}/ngspice
            COMMAND ${NGSPICE} -b pico_test.cir
            EXPECT [[UART0> Pico pret]]
                   [=[UART0> ADC0 = 1[23][0-9][0-9] \(1\.0[4-7] V\)]=]
                   [[UART0> VSYS = 0.00 V, USB : non]]
            REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[RP2040: ]])
        set_tests_properties(ngspice.pico PROPERTIES LABELS ngspice)
        # Pico I2C: LM75 and LCD I2C (PCF8574) slaves decoded bit by bit from
        # the SDA / SCL voltages, ACK and data bits pulled low by them.
        kisimavr_test(ngspice.pico_i2c FIXTURE firmware_pico TIMEOUT 900
            WORKDIR ${PROJECT_SOURCE_DIR}/ngspice
            COMMAND ${NGSPICE} -b pico_i2c_test.cir
            EXPECT [[I2C slaves on SDA / SCL: LM75 0x48, PCF8574 0x27]]
                   [[I2C> 0x48 W 00]] [[I2C> 0x48 R 19 80]]
                   [[UART0> LM75 : T = 25.5 C]] [[UART0> LCD : ecrit]]
                   [=[LCD> \[Pico I2C        \] \[T = 25.5 C      \]]=]
            REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[RP2040: ]] [[NACK]])
        set_tests_properties(ngspice.pico_i2c PROPERTIES LABELS ngspice)
        # Pico SPI: 74HC595 at 3.3 V, QH' read back on GP16.
        kisimavr_test(ngspice.pico_spi FIXTURE firmware_pico TIMEOUT 900
            WORKDIR ${PROJECT_SOURCE_DIR}/ngspice
            COMMAND ${NGSPICE} -b pico_spi_test.cir
            EXPECT [[UART0> Pico SPI : 74HC595, chenillard]]
                   [[UART0> SPI : envoye 02, relu 01]] [[UART0> SPI : envoye 04, relu 02]]
            REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[RP2040: ]])
        set_tests_properties(ngspice.pico_spi PROPERTIES LABELS ngspice)
        # Sensors and actuators: HC-SR04 + servo, NTC + L298N + DC motor, with an
        # ATmega328P and with a Pico.
        foreach(board IN ITEMS avr pico)
            if(board STREQUAL "avr")
                set(prefix "")
                set(fixture firmware)
                set(reject [[cannot open firmware]])
            else()
                set(prefix "pico_")
                set(fixture firmware_pico)
                set(reject [[RP2040: ]])
            endif()
            kisimavr_test(ngspice.${prefix}radar FIXTURE ${fixture} TIMEOUT 900
                WORKDIR ${PROJECT_SOURCE_DIR}/ngspice
                COMMAND ${NGSPICE} -b ${prefix}radar_test.cir
                EXPECT [[UART0> Radar : HC-SR04 \+ servo]]
                       [=[HCSR04> (39|40)\.[0-9] cm \(echo 23[0-9][0-9] us\)]=]
                       [=[UART0> Distance = (39|40)\.[0-9] cm, servo 12[0-9] deg]=]
                       [=[SERVO> [0-9]+ deg \(pulse [0-9]+ us\)]=]
                       [[UART0> Distance = 2[0-9]\.[0-9] cm]]
                REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[hors de portee]] ${reject})
            set_tests_properties(ngspice.${prefix}radar PROPERTIES LABELS ngspice)
            kisimavr_test(ngspice.${prefix}ventilo FIXTURE ${fixture} TIMEOUT 900
                WORKDIR ${PROJECT_SOURCE_DIR}/ngspice
                COMMAND ${NGSPICE} -b ${prefix}ventilo_test.cir
                EXPECT [[UART0> Ventilateur : CTN \+ L298N]]
                       [[UART0> T = 2[0-4]\.[0-9] C, moteur 0 %]]
                       [[UART0> T = 3[0-4]\.[0-9] C, moteur [5-9][0-9] %]]
                       [[UART0> T = 4[0-9]\.[0-9] C, moteur 100 %]]
                       [[vitesse += +[3-7]\.[0-9]+e\+00]]
                REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] ${reject})
            set_tests_properties(ngspice.${prefix}ventilo PROPERTIES LABELS ngspice)
        endforeach()
        # Pico 2 (RP2350, RISC-V cores): the same circuit.
        kisimavr_test(ngspice.pico2 FIXTURE firmware_pico2 TIMEOUT 600
            WORKDIR ${PROJECT_SOURCE_DIR}/ngspice
            COMMAND ${NGSPICE} -b pico2_test.cir
            EXPECT [[rp2350 riscv]] [=[UART0> Pico 2 pret \(RISC-V\)]=]
                   [=[UART0> ADC0 = 1[23][0-9][0-9] \(1\.0[4-7] V\)]=]
                   [[UART0> VSYS = 0.00 V, USB : non]]
            REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[RP2350: ]])
        set_tests_properties(ngspice.pico2 PROPERTIES LABELS ngspice)
    endif()
elseif(NGSPICE_OK)
    message(STATUS "ngspice test disabled: build directory is not <source>/build")
endif()

# ---- KiCad projects (kicad-cli) -------------------------------------------------
# ERC of every demo schematic, then the Arduino UNO schematic exported as a
# SPICE netlist by KiCad itself and simulated by ngspice.
find_program(KICAD_CLI kicad-cli)
if(KICAD_CLI AND Python3_FOUND)
    # Root sheet of each project (sub-sheets are checked through it).
    file(GLOB kicad_projects ${PROJECT_SOURCE_DIR}/kicad/projects/*/*.kicad_pro)
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/kicad)
    foreach(pro IN LISTS kicad_projects)
        get_filename_component(project ${pro} NAME_WE)
        get_filename_component(dir ${pro} DIRECTORY)
        set(sch ${dir}/${project}.kicad_sch)
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
            COMMAND ${CMAKE_COMMAND} -DKICAD_CLI=${KICAD_CLI} -DOUT=${uno_net}
                    -DSCH=${PROJECT_SOURCE_DIR}/kicad/projects/Arduino_UNO/Arduino_UNO.kicad_sch
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/kicad_netlist.cmake)
        set_tests_properties(kicad.uno_netlist PROPERTIES FIXTURES_SETUP uno_netlist
                             LABELS kicad TIMEOUT 120)

        kisimavr_test(ngspice.kicad_uno TIMEOUT 300
            WORKDIR ${PROJECT_SOURCE_DIR}/kicad/projects/Arduino_UNO
            # Batch mode without .print: -r (raw file) makes ngspice run the .tran.
            COMMAND ${NGSPICE} -b -r ${CMAKE_CURRENT_BINARY_DIR}/kicad/Arduino_UNO.raw ${uno_net}
            EXPECT [[UART0> Arduino UNO pret]]
                   [[UART0< Bonjour]] [[UART0> Recu : Bonjour]]
                   [[UART0< BLINK OFF]] [[UART0> OK BLINK OFF]]
                   [[UART0< BLINK ON]] [[UART0> OK BLINK ON]]
            REJECT [[setup failed]] [[mismatched]] [[framing error]] [[[Ee]rror]])
        set_tests_properties(ngspice.kicad_uno PROPERTIES LABELS "ngspice;kicad"
                             FIXTURES_REQUIRED "firmware;demos;uno_netlist")

        # SPI project: 74HC595 model with the official symbol.
        set(spi_net ${CMAKE_CURRENT_BINARY_DIR}/kicad/simAVR_spi.cir)
        add_test(NAME kicad.spi_netlist
            COMMAND ${CMAKE_COMMAND} -DKICAD_CLI=${KICAD_CLI} -DOUT=${spi_net}
                    -DSCH=${PROJECT_SOURCE_DIR}/kicad/projects/simAVR_spi/simAVR_spi.kicad_sch
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/kicad_netlist.cmake)
        set_tests_properties(kicad.spi_netlist PROPERTIES FIXTURES_SETUP spi_netlist
                             LABELS kicad TIMEOUT 120)
        kisimavr_test(ngspice.kicad_spi TIMEOUT 300
            WORKDIR ${PROJECT_SOURCE_DIR}/kicad/projects/simAVR_spi
            COMMAND ${NGSPICE} -b -r ${CMAKE_CURRENT_BINARY_DIR}/kicad/simAVR_spi.raw ${spi_net}
            EXPECT [[SPI> MOSI 02 \| MISO 01]] [[UART0> Q = 10 +relu = 08]]
            REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[during a transfer]])
        set_tests_properties(ngspice.kicad_spi PROPERTIES LABELS "ngspice;kicad"
                             FIXTURES_REQUIRED "firmware;demos;spi_netlist")

        # Pico project: official symbol MCU_Module:RaspberryPi_Pico.
        if(PICO_TESTS)
            set(pico_net ${CMAKE_CURRENT_BINARY_DIR}/kicad/simPICO.cir)
            add_test(NAME kicad.pico_netlist
                COMMAND ${CMAKE_COMMAND} -DKICAD_CLI=${KICAD_CLI} -DOUT=${pico_net}
                        -DSCH=${PROJECT_SOURCE_DIR}/kicad/projects/simPICO/simPICO.kicad_sch
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/kicad_netlist.cmake)
            set_tests_properties(kicad.pico_netlist PROPERTIES FIXTURES_SETUP pico_netlist
                                 LABELS kicad TIMEOUT 120)
            kisimavr_test(ngspice.kicad_pico TIMEOUT 600
                WORKDIR ${PROJECT_SOURCE_DIR}/kicad/projects/simPICO
                COMMAND ${NGSPICE} -b -r ${CMAKE_CURRENT_BINARY_DIR}/kicad/simPICO.raw ${pico_net}
                EXPECT [[UART0> Pico pret]] [[UART0> Recu : Bonjour Pico]]
                       [=[UART0> ADC0 = 1[23][0-9][0-9] \(1\.0[4-7] V\)]=]
                REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[RP2040: ]])
            set_tests_properties(ngspice.kicad_pico PROPERTIES LABELS "ngspice;kicad"
                                 FIXTURES_REQUIRED "firmware_pico;demos;pico_netlist")
        endif()

        # Pico board after the official schematic (hierarchical sheet).
        if(PICO_TESTS)
            set(pboard_net ${CMAKE_CURRENT_BINARY_DIR}/kicad/RaspberryPi_Pico.cir)
            add_test(NAME kicad.picoboard_netlist
                COMMAND ${CMAKE_COMMAND} -DKICAD_CLI=${KICAD_CLI} -DOUT=${pboard_net}
                        -DSCH=${PROJECT_SOURCE_DIR}/kicad/projects/RaspberryPi_Pico/RaspberryPi_Pico.kicad_sch
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/kicad_netlist.cmake)
            set_tests_properties(kicad.picoboard_netlist PROPERTIES
                                 FIXTURES_SETUP picoboard_netlist LABELS kicad TIMEOUT 120)
            # USB present (V2): VSYS = 5 V - D1, read on ADC3 through R5 / R6 and
            # Q1; GPIO24 senses VBUS through R10 / R1.
            kisimavr_test(ngspice.kicad_picoboard TIMEOUT 600
                WORKDIR ${PROJECT_SOURCE_DIR}/kicad/projects/RaspberryPi_Pico
                COMMAND ${NGSPICE} -b -r ${CMAKE_CURRENT_BINARY_DIR}/kicad/RaspberryPi_Pico.raw
                        ${pboard_net}
                EXPECT [[UART0> Pico pret]] [=[UART0> VSYS = 4\.[5-9][0-9] V, USB : oui]=]
                REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[RP2040: ]])
            set_tests_properties(ngspice.kicad_picoboard PROPERTIES LABELS "ngspice;kicad"
                                 FIXTURES_REQUIRED "firmware_pico;demos;picoboard_netlist")

            # Pico I2C / SPI masters (simPICO_i2c, simPICO_spi).
            foreach(bus IN ITEMS i2c spi)
                set(bus_net ${CMAKE_CURRENT_BINARY_DIR}/kicad/simPICO_${bus}.cir)
                add_test(NAME kicad.pico_${bus}_netlist
                    COMMAND ${CMAKE_COMMAND} -DKICAD_CLI=${KICAD_CLI} -DOUT=${bus_net}
                            -DSCH=${PROJECT_SOURCE_DIR}/kicad/projects/simPICO_${bus}/simPICO_${bus}.kicad_sch
                            -P ${CMAKE_CURRENT_SOURCE_DIR}/kicad_netlist.cmake)
                set_tests_properties(kicad.pico_${bus}_netlist PROPERTIES
                                     FIXTURES_SETUP pico_${bus}_netlist LABELS kicad TIMEOUT 120)
                if(bus STREQUAL "i2c")
                    set(expect [=[UART0> LM75 : T = 25\.5 C]=]
                               [=[LCD> \[Pico I2C        \] \[T = 25\.5 C      \]]=])
                else()
                    set(expect [[UART0> SPI : envoye 04, relu 02]])
                endif()
                kisimavr_test(ngspice.kicad_pico_${bus} TIMEOUT 900
                    WORKDIR ${PROJECT_SOURCE_DIR}/kicad/projects/simPICO_${bus}
                    COMMAND ${NGSPICE} -b -r ${CMAKE_CURRENT_BINARY_DIR}/kicad/simPICO_${bus}.raw
                            ${bus_net}
                    EXPECT ${expect}
                    REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[RP2040: ]] [[NACK]])
                set_tests_properties(ngspice.kicad_pico_${bus} PROPERTIES LABELS "ngspice;kicad"
                    FIXTURES_REQUIRED "firmware_pico;demos;pico_${bus}_netlist")
            endforeach()

            # Sensors and actuators: simAVR_radar / simAVR_ventilo / simPICO_radar /
            # simPICO_ventilo, netlists exported by KiCad.
            foreach(project IN ITEMS simAVR_radar simAVR_ventilo simPICO_radar simPICO_ventilo)
                set(mnet ${CMAKE_CURRENT_BINARY_DIR}/kicad/${project}.cir)
                add_test(NAME kicad.${project}_netlist
                    COMMAND ${CMAKE_COMMAND} -DKICAD_CLI=${KICAD_CLI} -DOUT=${mnet}
                            -DSCH=${PROJECT_SOURCE_DIR}/kicad/projects/${project}/${project}.kicad_sch
                            -P ${CMAKE_CURRENT_SOURCE_DIR}/kicad_netlist.cmake)
                set_tests_properties(kicad.${project}_netlist PROPERTIES
                                     FIXTURES_SETUP ${project}_netlist LABELS kicad TIMEOUT 120)
                if(project MATCHES "radar")
                    set(expect [=[UART0> Distance = (39|40)\.[0-9] cm, servo 12[0-9] deg]=]
                               [=[SERVO> [0-9]+ deg]=])
                else()
                    set(expect [=[UART0> T = 4[0-9]\.[0-9] C, moteur 100 %]=])
                endif()
                kisimavr_test(ngspice.kicad_${project} TIMEOUT 900
                    WORKDIR ${PROJECT_SOURCE_DIR}/kicad/projects/${project}
                    COMMAND ${NGSPICE} -b -r ${CMAKE_CURRENT_BINARY_DIR}/kicad/${project}.raw ${mnet}
                    EXPECT ${expect}
                    REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[RP2040: ]])
                set_tests_properties(ngspice.kicad_${project} PROPERTIES LABELS "ngspice;kicad"
                    FIXTURES_REQUIRED "firmware;firmware_pico;demos;${project}_netlist")
            endforeach()

            # Pico 2 (RP2350, RISC-V cores): the same two projects.
            set(pico2_net ${CMAKE_CURRENT_BINARY_DIR}/kicad/simPICO2.cir)
            add_test(NAME kicad.pico2_netlist
                COMMAND ${CMAKE_COMMAND} -DKICAD_CLI=${KICAD_CLI} -DOUT=${pico2_net}
                        -DSCH=${PROJECT_SOURCE_DIR}/kicad/projects/simPICO2/simPICO2.kicad_sch
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/kicad_netlist.cmake)
            set_tests_properties(kicad.pico2_netlist PROPERTIES FIXTURES_SETUP pico2_netlist
                                 LABELS kicad TIMEOUT 120)
            kisimavr_test(ngspice.kicad_pico2 TIMEOUT 600
                WORKDIR ${PROJECT_SOURCE_DIR}/kicad/projects/simPICO2
                COMMAND ${NGSPICE} -b -r ${CMAKE_CURRENT_BINARY_DIR}/kicad/simPICO2.raw ${pico2_net}
                EXPECT [=[UART0> Pico 2 pret \(RISC-V\)]=] [[UART0> Recu : Bonjour Pico]]
                       [=[UART0> ADC0 = 1[23][0-9][0-9] \(1\.0[4-7] V\)]=]
                REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[RP2350: ]])
            set_tests_properties(ngspice.kicad_pico2 PROPERTIES LABELS "ngspice;kicad"
                                 FIXTURES_REQUIRED "firmware_pico2;demos;pico2_netlist")

            set(p2board_net ${CMAKE_CURRENT_BINARY_DIR}/kicad/RaspberryPi_Pico2.cir)
            add_test(NAME kicad.pico2board_netlist
                COMMAND ${CMAKE_COMMAND} -DKICAD_CLI=${KICAD_CLI} -DOUT=${p2board_net}
                        -DSCH=${PROJECT_SOURCE_DIR}/kicad/projects/RaspberryPi_Pico2/RaspberryPi_Pico2.kicad_sch
                        -P ${CMAKE_CURRENT_SOURCE_DIR}/kicad_netlist.cmake)
            set_tests_properties(kicad.pico2board_netlist PROPERTIES
                                 FIXTURES_SETUP pico2board_netlist LABELS kicad TIMEOUT 120)
            kisimavr_test(ngspice.kicad_pico2board TIMEOUT 600
                WORKDIR ${PROJECT_SOURCE_DIR}/kicad/projects/RaspberryPi_Pico2
                COMMAND ${NGSPICE} -b -r ${CMAKE_CURRENT_BINARY_DIR}/kicad/RaspberryPi_Pico2.raw
                        ${p2board_net}
                EXPECT [=[UART0> Pico 2 pret \(RISC-V\)]=]
                       [=[UART0> VSYS = 4\.[5-9][0-9] V, USB : oui]=]
                REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[RP2350: ]])
            set_tests_properties(ngspice.kicad_pico2board PROPERTIES LABELS "ngspice;kicad"
                                 FIXTURES_REQUIRED "firmware_pico2;demos;pico2board_netlist")
        endif()

        # LCD project: the display is a second co-simulated part.
        set(lcd_net ${CMAKE_CURRENT_BINARY_DIR}/kicad/simAVR_lcd.cir)
        add_test(NAME kicad.lcd_netlist
            COMMAND ${CMAKE_COMMAND} -DKICAD_CLI=${KICAD_CLI} -DOUT=${lcd_net}
                    -DSCH=${PROJECT_SOURCE_DIR}/kicad/projects/simAVR_lcd/simAVR_lcd.kicad_sch
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/kicad_netlist.cmake)
        set_tests_properties(kicad.lcd_netlist PROPERTIES FIXTURES_SETUP lcd_netlist
                             LABELS kicad TIMEOUT 120)
        kisimavr_test(ngspice.kicad_lcd TIMEOUT 300
            WORKDIR ${PROJECT_SOURCE_DIR}/kicad/projects/simAVR_lcd
            COMMAND ${NGSPICE} -b -r ${CMAKE_CURRENT_BINARY_DIR}/kicad/simAVR_lcd.raw ${lcd_net}
            EXPECT [=[LCD> \[Bonjour STI2D   \] \[A0 = [0-4]\.[0-9][0-9] V     \]]=]
            REJECT [[setup failed]] [[mismatched]] [[[Ee]rror]] [[warning]])
        set_tests_properties(ngspice.kicad_lcd PROPERTIES LABELS "ngspice;kicad"
                             FIXTURES_REQUIRED "firmware;demos;lcd_netlist")
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
