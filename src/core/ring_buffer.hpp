#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <atomic>

// A simple single-producer single-consumer lock-free ring buffer for int16_t frames.
class RingBufferI16 {
public:
    explicit RingBufferI16(size_t capacity)
        : buffer_(capacity), capacity_(capacity), head_(0), tail_(0) {}

    size_t capacity() const { return capacity_; }

    // Push up to n samples, returns samples actually written.
    size_t push(const int16_t* data, size_t n) {
        size_t written = 0;
        while (written < n) {
            size_t head = head_.load(std::memory_order_relaxed);
            size_t tail = tail_.load(std::memory_order_acquire);
            size_t free_space = capacity_ - (head - tail);
            if (free_space == 0) break;
            size_t to_write = (n - written < free_space) ? (n - written) : free_space;
            for (size_t i = 0; i < to_write; ++i) {
                buffer_[(head + i) % capacity_] = data[written + i];
            }
            head_.store(head + to_write, std::memory_order_release);
            written += to_write;
        }
        return written;
    }

    // Pop up to n samples, returns samples actually read.
    size_t pop(int16_t* out, size_t n) {
        size_t read = 0;
        while (read < n) {
            size_t tail = tail_.load(std::memory_order_relaxed);
            size_t head = head_.load(std::memory_order_acquire);
            size_t available = head - tail;
            if (available == 0) break;
            size_t to_read = (n - read < available) ? (n - read) : available;
            for (size_t i = 0; i < to_read; ++i) {
                out[read + i] = buffer_[(tail + i) % capacity_];
            }
            tail_.store(tail + to_read, std::memory_order_release);
            read += to_read;
        }
        return read;
    }

    size_t size() const {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        return head - tail;
    }

private:
    std::vector<int16_t> buffer_;
    const size_t capacity_;
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
};
