cmake_minimum_required(VERSION 3.20)
project(graphic-pipeline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wshadow -O2 -g)
endif()

# ============================================================================
# Conan 依赖
# ============================================================================

find_package(glm REQUIRED)

# ============================================================================
# 主程序
# ============================================================================

add_executable(${PROJECT_NAME} main.cpp obj_loader.cpp)
target_link_libraries(${PROJECT_NAME} glm::glm)
