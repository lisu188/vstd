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
project(vstd VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Boost 1.58 REQUIRED COMPONENTS system filesystem)
find_package(Threads REQUIRED)

set(VSTD_HEADER_FILES
  vadaptors.h
  vany.h
  vassert.h
  vbind.h
  vcache.h
  vcast.h
  vchain.h
  vconverter.h
  vdefines.h
  veventloop.h
  vfunctional.h
  vfuture.h
  vhash.h
  vhex.h
  vlazy.h
  vlogger.h
  vmeta.h
  vneuro.h
  vstd.h
  vstring.h
  vthread.h
  vthreadpool.h
  vtraits.h
  vtuple.h
  vutil.h
)

add_library(vstd INTERFACE)
add_library(vstd::vstd ALIAS vstd)

option(VSTD_BUILD_EXAMPLES "Build vstd example targets" ON)

# Header-only target that propagates include dirs and required dependencies.
target_include_directories(vstd
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(vstd INTERFACE cxx_std_23)

target_link_libraries(vstd
  INTERFACE
    Boost::system
    Boost::filesystem
    Threads::Threads
)

if(VSTD_BUILD_EXAMPLES)
  add_executable(vstd_example main.cpp)
  target_link_libraries(vstd_example PRIVATE vstd::vstd)
endif()

install(TARGETS vstd EXPORT vstdTargets)
install(FILES ${VSTD_HEADER_FILES}
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT vstdTargets
  FILE vstdTargets.cmake
  NAMESPACE vstd::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vstd
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vstdConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/vstdConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vstd
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/vstdConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/vstdConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/vstdConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vstd
)

export(EXPORT vstdTargets
  FILE ${CMAKE_CURRENT_BINARY_DIR}/vstdTargets.cmake
  NAMESPACE vstd::
)
