#pragma once

#include "audio_input_device.hpp"
#include "file_capture.hpp"
#include "windows_wasapi_out.hpp"
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>

namespace audio {

/**
 * @brief Synthetic audio device that reads from a file and simulates microphone input
 * 
 * Features:
 * - Uses existing FileCapture implementation
 * - Reads WAV file in real-time chunks (simulates microphone latency)
 * - Optionally plays audio to speakers so user can hear what's being transcribed
 * - Can loop for continuous testing
 * - Useful for testing and demos
 */
class AudioInputDevice_Synthetic : public IAudioInputDevice {
public:
    AudioInputDevice_Synthetic();
    ~AudioInputDevice_Synthetic() override;
    
    bool initialize(
        const AudioInputConfig& config,
        AudioCallback audio_callback,
        ErrorCallback error_callback
    ) override;
    
    bool start() override;
    void stop() override;
    bool is_capturing() const override { return is_capturing_.load(); }
    AudioDeviceInfo get_device_info() const override;
    AudioInputConfig get_actual_config() const override { return config_; }
    
private:
    void capture_thread_func();
    
    AudioInputConfig config_;
    AudioCallback audio_callback_;
    ErrorCallback error_callback_;
    
    // Use existing file capture implementation
    FileCapture file_capture_;
    
    // Optional playback
    WindowsWasapiOut playback_out_;
    bool playback_enabled_ = false;
    
    // Threading
    std::unique_ptr<std::thread> capture_thread_;
    std::atomic<bool> is_capturing_{false};
    std::atomic<bool> should_stop_{false};
};

} // namespace audio
