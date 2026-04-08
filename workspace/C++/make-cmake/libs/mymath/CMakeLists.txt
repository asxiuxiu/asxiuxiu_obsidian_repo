# ============================================================
# mymath — 静态库
# 知识点：add_library(... STATIC ...) + target_include_directories
# ============================================================

add_library(mymath STATIC
    src/mymath.cpp
)

# PUBLIC：调用方（main_app）也会继承这个 include 路径
# 这样 main_app 的 CMakeLists 不需要再手动 target_include_directories
target_include_directories(mymath PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
