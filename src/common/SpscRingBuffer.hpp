#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace gv {

/// Lock-free single-producer / single-consumer ring buffer of trivially copyable values.
/// One thread calls Write, one (other) thread calls Read. When full, Write drops what doesn't fit.
template<typename T>
class SpscRingBuffer
{
public:
    explicit SpscRingBuffer(size_t capacity)
        : m_buffer(capacity + 1)
    {
    }

    /// Returns how many values were written.
    auto Write(const T* data, size_t count) -> size_t
    {
        const size_t head = m_head.load(std::memory_order_relaxed);
        const size_t tail = m_tail.load(std::memory_order_acquire);
        const size_t size = m_buffer.size();
        const size_t freeSpace = (tail + size - head - 1) % size;
        const size_t toWrite = std::min(count, freeSpace);

        const size_t first = std::min(toWrite, size - head);
        std::copy(data, data + first, m_buffer.begin() + static_cast<std::ptrdiff_t>(head));
        std::copy(data + first, data + toWrite, m_buffer.begin());

        m_head.store((head + toWrite) % size, std::memory_order_release);
        return toWrite;
    }

    /// Returns how many values were read into `out` (at most `maxCount`).
    auto Read(T* out, size_t maxCount) -> size_t
    {
        const size_t tail = m_tail.load(std::memory_order_relaxed);
        const size_t head = m_head.load(std::memory_order_acquire);
        const size_t size = m_buffer.size();
        const size_t available = (head + size - tail) % size;
        const size_t toRead = std::min(maxCount, available);

        const size_t first = std::min(toRead, size - tail);
        std::copy(m_buffer.begin() + static_cast<std::ptrdiff_t>(tail), m_buffer.begin() + static_cast<std::ptrdiff_t>(tail + first), out);
        std::copy(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(toRead - first), out + first);

        m_tail.store((tail + toRead) % size, std::memory_order_release);
        return toRead;
    }

    auto Capacity() const -> size_t
    {
        return m_buffer.size() - 1;
    }

private:
    std::vector<T> m_buffer;
    std::atomic<size_t> m_head{0}; //!< Next write position (producer).
    std::atomic<size_t> m_tail{0}; //!< Next read position (consumer).
};

} // namespace gv
