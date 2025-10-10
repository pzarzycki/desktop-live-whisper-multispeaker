#pragma once

#include "../audio_input_device.hpp"
#include "../windows_wasapi.hpp"
#include <thread>
#include <atomic>
#include <mutex>

namespace audio {

/**
 * @brief Windows WASAPI audio input device adapter
 * 
 * Wraps the existing WindowsWasapiCapture implementation into IAudioInputDevice interface
 */
class AudioInputDevice_Windows : public IAudioInputDevice {
public:
    AudioInputDevice_Windows();
    ~AudioInputDevice_Windows() override;
    
    bool initialize(
        const AudioInputConfig& config,
        AudioCallback audio_callback,
        ErrorCallback error_callback
    ) override;
    
    bool start() override;
    void stop() override;
    bool is_capturing() const override { return is_capturing_.load(); }
    AudioDeviceInfo get_device_info() const override;
    AudioInputConfig get_actual_config() const override { return actual_config_; }
    
    // Platform-specific: Enumerate Windows devices
    static std::vector<AudioDeviceInfo> enumerate_windows_devices();
    
private:
    void capture_thread_func();
    
    AudioInputConfig config_;
    AudioInputConfig actual_config_;
    AudioCallback audio_callback_;
    ErrorCallback error_callback_;
    
    // Use existing WASAPI implementation
    WindowsWasapiCapture wasapi_capture_;
    
    // Threading
    std::unique_ptr<std::thread> capture_thread_;
    std::atomic<bool> is_capturing_{false};
    std::atomic<bool> should_stop_{false};
    
    // Device info cache
    AudioDeviceInfo device_info_;
};

} // namespace audio
