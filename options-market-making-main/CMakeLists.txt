cmake_minimum_required(VERSION 3.14)
project(OptionsMarketMaking VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -Wall -Wextra")

# Main executable
add_executable(omme src/main.cpp)
target_include_directories(omme PRIVATE include)

# Benchmarks
add_executable(bench_tv benchmarks/bench_tv_engine.cpp)
target_include_directories(bench_tv PRIVATE include)

add_executable(bench_quotes benchmarks/bench_quote_gen.cpp)
target_include_directories(bench_quotes PRIVATE include)

add_executable(bench_system benchmarks/bench_full_system.cpp)
target_include_directories(bench_system PRIVATE include)

# Optional: if you have test files
# enable_testing()
# add_subdirectory(tests)
