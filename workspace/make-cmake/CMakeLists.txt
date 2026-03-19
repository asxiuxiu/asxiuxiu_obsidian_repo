# From note: C++/构建系统：make 与 CMake
# 最低 CMake 版本要求
cmake_minimum_required(VERSION 3.20)

# 项目名称和语言
project(MyProject VERSION 1.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 收集源文件
set(SOURCES
    src/main.cpp
    src/utils.cpp
    src/math.cpp
)

# 定义可执行目标
add_executable(my_app ${SOURCES})

# 添加头文件搜索路径
target_include_directories(my_app PRIVATE include/)

# 链接库（如果有）
# target_link_libraries(my_app PRIVATE some_lib)

# 查找和链接第三方库示例（按需启用）
# find_package(OpenSSL REQUIRED)
# target_link_libraries(my_app PRIVATE OpenSSL::SSL OpenSSL::Crypto)
