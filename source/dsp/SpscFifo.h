#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace evox::dsp
{

/** Wait-free single-producer / single-consumer ring buffer.
    Producer: audio thread. Consumer: UI thread. Neither side ever blocks; when the
    FIFO is full the producer drops the newest item (the UI simply misses a frame). */
template <typename T, std::size_t Capacity>
class SpscFifo
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    bool push (const T& item) noexcept
    {
        const auto w = write_.load (std::memory_order_relaxed);
        if (w - read_.load (std::memory_order_acquire) >= Capacity)
            return false;
        items_[w & (Capacity - 1)] = item;
        write_.store (w + 1, std::memory_order_release);
        return true;
    }

    bool pop (T& item) noexcept
    {
        const auto r = read_.load (std::memory_order_relaxed);
        if (r == write_.load (std::memory_order_acquire))
            return false;
        item = items_[r & (Capacity - 1)];
        read_.store (r + 1, std::memory_order_release);
        return true;
    }

    std::size_t size() const noexcept
    {
        return write_.load (std::memory_order_acquire) - read_.load (std::memory_order_acquire);
    }

private:
    std::array<T, Capacity> items_ {};
    alignas (64) std::atomic<std::size_t> write_ { 0 };
    alignas (64) std::atomic<std::size_t> read_ { 0 };
};

} // namespace evox::dsp
