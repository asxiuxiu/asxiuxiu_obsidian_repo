# Engine Core Module - 真实源码

add_library(CoreLib STATIC
    ${CMAKE_CURRENT_LIST_DIR}/core.cpp
)

target_include_directories(CoreLib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

message(STATUS "[Core] Building from: ${CMAKE_CURRENT_LIST_DIR}")
