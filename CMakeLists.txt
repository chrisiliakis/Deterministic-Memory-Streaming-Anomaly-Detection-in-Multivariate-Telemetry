cmake_minimum_required(VERSION 3.20)
project(StreamGuard VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STREAM_GUARD_ENABLE_ASAN "Enable Address and Undefined Behavior Sanitizers" OFF)
option(STREAM_GUARD_ENABLE_TSAN "Enable Thread Sanitizer" OFF)

add_library(stream_guard INTERFACE)
target_include_directories(stream_guard INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(stream_guard INTERFACE
    -Wall -Wextra -Wpedantic -Werror -O3 -march=native
)

if(UNIX AND NOT APPLE)
    target_link_libraries(stream_guard INTERFACE rt pthread)
endif()

if(STREAM_GUARD_ENABLE_ASAN)
    target_compile_options(stream_guard INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(stream_guard INTERFACE -fsanitize=address,undefined)
endif()

if(STREAM_GUARD_ENABLE_TSAN)
    target_compile_options(stream_guard INTERFACE -fsanitize=thread)
    target_link_options(stream_guard INTERFACE -fsanitize=thread)
endif()

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest googlebenchmark)

enable_testing()
add_executable(stream_guard_tests tests/test_all.cpp)
target_link_libraries(stream_guard_tests PRIVATE stream_guard GTest::gtest_main)
add_test(NAME StreamGuardUnitTests COMMAND stream_guard_tests)

add_executable(synth_producer tools/synth_producer.cpp)
target_link_libraries(synth_producer PRIVATE stream_guard)

add_executable(replay_producer tools/replay_producer.cpp)
target_link_libraries(replay_producer PRIVATE stream_guard)

add_executable(guard_monitor tools/guard_monitor.cpp)
target_link_libraries(guard_monitor PRIVATE stream_guard)

add_executable(bench_pipeline benchmarks/bench_pipeline.cpp)
target_link_libraries(bench_pipeline PRIVATE stream_guard benchmark::benchmark)