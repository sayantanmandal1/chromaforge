cmake_minimum_required(VERSION 3.15)
project(chromaforge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/W4 /O2)
else()
  add_compile_options(-Wall -Wextra -O3)
endif()

find_package(Threads REQUIRED)

add_library(chromaforge STATIC
  src/image.cpp
  src/color.cpp
  src/lut.cpp
  src/filters.cpp)
target_include_directories(chromaforge PUBLIC include)
target_link_libraries(chromaforge PUBLIC Threads::Threads)

add_executable(cf apps/cli.cpp)
target_link_libraries(cf PRIVATE chromaforge)

add_executable(cf_bench bench/bench.cpp)
target_link_libraries(cf_bench PRIVATE chromaforge)

add_executable(cf_tests tests/tests.cpp)
target_link_libraries(cf_tests PRIVATE chromaforge)

enable_testing()
add_test(NAME unit COMMAND cf_tests)
