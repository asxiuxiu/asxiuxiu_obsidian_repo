cmake_minimum_required(VERSION 3.16)
project(GraphicsDemo LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 统一配置单个 demo 目标：
# - 可执行文件输出到 build/<target>/
# - 向源码注入 DEMO_OUTPUT_DIR 宏，让 PPM 等产物也写到同一目录
function(configure_demo_target TARGET)
  set(_demo_out_dir "${CMAKE_BINARY_DIR}/${TARGET}")
  file(TO_CMAKE_PATH "${_demo_out_dir}" _demo_out_dir)

  set_target_properties(${TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${_demo_out_dir}"
  )

  target_compile_definitions(${TARGET} PRIVATE
    "DEMO_OUTPUT_DIR=\"${_demo_out_dir}\""
  )
endfunction()

add_subdirectory(pixel-demo)
add_subdirectory(rasterization)
