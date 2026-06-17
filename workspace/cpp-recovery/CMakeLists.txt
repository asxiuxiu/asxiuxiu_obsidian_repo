cmake_minimum_required(VERSION 3.20)
project(cpp-recovery LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# AddressSanitizer 开关
#   MSVC:    cmake -S . -B build -DENABLE_ASAN=ON
#   GCC/Clang: cmake -S . -B build -DENABLE_ASAN=ON
#   MinGW: 暂不支持（缺少 libasan 运行时库）
# ---------------------------------------------------------------------------
option(ENABLE_ASAN "Enable AddressSanitizer (MSVC/GCC/Clang only)" OFF)

if(ENABLE_ASAN)
    if(MSVC)
        add_compile_options(/fsanitize:address)
        # MSVC ASan 要求 /INCREMENTAL:NO，否则链接失败
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /INCREMENTAL:NO")
        message(STATUS "ASan enabled for MSVC")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # MinGW GCC 有前端支持但缺少 libasan，需要额外检查
        if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            message(WARNING "MinGW GCC on Windows lacks libasan runtime. "
                            "ASan will fail at link time. "
                            "Use MSVC or Clang on Windows instead.")
        endif()
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
        # ASan 推荐用 -O0 或 -O1，-O2 下报错信息行号可能偏移
        if(CMAKE_BUILD_TYPE STREQUAL "")
            message(STATUS "ASan enabled for GCC/Clang (optimize lowered to -O1)")
        endif()
    else()
        message(WARNING "ASan not supported for compiler: ${CMAKE_CXX_COMPILER_ID}")
    endif()
else()
    # 默认编译选项
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-Wall -Wextra -Wshadow -O2 -g)
    elseif(MSVC)
        add_compile_options(/W4 /utf-8)
    endif()
endif()

include_directories(include)

# ---------------------------------------------------------------------------
# 自动发现阶段练习目录
# 目录命名规范：phase<阶段号>/ex<练习编号>_<主题>/
# 例如：phase01/ex01_file_guard/
# ---------------------------------------------------------------------------
file(GLOB phase_dirs RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} phase*)
foreach(phase_dir ${phase_dirs})
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${phase_dir})
        file(GLOB exercise_dirs RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/${phase_dir} ${phase_dir}/ex*)
        foreach(exercise_dir ${exercise_dirs})
            if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${exercise_dir})
                add_subdirectory(${exercise_dir})
            endif()
        endforeach()
    endif()
endforeach()

# 保留旧 week01 目录兼容（迁移完成后可删除）
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/week01/CMakeLists.txt)
    add_subdirectory(week01)
endif()
