# ============================================================
# mystr — 动态库
# 知识点：add_library(... SHARED ...) + POSITION_INDEPENDENT_CODE
# ============================================================

add_library(mystr SHARED
    src/mystr.cpp
)

# 动态库必须开启位置无关代码（等价于 g++ -fPIC）
set_target_properties(mystr PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# PUBLIC：调用方自动继承 include 路径
target_include_directories(mystr PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
