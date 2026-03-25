cmake_minimum_required(VERSION 3.10)
project(day02_contains_duplicate)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 源文件
set(SOURCES
    solution.cpp
    main.cpp
)

# 头文件路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# 可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})
