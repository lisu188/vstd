cmake_minimum_required(VERSION 4.0.2)
project(vstd_package_consumer LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(vstd 0.1 CONFIG REQUIRED)
add_executable(package_consumer main.cpp)
target_link_libraries(package_consumer PRIVATE vstd::headers)

enable_testing()
add_test(NAME package_consumer COMMAND package_consumer)
set_tests_properties(package_consumer PROPERTIES TIMEOUT 20)
