#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace schuimm {

// Fixed-capacity single-producer/single-consumer queue without locks or allocation: one thread
// only pushes, one other thread only pops. Used to pass commands to the real-time audio thread.
template <typename T, std::size_t Capacity>
class SpscQueue {
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");
    static_assert(std::atomic<std::size_t>::is_always_lock_free, "the queue must be lock-free");

public:
    // Producer. Returns false (and drops nothing) if the queue is full.
    bool push(const T& value) noexcept
    {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail - head_.load(std::memory_order_acquire) == Capacity) {
            return false;
        }
        items_[tail & (Capacity - 1)] = value;
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Consumer. Returns false if the queue is empty.
    bool pop(T& value) noexcept
    {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        value = items_[head & (Capacity - 1)];
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    std::array<T, Capacity> items_{};
    std::atomic<std::size_t> head_{0};
    std::atomic<std::size_t> tail_{0};
};

} // namespace schuimm
