#pragma once
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <atomic>

namespace audio {

// Thread-safe queue for passing audio chunks between playback and processing threads
// Design: Audio source (microphone/file playback) never blocks - it always succeeds.
//         If processing can't keep up, it skips chunks to catch up with real-time.
//         This simulates real hardware: microphone keeps producing, processing may fall behind.
class AudioQueue {
public:
    struct Chunk {
        std::vector<int16_t> samples;
        int sample_rate;
    };

    AudioQueue(size_t max_size = 50) : max_size_(max_size), stopped_(false) {}

    // Push a chunk (called by audio source - microphone or file player)
    // Never blocks - always succeeds to keep audio flowing smoothly
    // If queue is full, oldest chunks will be skipped by processing thread
    bool push(Chunk&& chunk) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (stopped_) {
            return false;
        }
        
        queue_.push(std::move(chunk));
        cv_pop_.notify_one();
        return true;
    }

    // Pop a chunk (called by processing thread)
    // If queue has grown too large (processing fell behind), skip chunks to catch up
    bool pop(Chunk& chunk) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_pop_.wait(lock, [this] { return !queue_.empty() || stopped_; });
        if (stopped_ && queue_.empty()) {
            return false;
        }
        
        // If queue is getting full, processing has fallen behind
        // Skip chunks to catch up (drop oldest, keep newest)
        while (queue_.size() > max_size_) {
            queue_.pop();
            dropped_count_++;
        }
        
        chunk = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    // Signal stop (no more chunks will be added)
    void stop() {
        std::unique_lock<std::mutex> lock(mutex_);
        stopped_ = true;
        cv_pop_.notify_all();
    }

    size_t size() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return queue_.size();
    }

    size_t dropped_count() const {
        return dropped_count_.load();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_pop_;   // Notifies pop() when data available
    std::queue<Chunk> queue_;
    size_t max_size_;
    std::atomic<bool> stopped_;
    std::atomic<size_t> dropped_count_{0};
};

} // namespace audio
