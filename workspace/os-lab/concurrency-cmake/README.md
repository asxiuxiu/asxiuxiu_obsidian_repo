# 并发与多线程实验工程

对应笔记系列：`Notes/操作系统/并发与多线程/`

## 快速开始

```bash
cd workspace/os-lab/concurrency-cmake
mkdir build && cd build
cmake ..
cmake --build . -j$(sysctl -n hw.ncpu)   # macOS
# cmake --build . -j$(nproc)              # Linux
```

## 实验列表

| 可执行文件 | 对应笔记 | 说明 |
|-----------|---------|------|
| `01_process_vs_thread` | 笔记1 | 进程隔离 vs 线程共享（Linux 上 fork() 有输出） |
| `02_first_thread` | 笔记2 | 创建线程、join/detach、参数传递 |
| `03_data_race` | 笔记3 | 演示数据竞争：计数器小于预期 |
| `04_atomic_basics` | 笔记4 | Atomic 计数器 vs 非 Atomic、CAS 实现递增 |
| `05_mutex_queue` | 笔记5 | Mutex 保护生产者-消费者队列 |
| `05_deadlock_demo` | 笔记5 | **会卡死！** 演示死锁，按 Ctrl+C 终止 |
| `06_mutex_vs_spinlock` | 笔记6 | 短临界区下 Mutex vs Spinlock 性能对比 |
| `07_condition_variable` | 笔记7 | 条件变量实现生产者-消费者 |
| `07_cv_vs_busywait` | 笔记7 | 条件变量 vs 忙等的 CPU 占用差异 |
| `08_acquire_release` | 笔记8 | 验证 Release-Acquire 的跨线程可见性 |
| `08_fence_bench` | 笔记8 | Memory Fence 性能差异（relaxed/acq_rel/seq_cst） |
| `09_ra_correctness` | 笔记9 | Release-Acquire 正确性验证（10万次循环） |
| `09_ra_gamestate` | 笔记9 | 游戏状态版本号发布演示 |
| `10_spsc_bench` | 笔记10 | SPSC 无锁队列 vs Mutex 队列性能对比 |

## 运行示例

```bash
# 数据竞争演示
./03_data_race

# 原子操作正确性验证
./04_atomic_basics

# 性能对比
./06_mutex_vs_spinlock
./10_spsc_bench

# 条件变量（带两个消费者）
./07_condition_variable
```

## 工程结构

```
concurrency-cmake/
├── CMakeLists.txt          # 主构建配置
├── README.md               # 本文件
├── 01_process_vs_thread/   # 笔记1：进程与线程
├── 02_first_thread/        # 笔记2：std::thread 基础
├── 03_data_race/           # 笔记3：数据竞争
├── 04_atomic/              # 笔记4：std::atomic
├── 05_mutex/               # 笔记5：互斥锁
├── 06_spinlock/            # 笔记6：自旋锁
├── 07_condition_variable/  # 笔记7：条件变量
├── 08_memory_order/        # 笔记8：内存序
├── 09_release_acquire/     # 笔记9：Release-Acquire
└── 10_lockfree_queue/      # 笔记10：无锁队列
    └── spsc_queue.hpp      # SPSC Ring Buffer 实现
```

## 注意事项

- macOS 上 `fork()` 的行为与 Linux 略有不同，笔记1中的进程隔离实验在 macOS 上会跳过。
- `05_deadlock_demo` 会**永久卡住**，用于演示死锁现象，按 `Ctrl+C` 终止。
- `07_cv_vs_busywait` 建议配合 `htop` / 活动监视器观察 CPU 占用差异。
- 所有实验使用 `-O2` 优化编译，以反映真实性能。
