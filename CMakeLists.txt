cmake_minimum_required(VERSION 3.20)
project(ultrahft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(memory_pool_test tests/unit/memory_pool_test.cpp)
target_include_directories(memory_pool_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME memory_pool_test COMMAND memory_pool_test)
