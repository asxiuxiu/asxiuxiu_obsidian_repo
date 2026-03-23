cmake_minimum_required(VERSION 3.20)
project(AlgoPractice VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# GCC 警告选项（Windows MSYS2 / macOS 通用）
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wshadow -O2)
endif()

# 全局 include：公共工具头文件
include_directories(include)

# 开启测试
enable_testing()

# ============================================================
# 每天练习目录，按需添加
# ============================================================
add_subdirectory(day01_two_sum)
# add_subdirectory(day02_contains_duplicate)
# add_subdirectory(day03_valid_anagram)
