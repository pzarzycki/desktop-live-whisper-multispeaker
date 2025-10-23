#include "coreaudio_output.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <iostream>
#include <cstring>
#include <queue>
#include <mutex>

namespace audio {

// Internal state for managing audio queue
struct OutputState {
    std::queue<std::vector<int16_t>> pending_buffers;
    std::mutex mutex;
    AudioQueueRef queue = nullptr;
    AudioQueueBufferRef buffers[3] = {nullptr, nullptr, nullptr};
    int channels = 1;
    bool stopping = false;
};

// Static callback - must match AudioQueueOutputCallback signature
static void output_callback(
    void* user_data,
    AudioQueueRef audio_queue,
    AudioQueueBufferRef buffer
) {
    auto* state = static_cast<OutputState*>(user_data);
    
    if (state->stopping) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(state->mutex);
    
    // Fill buffer with pending data or silence
    if (!state->pending_buffers.empty()) {
        auto& pending = state->pending_buffers.front();
        
        // Copy data to buffer
        size_t bytes_to_copy = std::min(
            pending.size() * sizeof(int16_t),
            (size_t)buffer->mAudioDataBytesCapacity
        );
        
        memcpy(buffer->mAudioData, pending.data(), bytes_to_copy);
        buffer->mAudioDataByteSize = bytes_to_copy;
        
        state->pending_buffers.pop();
    } else {
        // No data available - output silence
        memset(buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity);
        buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
    }
    
    // Re-enqueue buffer
    AudioQueueEnqueueBuffer(audio_queue, buffer, 0, nullptr);
}

CoreAudioOutput::~CoreAudioOutput() {
    stop();
}

bool CoreAudioOutput::start(int sample_rate, int channels) {
    if (running_) {
        std::cerr << "[CoreAudioOutput] Already running\n";
        return false;
    }
    
    sample_rate_ = sample_rate;
    channels_ = channels;
    
    // Create output state
    auto* state = new OutputState();
    state->channels = channels;
    audio_queue_ = state;
    
    // Set up audio format (Linear PCM, 16-bit signed integer)
    AudioStreamBasicDescription format = {0};
    format.mSampleRate = sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mBitsPerChannel = 16;
    format.mChannelsPerFrame = channels;
    format.mBytesPerFrame = channels * 2;  // 2 bytes per sample
    format.mFramesPerPacket = 1;
    format.mBytesPerPacket = format.mBytesPerFrame;
    
    // Create audio queue for output
    OSStatus status = AudioQueueNewOutput(
        &format,
        output_callback,
        state,  // User data
        nullptr,  // Run loop (use default)
        kCFRunLoopCommonModes,
        0,  // Flags
        &state->queue
    );
    
    if (status != noErr) {
        std::cerr << "[CoreAudioOutput] Failed to create output queue: " << status << "\n";
        delete state;
        audio_queue_ = nullptr;
        return false;
    }
    
    // Allocate buffers (100ms each)
    const int buffer_size_frames = sample_rate / 10;  // 100ms
    const int buffer_size_bytes = buffer_size_frames * channels * 2;
    
    for (int i = 0; i < 3; ++i) {
        status = AudioQueueAllocateBuffer(state->queue, buffer_size_bytes, &state->buffers[i]);
        if (status != noErr) {
            std::cerr << "[CoreAudioOutput] Failed to allocate buffer " << i << ": " << status << "\n";
            stop();
            return false;
        }
        
        // Prime buffers with silence and enqueue
        memset(state->buffers[i]->mAudioData, 0, buffer_size_bytes);
        state->buffers[i]->mAudioDataByteSize = buffer_size_bytes;
        AudioQueueEnqueueBuffer(state->queue, state->buffers[i], 0, nullptr);
    }
    
    // Start playback
    status = AudioQueueStart(state->queue, nullptr);
    if (status != noErr) {
        std::cerr << "[CoreAudioOutput] Failed to start queue: " << status << "\n";
        stop();
        return false;
    }
    
    running_ = true;
    return true;
}

void CoreAudioOutput::stop() {
    if (!running_ || !audio_queue_) {
        return;
    }
    
    auto* state = static_cast<OutputState*>(audio_queue_);
    state->stopping = true;
    
    // Stop and dispose audio queue
    if (state->queue) {
        AudioQueueStop(state->queue, true);  // Immediate stop
        AudioQueueDispose(state->queue, true);
        state->queue = nullptr;
    }
    
    delete state;
    audio_queue_ = nullptr;
    running_ = false;
}

void CoreAudioOutput::write(const short* data, unsigned long long frames) {
    if (!running_ || !audio_queue_) {
        return;
    }
    
    auto* state = static_cast<OutputState*>(audio_queue_);
    
    // Store data in pending queue
    std::lock_guard<std::mutex> lock(state->mutex);
    
    // Convert to vector for queuing
    std::vector<int16_t> buffer(data, data + frames * channels_);
    state->pending_buffers.push(std::move(buffer));
}

}  // namespace audio
