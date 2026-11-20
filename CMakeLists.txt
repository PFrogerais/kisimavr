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

# ---- Plug-in loaded by ngspice d_cosim --------------------------------------
add_library(mcu_cosim MODULE src/mcu_cosim.cpp)
target_include_directories(mcu_cosim PRIVATE include)
target_include_directories(mcu_cosim SYSTEM PRIVATE ${SIMAVR_INCLUDE_DIRS})
target_link_libraries(mcu_cosim PRIVATE ${SIMAVR_LIBRARIES})
target_compile_options(mcu_cosim PRIVATE -Wall -Wextra -Wpedantic)
# Embed the simavr location so ngspice/KiCad need no LD_LIBRARY_PATH.
get_filename_component(SIMAVR_LIBDIR "${SIMAVR_LIBRARIES}" DIRECTORY)
set_target_properties(mcu_cosim PROPERTIES
    BUILD_RPATH   "${SIMAVR_LIBDIR}"
    INSTALL_RPATH "${SIMAVR_LIBDIR}"
    PREFIX ""                       # mcu_cosim.so
    CXX_VISIBILITY_PRESET hidden    # Only Cosim_setup is exported
    VISIBILITY_INLINES_HIDDEN ON)

# ---- Stand-alone runner (tests the plug-in without ngspice) -----------------
add_executable(cosim_runner tools/main.cpp)
target_include_directories(cosim_runner PRIVATE include)
target_link_libraries(cosim_runner PRIVATE ${CMAKE_DL_LIBS})
target_compile_options(cosim_runner PRIVATE -Wall -Wextra -Wpedantic)

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
