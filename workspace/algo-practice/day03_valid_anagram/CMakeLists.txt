cmake_minimum_required(VERSION 3.10)
project(day03_valid_anagram)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(day03_valid_anagram
    main.cpp
    solution.cpp
    solution.h
)

target_include_directories(day03_valid_anagram PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)