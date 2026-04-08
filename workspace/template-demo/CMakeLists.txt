cmake_minimum_required(VERSION 3.20)
project(template-demo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wshadow -O2 -g)
endif()

add_executable(${PROJECT_NAME} main.cpp)
