# MIT License
#
# Copyright (c) 2019 Andrzej Lis
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

cmake_minimum_required(VERSION 3.16)
project(vstd LANGUAGES CXX)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost 1.58 REQUIRED COMPONENTS filesystem)
find_package(Threads REQUIRED)

include_directories(${Boost_INCLUDE_DIRS})

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
        vthread.h
        vthreadpool.h
        vtraits.h
        vutil.h
        vassert.h
        vbind.h
        vcache.h
        vstd.h
        vstring.h
        vmeta.h
        vneuro.h
        vany.h
        vtuple.h
        veventloop.h)

add_executable(vstd ${SOURCE_FILES} main.cpp)

target_link_libraries(vstd PRIVATE Boost::filesystem Threads::Threads)

enable_testing()

add_executable(vmeta_tests tests/vmeta_tests.cpp)
target_include_directories(vmeta_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vmeta_tests PRIVATE Boost::filesystem Threads::Threads)
add_test(NAME vmeta_tests COMMAND vmeta_tests)

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
target_include_directories(header_self_sufficiency_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(header_self_sufficiency_tests PRIVATE Boost::filesystem Threads::Threads)
add_test(NAME header_self_sufficiency_tests COMMAND header_self_sufficiency_tests)

find_program(CLANG_FORMAT_EXE NAMES clang-format)
if(CLANG_FORMAT_EXE)
    set(FORMAT_FILES
            ${SOURCE_FILES}
            main.cpp
            tests/vmeta_tests.cpp)

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
