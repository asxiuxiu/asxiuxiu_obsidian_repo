#!/usr/bin/env python3
"""
Source Forest 生成器
模拟引擎的 chaos_launch_generator.exe

功能：
1. 解析 project_tree_config.json（构建配置）
2. 读取各个仓库的 cmake_projects.json（模块配置）
3. 在 build/sourcetree/ 生成代理 CMakeLists.txt
4. 生成最终的主入口 main.cpp

设计思路：
- 该脚本不直接改写各仓库真实源码，而是在 `build/sourcetree` 下生成一棵“代理源码树”。
- 每个代理目录只放一个 CMakeLists.txt，通过 include() 转发到真实模块目录。
- 最终 CMake 只面向代理树构建，从而实现“按配置拼装模块”的效果。
"""

import json
import os
import sys
import argparse
from pathlib import Path


def parse_args():
    """解析命令行参数。

    参数约定：
    - --config: 必填，顶层配置（project_tree_config.json）。
    - --output: 可选，代理 sourcetree 输出目录。
    - --source-root: 可选，源码仓库根目录。

    返回：
    - argparse.Namespace
    """
    parser = argparse.ArgumentParser(description='Source Forest Generator')
    parser.add_argument('--config', required=True, help='Path to project_tree_config.json')
    parser.add_argument('--output', default='build/sourcetree', help='Output directory for sourcetree')
    parser.add_argument('--source-root', default='.', help='Root directory containing source repos')
    return parser.parse_args()


def load_json(path):
    """加载 JSON 文件。

    这里统一使用 UTF-8，避免 Windows/跨平台环境下的编码差异。
    """
    with open(path, 'r', encoding='utf-8') as f:
        return json.load(f)


def generate_proxy_cmake(proxy_path, real_source_path):
    """生成单个模块的代理 CMakeLists.txt。

    参数：
    - proxy_path: 代理 CMakeLists.txt 输出路径。
    - real_source_path: 真实模块源码目录（绝对路径）。

    生成结果的核心逻辑是：
    1. 设置 REAL_SOURCE_PATH 变量。
    2. include(REAL_SOURCE_PATH/CMakeLists.txt) 让 CMake 转发到真实模块。
    """
    # 确保路径使用正斜杠（CMake 兼容）
    cmake_path = str(real_source_path).replace('\\', '/')
    proxy_content = f"""# 代理 CMakeLists.txt
# 这是"传菜窗口"：包含真实源码目录的 CMakeLists.txt

set(REAL_SOURCE_PATH "{cmake_path}")
message(STATUS "[Proxy] Including: ${{REAL_SOURCE_PATH}}")

# 包含真实源码的构建配置
include(${{REAL_SOURCE_PATH}}/CMakeLists.txt)
"""
    
    proxy_path.parent.mkdir(parents=True, exist_ok=True)
    with open(proxy_path, 'w', encoding='utf-8') as f:
        f.write(proxy_content)


def generate_main_cpp(sourcetree_dir, solution_name, enabled_modules):
    """基于模板生成最终入口 main.cpp。

    参数：
    - sourcetree_dir: 代理树根目录。
    - solution_name: 方案名，用于替换模板占位符。
    - enabled_modules: 已启用模块列表（由 generate_sourcetree 收集）。

    说明：
    - 该函数通过模块名称关键字映射到固定初始化函数，属于“约定优于配置”的简化实现。
    - 若后续模块增多，建议把这部分映射抽成可配置表。
    """
    template_path = Path(__file__).parent / 'templates' / 'main.cpp'
    with open(template_path, 'r', encoding='utf-8') as f:
        template = f.read()
    
    # 根据启用的模块生成前向声明
    forward_decls = []
    init_calls = []
    
    # 按模块类型 + 名称关键字，拼接前向声明与初始化调用。
    for module in enabled_modules:
        module_name = module['name']
        module_type = module.get('type', 'unknown')
        
        if module_type == 'engine':
            if 'core' in module_name.lower():
                forward_decls.append("namespace Engine { void InitCore(); }")
                init_calls.append("    Engine::InitCore();")
            elif 'render' in module_name.lower():
                forward_decls.append("namespace Engine { void InitRenderer(); }")
                init_calls.append("    Engine::InitRenderer();")
        elif module_type == 'game':
            if 'client' in module_name.lower():
                forward_decls.append("namespace Game { void InitClient(); }")
                init_calls.append("    Game::InitClient();")
            elif 'server' in module_name.lower():
                forward_decls.append("namespace Game { void InitServer(); }")
                init_calls.append("    Game::InitServer();")
        elif module_type == 'plugin':
            if 'physics' in module_name.lower():
                forward_decls.append("namespace Plugin { void InitPhysics(); }")
                init_calls.append("    Plugin::InitPhysics();")
    
    # 去重并保持顺序：dict.fromkeys() 在 Python 3.7+ 保序。
    forward_decls = list(dict.fromkeys(forward_decls))
    
    main_cpp = template.replace('@SOLUTION_NAME@', solution_name)
    main_cpp = main_cpp.replace('@FORWARD_DECLS@', '\n'.join(forward_decls))
    main_cpp = main_cpp.replace('@INIT_CALLS@', '\n'.join(init_calls))
    
    with open(sourcetree_dir / 'main.cpp', 'w', encoding='utf-8') as f:
        f.write(main_cpp)


def generate_modules_cmake(sourcetree_dir, enabled_modules):
    """生成 generated/modules.cmake。

    输出形如：
    set(ENABLED_MODULES
        _engine/source/core
        _game/source/client
        ...
    )

    供根 CMake 或工具脚本消费，实现“模块集合”的统一来源。
    """
    modules_file = sourcetree_dir / 'generated' / 'modules.cmake'
    modules_file.parent.mkdir(parents=True, exist_ok=True)
    
    content = "# Auto-generated module list\n"
    content += "set(ENABLED_MODULES\n"
    
    for module in enabled_modules:
        # CMake 路径统一使用 '/'，避免平台分隔符差异。
        module_dir = module['proxy_dir'].replace(os.sep, '/')
        content += f"    {module_dir}\n"
    
    content += ")\n"
    
    with open(modules_file, 'w', encoding='utf-8') as f:
        f.write(content)


def generate_sourcetree(config_path, output_dir, source_root):
    """执行完整 sourcetree 生成流程。

    输入：
    - config_path: 顶层项目树配置。
    - output_dir: 生成目录（通常为 build/sourcetree）。
    - source_root: 仓库根目录。

    步骤概览：
    1. 读取顶层配置，获取 solution_name 与 source_tree。
    2. 遍历各仓库 cmake_projects.json，筛选 enabled 模块。
    3. 为每个启用模块写代理 CMakeLists.txt。
    4. 生成根 CMakeLists.txt / generated/modules.cmake / main.cpp。
    """
    
    print(f"[Generator] Loading config: {config_path}")
    config = load_json(config_path)
    
    solution_name = config['solution_name']
    source_tree = config['source_tree']
    
    print(f"[Generator] Solution: {solution_name}")
    print(f"[Generator] Repositories: {len(source_tree)}")
    
    sourcetree_dir = Path(output_dir)
    sourcetree_dir.mkdir(parents=True, exist_ok=True)
    
    # 创建生成目录骨架；存在时不报错。
    (sourcetree_dir / 'generated').mkdir(exist_ok=True)
    (sourcetree_dir / 'cmake').mkdir(exist_ok=True)
    
    # 复制通用 CMake 工具模块（当前主要是 JSON 解析辅助）。
    template_dir = Path(__file__).parent / 'templates'
    import shutil
    shutil.copy(template_dir / 'cmake' / 'json_parser.cmake', 
                sourcetree_dir / 'cmake' / 'json_parser.cmake')
    
    enabled_modules = []
    
    # 处理每个仓库
    for repo_config_path in source_tree:
        full_repo_config = Path(source_root) / repo_config_path
        print(f"[Generator] Processing repo: {full_repo_config}")
        
        if not full_repo_config.exists():
            print(f"[Warning] Config not found: {full_repo_config}")
            continue
        
        repo_config = load_json(full_repo_config)
        repo_name = repo_config['name']
        repo_root = repo_config['root_path']
        modules = repo_config['modules']
        
        # 通过仓库名做轻量分类，用于后续 main.cpp 生成时的初始化函数映射。
        repo_type = 'unknown'
        if 'engine' in repo_name.lower():
            repo_type = 'engine'
        elif 'game' in repo_name.lower():
            repo_type = 'game'
        elif 'plugin' in repo_name.lower():
            repo_type = 'plugin'
        
        # 处理每个模块
        for module in modules:
            if not module.get('enabled', True):
                print(f"  [Skip] {module['name']} (disabled)")
                continue
            
            module_name = module['name']
            module_path = module['path']
            
            # 真实源码路径（绝对路径），避免 CMake include 时受当前工作目录影响。
            real_source_path = (Path(source_root) / repo_root / module_path).resolve()
            
            # 代理文件路径（在 sourcetree 中镜像 repo/module 层级）。
            proxy_dir = sourcetree_dir / repo_root / module_path
            proxy_cmake = proxy_dir / 'CMakeLists.txt'
            
            print(f"  [Generate] {repo_name}/{module_name} -> {proxy_cmake}")
            
            # 生成代理 CMakeLists.txt
            generate_proxy_cmake(proxy_cmake, real_source_path)
            
            # 收集启用模块的元信息，供后续根 CMake/main.cpp/modules.cmake 统一生成。
            enabled_modules.append({
                'name': module_name,
                'type': repo_type,
                'proxy_dir': str(proxy_dir.relative_to(sourcetree_dir)),
                'real_path': str(real_source_path)
            })
    
    # 生成主 CMakeLists.txt（从模板）
    print("[Generator] Creating root CMakeLists.txt")
    with open(template_dir / 'CMakeLists.txt', 'r', encoding='utf-8') as f:
        root_cmake_template = f.read()
    
    root_cmake = root_cmake_template.replace('@SOLUTION_NAME@', solution_name)
    root_cmake = root_cmake.replace('@CONFIG_PATH@', str(Path(config_path).resolve()).replace('\\', '/'))
    root_cmake = root_cmake.replace('@CMAKE_CURRENT_SOURCE_DIR@', str(sourcetree_dir.resolve()).replace('\\', '/'))
    
    # 根据启用模块推导链接库（这里同样是关键字映射的简化实现）。
    link_libs = []
    for m in enabled_modules:
        if 'core' in m['name'].lower():
            link_libs.append('CoreLib')
        elif 'render' in m['name'].lower():
            link_libs.append('RendererLib')
        elif 'client' in m['name'].lower():
            link_libs.append('ClientLib')
        elif 'physics' in m['name'].lower():
            link_libs.append('PhysicsLib')
    
    # 写入模板占位符时按 CMake target_link_libraries 的缩进格式拼接。
    link_libs_str = '\n'.join([f'    {lib}' for lib in link_libs])
    root_cmake = root_cmake.replace('@LINK_LIBS@', link_libs_str)
    
    with open(sourcetree_dir / 'CMakeLists.txt', 'w', encoding='utf-8') as f:
        f.write(root_cmake)
    
    # 生成 modules.cmake（供 json_parser.cmake 使用）
    generate_modules_cmake(sourcetree_dir, enabled_modules)
    
    # 生成 main.cpp
    generate_main_cpp(sourcetree_dir, solution_name, enabled_modules)
    
    print(f"\n[Generator] Successfully generated {len(enabled_modules)} modules")
    print(f"[Generator] Output: {sourcetree_dir.absolute()}")
    print(f"[Generator] Next step: cmake -S {output_dir} -B build")


def main():
    """命令行入口。

    这里会把相对路径参数统一转换为相对于脚本上一级目录的绝对路径，
    以保证从不同工作目录调用时行为一致。
    """
    args = parse_args()
    
    # 获取脚本所在目录（build-system-demo/）
    script_dir = Path(__file__).parent.parent
    
    config_path = Path(args.config)
    if not config_path.is_absolute():
        config_path = script_dir / config_path
    
    source_root = Path(args.source_root)
    if not source_root.is_absolute():
        source_root = script_dir / source_root
    
    output_path = Path(args.output)
    if not output_path.is_absolute():
        output_path = script_dir / output_path
    
    generate_sourcetree(config_path, output_path, source_root)


if __name__ == '__main__':
    main()
            