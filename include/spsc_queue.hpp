// include/spsc_queue.hpp
#pragma once
#include <array>
#include <atomic>

template<typename T, size_t N>
class SPSCQueue {
    static_assert((N & (N-1)) == 0, "N must be power of 2");
public:
    bool push(const T& item) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);
        size_t next = (tail + 1) & (N - 1);
        if (next == head_.load(std::memory_order_acquire)) return false;
        buffer_[tail] = item;
        tail_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& item) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) return false;
        item = buffer_[head];
        head_.store((head + 1) & (N - 1), std::memory_order_release);
        return true;
    }

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    std::array<T, N> buffer_;
};
