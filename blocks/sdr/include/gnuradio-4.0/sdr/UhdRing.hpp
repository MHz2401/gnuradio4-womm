#ifndef GNURADIO_UHD_RING_HPP
#define GNURADIO_UHD_RING_HPP

#include <atomic>
#include <bit>
#include <cstring>
#include <span>
#include <vector>

namespace gr::blocks::sdr::detail {

/**
 * @brief Single-producer, single-consumer sample ring for one radio channel.
 *
 * The producer is the device IO thread; the consumer is whichever scheduler worker runs
 * processBulk. Indices are monotonic 64-bit counts rather than wrapped offsets, so a full
 * ring and an empty ring are never the same state and no spare slot has to be sacrificed
 * to tell them apart.
 *
 * Capacity is rounded to a power of two so the wrap is a mask rather than a division.
 */
template<typename T>
requires std::is_trivially_copyable_v<T>
struct UhdRing {
    std::vector<T>             buffer{};
    std::size_t                mask = 0UZ;
    std::atomic<std::uint64_t> writeIndex{0U};
    std::atomic<std::uint64_t> readIndex{0U};

    void reset(std::size_t capacity) {
        const std::size_t rounded = std::bit_ceil(capacity < 2UZ ? 2UZ : capacity);
        buffer.assign(rounded, T{});
        mask = rounded - 1UZ;
        writeIndex.store(0U, std::memory_order_relaxed);
        readIndex.store(0U, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return buffer.size(); }

    [[nodiscard]] std::size_t available() const noexcept { //
        return static_cast<std::size_t>(writeIndex.load(std::memory_order_acquire) - readIndex.load(std::memory_order_relaxed));
    }

    [[nodiscard]] std::size_t space() const noexcept { //
        return capacity() - static_cast<std::size_t>(writeIndex.load(std::memory_order_relaxed) - readIndex.load(std::memory_order_acquire));
    }

    // Producer side. The caller must have checked space() >= count.
    void push(const T* source, std::size_t count) noexcept {
        const std::uint64_t head   = writeIndex.load(std::memory_order_relaxed);
        const std::size_t   offset = static_cast<std::size_t>(head) & mask;
        const std::size_t   first  = std::min(count, capacity() - offset);
        std::memcpy(buffer.data() + offset, source, first * sizeof(T));
        if (count > first) {
            std::memcpy(buffer.data(), source + first, (count - first) * sizeof(T));
        }
        writeIndex.store(head + count, std::memory_order_release);
    }

    // Consumer side. The caller must have checked available() >= count.
    void pop(T* destination, std::size_t count) noexcept {
        const std::uint64_t tail   = readIndex.load(std::memory_order_relaxed);
        const std::size_t   offset = static_cast<std::size_t>(tail) & mask;
        const std::size_t   first  = std::min(count, capacity() - offset);
        std::memcpy(destination, buffer.data() + offset, first * sizeof(T));
        if (count > first) {
            std::memcpy(destination + first, buffer.data(), (count - first) * sizeof(T));
        }
        readIndex.store(tail + count, std::memory_order_release);
    }
};

} // namespace gr::blocks::sdr::detail

#endif // GNURADIO_UHD_RING_HPP
