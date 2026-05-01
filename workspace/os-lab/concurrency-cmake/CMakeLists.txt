cmake_minimum_required(VERSION 3.16)
project(concurrency-lab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 平台检测
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_compile_definitions(PLATFORM_LINUX)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_compile_definitions(PLATFORM_MACOS)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_compile_definitions(PLATFORM_WINDOWS)
endif()

# 公共编译选项
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-O0 -pthread)
    add_link_options(-pthread)
endif()

# 01: 进程隔离 vs 线程共享
add_executable(01_process_vs_thread 01_process_vs_thread/main.cpp)

# 02: 第一个多线程程序
add_executable(02_first_thread 02_first_thread/main.cpp)

# 03: 数据竞争演示
add_executable(03_data_race 03_data_race/main.cpp)

# 04: 原子操作
add_executable(04_atomic_basics 04_atomic/main.cpp)

# 05: 互斥锁
add_executable(05_mutex_queue 05_mutex/main_mutex_queue.cpp)
add_executable(05_deadlock_demo 05_mutex/main_deadlock.cpp)

# 06: 自旋锁 vs 互斥锁
add_executable(06_mutex_vs_spinlock 06_spinlock/main.cpp)

# 07: 条件变量
add_executable(07_condition_variable 07_condition_variable/main_cv.cpp)
add_executable(07_cv_vs_busywait 07_condition_variable/main_cv_vs_busywait.cpp)

# 08: 内存序
add_executable(08_acquire_release 08_memory_order/main_acquire_release.cpp)
add_executable(08_fence_bench 08_memory_order/main_fence_bench.cpp)

# 09: Release-Acquire 实战
add_executable(09_ra_correctness 09_release_acquire/main_ra_correctness.cpp)
add_executable(09_ra_gamestate 09_release_acquire/main_ra_gamestate.cpp)

# 10: 无锁队列
add_executable(10_spsc_bench 10_lockfree_queue/main.cpp)
target_include_directories(10_spsc_bench PRIVATE 10_lockfree_queue)
