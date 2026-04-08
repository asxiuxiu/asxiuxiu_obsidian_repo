# Game Server Module - 真实源码（通常用于专用服务器）

add_library(ServerLib STATIC
    ${CMAKE_CURRENT_LIST_DIR}/server.cpp
)

target_include_directories(ServerLib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(ServerLib PUBLIC CoreLib)

message(STATUS "[Server] Building from: ${CMAKE_CURRENT_LIST_DIR}")
