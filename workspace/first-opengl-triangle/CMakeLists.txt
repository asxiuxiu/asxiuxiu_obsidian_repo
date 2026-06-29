cmake_minimum_required(VERSION 3.20)
project(first-opengl-triangle LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 严格使用 UCRT64 工具链；避免与 mingw64 的 DLL 混用
if(CMAKE_SYSTEM_NAME STREQUAL "Windows" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(EXISTS "C:/msys64/ucrt64/bin")
        list(PREPEND CMAKE_PREFIX_PATH "C:/msys64/ucrt64")
    endif()
endif()

include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(glfw)

add_executable(${PROJECT_NAME}
    src/glad.c
    src/main.cpp
)

# 构建完成后把 compile_commands.json 复制到项目根目录，方便 clangd 自动加载
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/compile_commands.json"
        "${CMAKE_SOURCE_DIR}/compile_commands.json"
)

target_include_directories(${PROJECT_NAME} PRIVATE include)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wshadow -O2 -g)
endif()

# Windows 上需要链接必要的系统库
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE gdi32 winmm)
endif()
