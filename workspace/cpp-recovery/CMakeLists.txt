cmake_minimum_required(VERSION 3.20)
project(cpp-recovery LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wshadow -O2 -g)
elseif(MSVC)
    add_compile_options(/W4 /utf-8)
endif()

include_directories(include)

add_subdirectory(week01)
