# Physics Plugin Module - 真实源码

add_library(PhysicsLib STATIC
    ${CMAKE_CURRENT_LIST_DIR}/physics.cpp
)

target_include_directories(PhysicsLib PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(PhysicsLib PUBLIC CoreLib)

message(STATUS "[Physics] Building from: ${CMAKE_CURRENT_LIST_DIR}")
