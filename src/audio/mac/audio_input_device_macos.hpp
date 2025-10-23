#pragma once

#include "audio/audio_input_device.hpp"
#include <atomic>
#include <thread>
#include <memory>
#include <AudioToolbox/AudioToolbox.h>

namespace audio {

/**
 * @brief macOS CoreAudio implementation of audio input
 * 
 * Uses AudioQueue API for microphone capture
 */
class AudioInputDevice_macOS : public IAudioInputDevice {
public:
    AudioInputDevice_macOS();
    ~AudioInputDevice_macOS() override;
    
    // Enumerate macOS audio input devices
    static std::vector<AudioDeviceInfo> enumerate_macos_devices();
    
    // IAudioInputDevice interface
    bool initialize(
        const AudioInputConfig& config,
        AudioCallback audio_callback,
        ErrorCallback error_callback
    ) override;
    
    bool start() override;
    void stop() override;
    bool is_capturing() const override { return is_capturing_.load(); }
    AudioDeviceInfo get_device_info() const override { return device_info_; }
    AudioInputConfig get_actual_config() const override { return actual_config_; }
    
private:
    // CoreAudio callback (static, calls instance method)
    static void audio_input_callback(
        void* user_data,
        AudioQueueRef queue,
        AudioQueueBufferRef buffer,
        const AudioTimeStamp* start_time,
        UInt32 num_packets,
        const AudioStreamPacketDescription* packet_desc
    );
    
    // Instance callback handler
    void handle_audio_buffer(AudioQueueBufferRef buffer, UInt32 num_packets);
    
    // CoreAudio objects
    AudioQueueRef audio_queue_ = nullptr;
    static constexpr int kNumBuffers = 3;
    AudioQueueBufferRef audio_buffers_[kNumBuffers] = {nullptr};
    
    // Configuration
    AudioInputConfig config_;
    AudioInputConfig actual_config_;
    AudioDeviceInfo device_info_;
    
    // Callbacks
    AudioCallback audio_callback_;
    ErrorCallback error_callback_;
    
    // State
    std::atomic<bool> is_capturing_{false};
    std::atomic<bool> should_stop_{false};
};

} // namespace audio
