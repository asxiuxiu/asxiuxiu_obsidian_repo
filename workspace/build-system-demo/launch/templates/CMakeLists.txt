cmake_minimum_required(VERSION 3.20)
project(@SOLUTION_NAME@ LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Unified output directories (must be set before add_subdirectory)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# For multi-config generators (e.g., Visual Studio)
if(CMAKE_CONFIGURATION_TYPES)
    foreach(CONFIG_TYPE ${CMAKE_CONFIGURATION_TYPES})
        string(TOUPPER ${CONFIG_TYPE} CONFIG_TYPE_UPPER)
        set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/lib)
        set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/lib)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_TYPE_UPPER} ${CMAKE_BINARY_DIR}/bin)
    endforeach()
endif()

# Include JSON parser module
include(@CMAKE_CURRENT_SOURCE_DIR@/cmake/json_parser.cmake)

# Parse Source Forest config
ParseSourceForest("@CONFIG_PATH@")

# Add all enabled modules
foreach(module ${ENABLED_MODULES})
    message(STATUS "Adding module: ${module}")
    add_subdirectory(${module})
endforeach()

# Create final executable
add_executable(${PROJECT_NAME} 
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

target_link_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_BINARY_DIR}/lib
)

target_link_libraries(${PROJECT_NAME} PRIVATE
@LINK_LIBS@
)
