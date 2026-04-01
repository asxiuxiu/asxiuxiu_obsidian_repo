# Game Client Module - 真实源码

add_library(ClientLib STATIC
    ${CMAKE_CURRENT_LIST_DIR}/client.cpp
)

target_include_directories(ClientLib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(ClientLib PUBLIC CoreLib RendererLib)

message(STATUS "[Client] Building from: ${CMAKE_CURRENT_LIST_DIR}")
