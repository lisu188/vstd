# MIT License
#
# Copyright (c) 2019-2026 Andrzej Lis
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

cmake_minimum_required(VERSION 4.0.2)
project(vstd VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/RequireCxx26.cmake)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(vstd_headers INTERFACE)
add_library(vstd::headers ALIAS vstd_headers)
target_compile_features(vstd_headers INTERFACE cxx_std_26)
set_target_properties(vstd_headers PROPERTIES EXPORT_NAME headers)
target_include_directories(vstd_headers INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/vstd>")

find_package(Boost 1.58 REQUIRED COMPONENTS filesystem)
find_package(Threads REQUIRED)

target_link_libraries(vstd_headers INTERFACE Boost::headers Boost::filesystem Threads::Threads)

set(SOURCE_FILES
        vadaptors.h
        vcast.h
        vchain.h
        vconverter.h
        vdefines.h
        vfunctional.h
        vfuture.h
        vhash.h
        vhex.h
        vlazy.h
        vlogger.h
        vmove_only_function.h
        vthread.h
        vthreadpool.h
        vtraits.h
        vutil.h
        vassert.h
        vbind.h
        vcache.h
        vstd.h
        vstring.h
        vtask.h
        vmeta.h
        vneuro.h
        vany.h
        vtuple.h
        veventloop.h)

set(VSTD_PACKAGE_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/vstd")
install(TARGETS vstd_headers EXPORT vstdTargets)
install(FILES ${SOURCE_FILES} DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/vstd")
install(FILES LICENSE.md DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/vstd")
install(EXPORT vstdTargets NAMESPACE vstd:: DESTINATION "${VSTD_PACKAGE_INSTALL_DIR}")
export(EXPORT vstdTargets NAMESPACE vstd:: FILE "${CMAKE_CURRENT_BINARY_DIR}/vstdTargets.cmake")
configure_package_config_file(cmake/vstdConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/vstdConfig.cmake"
    INSTALL_DESTINATION "${VSTD_PACKAGE_INSTALL_DIR}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/vstdConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMinorVersion ARCH_INDEPENDENT)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/vstdConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/vstdConfigVersion.cmake"
    DESTINATION "${VSTD_PACKAGE_INSTALL_DIR}")

add_executable(vstd ${SOURCE_FILES} main.cpp)

target_link_libraries(vstd PRIVATE vstd::headers)

enable_testing()

add_executable(vmeta_tests tests/vmeta_tests.cpp)

target_link_libraries(vmeta_tests PRIVATE vstd::headers)
add_test(NAME vmeta_tests COMMAND vmeta_tests)
if(MSVC)
    target_compile_options(vmeta_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(vmeta_tests PRIVATE -UNDEBUG)
endif()

add_executable(vneuro_tests tests/vneuro_tests.cpp)
target_link_libraries(vneuro_tests PRIVATE vstd::headers)

add_test(NAME vneuro_tests COMMAND vneuro_tests)
if(MSVC)
    target_compile_options(vneuro_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(vneuro_tests PRIVATE -UNDEBUG)
endif()

option(VSTD_BUILD_BENCHMARKS "Build vstd benchmarks" OFF)
if(VSTD_BUILD_BENCHMARKS)
    add_executable(vneuro_bench benchmarks/vneuro_bench.cpp)
    target_link_libraries(vneuro_bench PRIVATE vstd::headers)

endif()

foreach(TEST_NAME vmeta_regressions vmeta_allocations vfuture_regressions vrandom_tests vfunctional_tests vstring_tests
                  vbind_tests vutil_regressions vthreadpool_regressions vtask_tests vcache_regressions
                  vmove_only_function_tests)
    add_executable(${TEST_NAME} tests/${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME} PRIVATE vstd::headers)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    if(MSVC)
        target_compile_options(${TEST_NAME} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${TEST_NAME} PRIVATE -UNDEBUG)
    endif()
endforeach()
add_executable(vmove_only_function_portable_tests tests/vmove_only_function_tests.cpp)
target_link_libraries(vmove_only_function_portable_tests PRIVATE vstd::headers Threads::Threads)
target_compile_definitions(vmove_only_function_portable_tests PRIVATE VSTD_FORCE_PORTABLE_MOVE_ONLY_FUNCTION=1)
if(MSVC)
    target_compile_options(vmove_only_function_portable_tests PRIVATE /UNDEBUG)
else()
    target_compile_options(vmove_only_function_portable_tests PRIVATE -UNDEBUG)
endif()
add_test(NAME vmove_only_function_portable_tests COMMAND vmove_only_function_portable_tests)
set_tests_properties(vmove_only_function_tests vmove_only_function_portable_tests PROPERTIES TIMEOUT 20)
foreach(TEST_NAME vtask_tests vthreadpool_regressions)
    add_executable(${TEST_NAME}_portable tests/${TEST_NAME}.cpp)
    target_link_libraries(${TEST_NAME}_portable PRIVATE vstd::headers Threads::Threads)
    target_compile_definitions(${TEST_NAME}_portable PRIVATE VSTD_FORCE_PORTABLE_MOVE_ONLY_FUNCTION=1)
    if(MSVC)
        target_compile_options(${TEST_NAME}_portable PRIVATE /UNDEBUG)
    else()
        target_compile_options(${TEST_NAME}_portable PRIVATE -UNDEBUG)
    endif()
    add_test(NAME ${TEST_NAME}_portable COMMAND ${TEST_NAME}_portable)
    set_tests_properties(${TEST_NAME}_portable PROPERTIES TIMEOUT 20)
endforeach()
set_tests_properties(vfuture_regressions vthreadpool_regressions PROPERTIES TIMEOUT 20)

option(VSTD_REQUIRE_INTEGRATION_TESTS "Require SDL2 and Boost.Python regression dependencies" OFF)
option(VSTD_REQUIRE_SDL_TESTS "Require headless SDL2 runtime regression dependencies" OFF)
if(VSTD_REQUIRE_INTEGRATION_TESTS)
    set(VSTD_INTEGRATION_FIND_MODE REQUIRED)
else()
    set(VSTD_INTEGRATION_FIND_MODE QUIET)
endif()
if(VSTD_REQUIRE_SDL_TESTS)
    find_package(SDL2 REQUIRED)
else()
    find_package(SDL2 ${VSTD_INTEGRATION_FIND_MODE})
endif()
if(SDL2_FOUND)
    add_executable(veventloop_startup_regressions tests/veventloop_startup_regressions.cpp)
    target_compile_definitions(veventloop_startup_regressions PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(veventloop_startup_regressions PRIVATE vstd::headers SDL2::SDL2)
    add_test(NAME veventloop_startup_regressions COMMAND veventloop_startup_regressions)
    set_tests_properties(veventloop_startup_regressions PROPERTIES TIMEOUT 20
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")

    add_executable(vruntime_regressions tests/vruntime_regressions.cpp)
    target_compile_definitions(vruntime_regressions PRIVATE SDL_MAIN_HANDLED)
    target_link_libraries(vruntime_regressions PRIVATE vstd::headers SDL2::SDL2)
    add_test(NAME vruntime_regressions COMMAND vruntime_regressions)
    set_tests_properties(vruntime_regressions PROPERTIES TIMEOUT 20
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy" LABELS performance)
    if(MSVC)
        target_compile_options(vruntime_regressions PRIVATE /UNDEBUG)
    else()
        target_compile_options(vruntime_regressions PRIVATE -UNDEBUG)
    endif()

    add_executable(vtask_eventloop_tests tests/vtask_tests.cpp)
    target_compile_definitions(vtask_eventloop_tests PRIVATE VSTD_TEST_EVENT_LOOP=1)
    target_link_libraries(vtask_eventloop_tests PRIVATE vstd::headers SDL2::SDL2)
    add_test(NAME vtask_eventloop_tests COMMAND vtask_eventloop_tests)
    set_tests_properties(vtask_eventloop_tests PROPERTIES TIMEOUT 20
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    if(MSVC)
        target_compile_options(vtask_eventloop_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(vtask_eventloop_tests PRIVATE -UNDEBUG)
    endif()
else()
    message(STATUS "SDL2 not found: headless event-loop regression tests are unavailable")
endif()

find_package(Python3 ${VSTD_INTEGRATION_FIND_MODE} COMPONENTS Development)
if(Python3_Development_FOUND)
    set(VSTD_BOOST_PYTHON_COMPONENT python${Python3_VERSION_MAJOR}${Python3_VERSION_MINOR})
    find_package(Boost 1.58 ${VSTD_INTEGRATION_FIND_MODE} COMPONENTS ${VSTD_BOOST_PYTHON_COMPONENT})
    if(TARGET Boost::${VSTD_BOOST_PYTHON_COMPONENT})
        add_executable(vconverter_regressions tests/vconverter_regressions.cpp)
        target_link_libraries(vconverter_regressions PRIVATE vstd::headers Boost::${VSTD_BOOST_PYTHON_COMPONENT} Python3::Python)
        add_test(NAME vconverter_regressions COMMAND vconverter_regressions)
        set_tests_properties(vconverter_regressions PROPERTIES ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    else()
        message(STATUS "Boost.Python not found: callback converter regression tests are unavailable")
    endif()
else()
    message(STATUS "Python development files not found: callback converter regression tests are unavailable")
endif()

set(HEADER_SELF_SUFFICIENCY_FILES ${SOURCE_FILES})
# vconverter.h depends on Boost.Python/Python development headers, which are not part of this target's configured deps.
list(REMOVE_ITEM HEADER_SELF_SUFFICIENCY_FILES vconverter.h)

set(HEADER_SELF_SUFFICIENCY_DIR ${CMAKE_CURRENT_BINARY_DIR}/header-self-sufficiency)
file(MAKE_DIRECTORY ${HEADER_SELF_SUFFICIENCY_DIR})
set(HEADER_SELF_SUFFICIENCY_SOURCES)
foreach(HEADER_FILE IN LISTS HEADER_SELF_SUFFICIENCY_FILES)
    string(MAKE_C_IDENTIFIER ${HEADER_FILE} HEADER_SYMBOL)
    set(HEADER_SOURCE ${HEADER_SELF_SUFFICIENCY_DIR}/${HEADER_SYMBOL}.cpp)
    file(WRITE ${HEADER_SOURCE}
            "#include \"${CMAKE_CURRENT_SOURCE_DIR}/${HEADER_FILE}\"\nvoid header_check_${HEADER_SYMBOL}() {}\n")
    list(APPEND HEADER_SELF_SUFFICIENCY_SOURCES ${HEADER_SOURCE})
endforeach()

set(HEADER_SELF_SUFFICIENCY_MAIN ${HEADER_SELF_SUFFICIENCY_DIR}/main.cpp)
file(WRITE ${HEADER_SELF_SUFFICIENCY_MAIN} "int main() { return 0; }\n")
list(APPEND HEADER_SELF_SUFFICIENCY_SOURCES ${HEADER_SELF_SUFFICIENCY_MAIN})

add_executable(header_self_sufficiency_tests ${HEADER_SELF_SUFFICIENCY_SOURCES})
target_link_libraries(header_self_sufficiency_tests PRIVATE vstd::headers)
add_test(NAME header_self_sufficiency_tests COMMAND header_self_sufficiency_tests)

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    set(FORMAT_FILES
            ${SOURCE_FILES}
            main.cpp
            tests/vmeta_tests.cpp
            tests/vmeta_regressions.cpp
            tests/vmeta_allocations.cpp
            tests/vneuro_tests.cpp
            tests/vfuture_regressions.cpp
            tests/vrandom_tests.cpp
            tests/vfunctional_tests.cpp
            tests/vstring_tests.cpp
            tests/vtask_tests.cpp
            tests/vbind_tests.cpp
            tests/vutil_regressions.cpp
            tests/vcache_regressions.cpp
            tests/vthreadpool_regressions.cpp
            tests/vmove_only_function_tests.cpp
            tests/vruntime_regressions.cpp
            tests/veventloop_startup_regressions.cpp
            tests/vconverter_regressions.cpp
            benchmarks/vneuro_bench.cpp
            tests/package_consumer/main.cpp)

    add_custom_target(format
            COMMAND ${CLANG_FORMAT_EXE} -i ${FORMAT_FILES}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Formatting C++ sources with clang-format")

    add_custom_target(format-check
            COMMAND ${CLANG_FORMAT_EXE} --dry-run --Werror ${FORMAT_FILES}
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            COMMENT "Checking C++ formatting with clang-format")
else()
    message(STATUS "clang-format not found: 'format' and 'format-check' targets are unavailable")
endif()
