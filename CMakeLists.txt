cmake_minimum_required(VERSION 3.20)
project(ultrahft LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(memory_pool_test tests/unit/memory_pool_test.cpp)
target_include_directories(memory_pool_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME memory_pool_test COMMAND memory_pool_test)

# DPDK support
find_package(PkgConfig REQUIRED)
pkg_check_modules(DPDK libdpdk)

if(DPDK_FOUND)
  add_executable(market_data_pipeline_demo examples/market_data_pipeline_demo.cpp)
  target_include_directories(market_data_pipeline_demo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DPDK_INCLUDE_DIRS}
  )
  target_compile_options(market_data_pipeline_demo PRIVATE ${DPDK_CFLAGS_OTHER})
  target_link_libraries(market_data_pipeline_demo PRIVATE ${DPDK_LIBRARIES})
  target_link_directories(market_data_pipeline_demo PRIVATE ${DPDK_LIBRARY_DIRS})
else()
  message(WARNING "DPDK not found – skipping market_data_pipeline_demo")
endif()
