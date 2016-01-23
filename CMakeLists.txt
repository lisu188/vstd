cmake_minimum_required(VERSION 3.3)
project(vstd)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

FIND_PACKAGE(Boost 1.60)

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
        vstd.h
        vstring.h
        vmeta.h
        vneuro.h
        vany.h)

add_executable(vstd ${SOURCE_FILES} main.cpp)