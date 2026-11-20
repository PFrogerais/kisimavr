cmake_minimum_required(VERSION 3.18)
project(mcu_cosim VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- simavr ------------------------------------------------------------------
# Either installed (pkg-config / system paths) or given with
#   -DSIMAVR_INCLUDE_DIRS=... -DSIMAVR_LIBRARIES=...
if(NOT SIMAVR_INCLUDE_DIRS OR NOT SIMAVR_LIBRARIES)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SIMAVR_PC QUIET simavr)
    endif()
    find_path(SIMAVR_INCLUDE_DIRS sim_avr.h
        HINTS ${SIMAVR_PC_INCLUDE_DIRS} $ENV{HOME}/.local/include
        PATH_SUFFIXES simavr simavr/sim)
    find_library(SIMAVR_LIBRARIES simavr
        HINTS ${SIMAVR_PC_LIBRARY_DIRS} $ENV{HOME}/.local/lib $ENV{HOME}/.local/lib64)
    if(NOT SIMAVR_INCLUDE_DIRS OR NOT SIMAVR_LIBRARIES)
        message(FATAL_ERROR
            "simavr development files not found.\n"
            "  Fedora: sudo dnf install simavr-devel   (check: dnf provides '*/sim_avr.h')\n"
            "  Debian: sudo apt install libsimavr-dev\n"
            "  Or build simavr from source and pass\n"
            "    -DSIMAVR_INCLUDE_DIRS=<dir with sim_avr.h> -DSIMAVR_LIBRARIES=<path to libsimavr.so>")
    endif()
endif()
message(STATUS "simavr headers: ${SIMAVR_INCLUDE_DIRS}")
message(STATUS "simavr library: ${SIMAVR_LIBRARIES}")

# ---- Helpers without simavr (config, I2C devices, paths, VCD): unit-tested ---
set(KISIMAVR_WARNINGS -Wall -Wextra -Wpedantic)
add_library(kisimavr_core STATIC
    src/config.cpp src/hd44780.cpp src/i2c_devices.cpp src/i2c_slave_device.cpp
    src/lcd_device.cpp src/paths.cpp
    src/rp2040_config.cpp src/spi_frame.cpp src/uart_script.cpp src/vcd.cpp)
target_include_directories(kisimavr_core PUBLIC include src)
target_compile_options(kisimavr_core PRIVATE ${KISIMAVR_WARNINGS})
set_target_properties(kisimavr_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON    # Linked into the plug-in.
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

# ---- Plug-in loaded by ngspice d_cosim --------------------------------------
add_library(mcu_cosim MODULE
    src/mcu_cosim.cpp src/mcu_bridge.cpp src/sar_adc.cpp src/uart0.cpp src/i2c_bus.cpp
    src/spi_bus.cpp)
target_include_directories(mcu_cosim SYSTEM PRIVATE ${SIMAVR_INCLUDE_DIRS})
target_link_libraries(mcu_cosim PRIVATE kisimavr_core ${SIMAVR_LIBRARIES})
target_compile_options(mcu_cosim PRIVATE ${KISIMAVR_WARNINGS})
# Embed the simavr location so ngspice/KiCad need no LD_LIBRARY_PATH.
get_filename_component(SIMAVR_LIBDIR "${SIMAVR_LIBRARIES}" DIRECTORY)
set_target_properties(mcu_cosim PROPERTIES
    BUILD_RPATH   "${SIMAVR_LIBDIR}"
    INSTALL_RPATH "${SIMAVR_LIBDIR}"
    PREFIX ""                       # mcu_cosim.so
    CXX_VISIBILITY_PRESET hidden    # Only Cosim_setup is exported
    VISIBILITY_INLINES_HIDDEN ON)

# ---- RP2040 (Raspberry Pi Pico): rp2040js run by an embedded QuickJS -------
# QuickJS-ng is downloaded at configure time (pinned tag). Offline: pass
# -DKISIMAVR_RP2040=OFF, or -DFETCHCONTENT_SOURCE_DIR_QUICKJS=<local clone>.
option(KISIMAVR_RP2040 "RP2040 support (QuickJS + rp2040js)" ON)
if(KISIMAVR_RP2040)
    cmake_minimum_required(VERSION 3.28)    # FetchContent EXCLUDE_FROM_ALL / SYSTEM
    enable_language(C)
    include(FetchContent)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)    # Linked into the plug-in.
    FetchContent_Declare(quickjs
        GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
        GIT_TAG        v0.10.1
        GIT_SHALLOW    TRUE
        EXCLUDE_FROM_ALL
        SYSTEM)
    FetchContent_MakeAvailable(quickjs)
    # Third-party C code: its warnings are not ours (newer GCC, -Werror).
    target_compile_options(qjs PRIVATE -w -Wno-error)
    target_sources(mcu_cosim PRIVATE src/rp2040_device.cpp)
    target_link_libraries(mcu_cosim PRIVATE qjs)
    target_compile_definitions(mcu_cosim PRIVATE KISIMAVR_HAVE_RP2040=1)
    message(STATUS "RP2040 support: on (QuickJS-ng v0.10.1, third_party/rp2040js)")
else()
    message(STATUS "RP2040 support: off")
endif()

# ---- Stand-alone runner (tests the plug-in without ngspice) -----------------
add_executable(cosim_runner tools/main.cpp)
target_include_directories(cosim_runner PRIVATE include)
target_link_libraries(cosim_runner PRIVATE ${CMAKE_DL_LIBS})
target_compile_options(cosim_runner PRIVATE ${KISIMAVR_WARNINGS})

install(TARGETS mcu_cosim LIBRARY DESTINATION lib/ngspice)

# ---- KiCad demo models (absolute paths of this machine) ----------------------
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_custom_target(demos
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/setup_demos.py
                --so $<TARGET_FILE:mcu_cosim>
        DEPENDS mcu_cosim
        COMMENT "Writing the .lib models of kicad/projects/")
endif()

# ---- Tests (ctest) ----------------------------------------------------------
option(KISIMAVR_TESTS "Build the regression tests" ON)
if(KISIMAVR_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
