// SPSC 无锁队列
// 对应笔记：无锁队列——综合运用

#pragma once
#include <atomic>
#include <vector>
#include <cassert>
#include <new>

template<typename T>
class SPSCQueue {
    struct Slot {
        T data;
        std::atomic<bool> ready{false};
    };
    
    std::unique_ptr<Slot[]> buffer;
    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
    size_t capacity;
    size_t mask;

public:
    explicit SPSCQueue(size_t cap) : capacity(cap) {
        assert((cap & (cap - 1)) == 0 && "Capacity must be power of 2");
        mask = cap - 1;
        buffer = std::make_unique<Slot[]>(cap);
    }

    bool enqueue(const T& item) {
        size_t h = head.load(std::memory_order_relaxed);
        size_t next = (h + 1) & mask;
        if (next == tail.load(std::memory_order_acquire)) {
            return false;
        }
        buffer[h].data = item;
        buffer[h].ready.store(true, std::memory_order_release);
        head.store(next, std::memory_order_release);
        return true;
    }

    bool dequeue(T& item) {
        size_t t = tail.load(std::memory_order_relaxed);
        if (t == head.load(std::memory_order_acquire)) {
            return false;
        }
        if (!buffer[t].ready.load(std::memory_order_acquire)) {
            return false;
        }
        item = buffer[t].data;
        buffer[t].ready.store(false, std::memory_order_release);
        tail.store((t + 1) & mask, std::memory_order_release);
        return true;
    }
};
