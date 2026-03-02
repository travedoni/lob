#pragma once
#include <atomic>
#include <array>
#include <optional>
#include <cstddef>

// SPSCQueue<T, N>
// A lock-free Single Producer / Single Consumer ring buffer.
//
// "Lock-free" means no mutex, no kernel calls, no blocking.
// Synchronization is done entirely with std::atomic load/store operations.
//
// N must be a power of 2. Use bitmasking (& (N-1)) instead of
// modulo (% N) for wrapping: division is expensive, AND is one cycle.

template<typename T, std::size_t N>
class SPSCQueue {
    static_assert((N & (N - 1)) == 0, "SPSCQueue capacity must be a power of 2");
    static_assert(N >= 2, "SPSCQueue must me at least 2");

public:
    SPSCQueue() : head_(0), tail_(0) {}

    // Push, producer thread only
    // Returns false if the queue is full (non-blocking)
    bool push(const T& item) {
        const std::size_t tail = tail_.load();
        const std::size_t next = (tail + 1) & kMask;

        if (next == head_.load()) return false;

        slots_[tail] = item;
        tail_.store(next);
        return true;
    }

    // Move-push variant (avoids copy for large T)
    bool push(T&& item) {
        const std::size_t tail = tail_.load();
        const std::size_t next = (tail + 1) & kMask;

        if (next == head_.load()) return false;

        slots_[tail] = std::move(item);
        tail_.store(next);
        return true;
    }

    // Pop, consumer thread only
    // Returns std::nullopt if the queue is empty (non-blocking)
    std::optional<T> pop() {
        const std::size_t head = head_.load();

        if (head == tail_.load()) return std::nullopt;

        T item = std::move(slots_[head]);
        head_.store((head + 1) & kMask);
        return item;
    }

    bool empty() const { return head_.load() == tail_.load(); }
    std::size_t size() const {
        std::size_t t = tail_.load(), h = head_.load();
        return (t - h + N) & kMask;
    }
    std::size_t capacity() const { return N - 1; } // 1 slot reserved

private:
    static constexpr std::size_t kMask = N - 1;
    static constexpr std::size_t kCacheLineSize = 64;

    alignas(kCacheLineSize) std::atomic<std::size_t> head_;
    alignas(kCacheLineSize) std::atomic<std::size_t> tail_;

    std::array<T, N> slots_;
};

