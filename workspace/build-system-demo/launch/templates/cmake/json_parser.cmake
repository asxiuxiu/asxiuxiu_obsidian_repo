# JSON 解析模块
# 负责读取 project_tree_config.json 和各个 cmake_projects.json

set(ENABLED_MODULES "")

function(ParseSourceForest config_path)
    message(STATUS "Parsing Source Forest config: ${config_path}")
    
    # 注意：这里简化处理，实际应该用 CMake 的 JSON 解析或外部工具
    # 在 Demo 中，我们让 Python 生成器预处理 JSON，CMake 只需要包含生成的列表
    
    set(modules_file "${CMAKE_CURRENT_SOURCE_DIR}/generated/modules.cmake")
    message(STATUS "Including modules file: ${modules_file}")
    include("${modules_file}")
    
    # 关键：将变量传递到父作用域
    set(ENABLED_MODULES ${ENABLED_MODULES} PARENT_SCOPE)
endfunction()
