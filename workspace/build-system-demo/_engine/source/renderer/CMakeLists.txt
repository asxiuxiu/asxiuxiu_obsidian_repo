# Engine Renderer Module - 真实源码

add_library(RendererLib STATIC
    ${CMAKE_CURRENT_LIST_DIR}/renderer.cpp
)

target_include_directories(RendererLib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(RendererLib PUBLIC CoreLib)

message(STATUS "[Renderer] Building from: ${CMAKE_CURRENT_LIST_DIR}")
