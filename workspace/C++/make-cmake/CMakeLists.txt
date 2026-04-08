# From note: C++/构建系统：make 与 CMake
# 最低 CMake 版本要求
cmake_minimum_required(VERSION 3.20)

# 项目名称和语言
project(MyProject VERSION 1.0 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# 子库（add_subdirectory 会读取各自的 CMakeLists.txt）
#
#   mymath → STATIC  静态库（编译时复制进可执行文件）
#   mystr  → SHARED  动态库（运行时加载，需要 .dylib/.so）
# ============================================================
add_subdirectory(libs/mymath)
add_subdirectory(libs/mystr)

# ============================================================
# 主可执行文件
# ============================================================
add_executable(my_app
    src/main.cpp
    # 注意：src/utils.cpp 和 src/math.cpp 已不再使用
    # 现在使用的是 libs/mymath 和 libs/mystr 子库
)

# include/ 目录已不再需要（旧的头文件已废弃）
# target_include_directories(my_app PRIVATE include/)

# 链接两个子库
# 因为 mymath/mystr 的 CMakeLists 里用了 PUBLIC include，
# 它们的 include 路径会自动传递给 my_app，无需再手动添加
target_link_libraries(my_app
    PRIVATE mymath   # 静态库：编译时代码直接合并进 my_app
    PRIVATE mystr    # 动态库：运行时加载 libmystr.dylib / libmystr.so
)

# ============================================================
# 查找和链接第三方库示例（按需启用）
# find_package(OpenSSL REQUIRED)
# target_link_libraries(my_app PRIVATE OpenSSL::SSL OpenSSL::Crypto)
# ============================================================
