#!/usr/bin/env python3
"""
Source Forest 生成器
模拟引擎的 chaos_launch_generator.exe

功能：
1. 解析 project_tree_config.json（构建配置）
2. 读取各个仓库的 cmake_projects.json（模块配置）
3. 在 build/sourcetree/ 生成代理 CMakeLists.txt
4. 生成最终的主入口 main.cpp
"""

import json
import os
import sys
import argparse
from pathlib import Path


def parse_args():
    parser = argparse.ArgumentParser(description='Source Forest Generator')
    parser.add_argument('--config', required=True, help='Path to project_tree_config.json')
    parser.add_argument('--output', default='build/sourcetree', help='Output directory for sourcetree')
    parser.add_argument('--source-root', default='.', help='Root directory containing source repos')
    return parser.parse_args()


def load_json(path):
    """加载 JSON 文件"""
    with open(path, 'r', encoding='utf-8') as f:
        return json.load(f)


def generate_proxy_cmake(proxy_path, real_source_path):
    """生成代理 CMakeLists.txt"""
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
    """生成主入口文件 main.cpp"""
    template_path = Path(__file__).parent / 'templates' / 'main.cpp'
    with open(template_path, 'r', encoding='utf-8') as f:
        template = f.read()
    
    # 根据启用的模块生成前向声明
    forward_decls = []
    init_calls = []
    
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
    
    forward_decls = list(dict.fromkeys(forward_decls))  # 去重
    
    main_cpp = template.replace('@SOLUTION_NAME@', solution_name)
    main_cpp = main_cpp.replace('@FORWARD_DECLS@', '\n'.join(forward_decls))
    main_cpp = main_cpp.replace('@INIT_CALLS@', '\n'.join(init_calls))
    
    with open(sourcetree_dir / 'main.cpp', 'w', encoding='utf-8') as f:
        f.write(main_cpp)


def generate_modules_cmake(sourcetree_dir, enabled_modules):
    """生成模块列表（供 CMake 使用）"""
    modules_file = sourcetree_dir / 'generated' / 'modules.cmake'
    modules_file.parent.mkdir(parents=True, exist_ok=True)
    
    content = "# Auto-generated module list\n"
    content += "set(ENABLED_MODULES\n"
    
    for module in enabled_modules:
        module_dir = module['proxy_dir'].replace(os.sep, '/')
        content += f"    {module_dir}\n"
    
    content += ")\n"
    
    with open(modules_file, 'w', encoding='utf-8') as f:
        f.write(content)


def generate_sourcetree(config_path, output_dir, source_root):
    """主生成流程"""
    
    print(f"[Generator] Loading config: {config_path}")
    config = load_json(config_path)
    
    solution_name = config['solution_name']
    source_tree = config['source_tree']
    
    print(f"[Generator] Solution: {solution_name}")
    print(f"[Generator] Repositories: {len(source_tree)}")
    
    sourcetree_dir = Path(output_dir)
    sourcetree_dir.mkdir(parents=True, exist_ok=True)
    
    # 创建子目录
    (sourcetree_dir / 'generated').mkdir(exist_ok=True)
    (sourcetree_dir / 'cmake').mkdir(exist_ok=True)
    
    # 复制 CMake 模块
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
        
        # 确定仓库类型（简单判断）
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
            
            # 真实源码路径（绝对路径）
            real_source_path = (Path(source_root) / repo_root / module_path).resolve()
            
            # 代理文件路径（在 sourcetree 中）
            proxy_dir = sourcetree_dir / repo_root / module_path
            proxy_cmake = proxy_dir / 'CMakeLists.txt'
            
            print(f"  [Generate] {repo_name}/{module_name} -> {proxy_cmake}")
            
            # 生成代理 CMakeLists.txt
            generate_proxy_cmake(proxy_cmake, real_source_path)
            
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
    
    # 根据启用的模块调整 link libraries
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
    args = parse_args()
    
    # 获取脚本所在目录（build-system-demo/launch/）
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
