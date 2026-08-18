cmake_minimum_required(VERSION 3.20)

project(mkv-writer-cmake-consumer LANGUAGES CXX)

find_package(mkv-writer CONFIG REQUIRED)

enable_testing()
add_executable(mkv-writer-consumer-test main.cpp)
target_compile_features(mkv-writer-consumer-test PRIVATE cxx_std_17)
target_link_libraries(mkv-writer-consumer-test PRIVATE mkv-writer::mkv-writer)
add_test(NAME mkv-writer-consumer-test COMMAND mkv-writer-consumer-test)
