#include "audio_input_device_windows.hpp"
#include <chrono>
#include <algorithm>

namespace audio {

AudioInputDevice_Windows::AudioInputDevice_Windows() = default;

AudioInputDevice_Windows::~AudioInputDevice_Windows() {
    stop();
}

std::vector<AudioDeviceInfo> AudioInputDevice_Windows::enumerate_windows_devices() {
    std::vector<AudioDeviceInfo> devices;
    
    // Use existing WindowsWasapiCapture enumeration
    auto wasapi_devices = WindowsWasapiCapture::list_input_devices();
    
    for (size_t i = 0; i < wasapi_devices.size(); ++i) {
        AudioDeviceInfo info;
        info.id = wasapi_devices[i].id;
        info.name = wasapi_devices[i].name;
        info.driver = "WASAPI";
        info.default_sample_rate = 48000;  // Windows typically uses 48kHz
        info.max_channels = 2;
        info.is_default = (i == 0);  // First device is usually default
        devices.push_back(info);
    }
    
    return devices;
}

bool AudioInputDevice_Windows::initialize(
    const AudioInputConfig& config,
    AudioCallback audio_callback,
    ErrorCallback error_callback
) {
    config_ = config;
    audio_callback_ = audio_callback;
    error_callback_ = error_callback;
    
    // Start WASAPI capture
    bool ok = false;
    if (config.device_id.empty() || config.device_id == "default") {
        ok = wasapi_capture_.start();
    } else {
        ok = wasapi_capture_.start_with_device(config.device_id);
    }
    
    if (!ok) {
        if (error_callback_) {
            error_callback_("Failed to start WASAPI capture", true);
        }
        return false;
    }
    
    // Store actual configuration from WASAPI
    actual_config_ = config;
    actual_config_.sample_rate = wasapi_capture_.sample_rate();
    actual_config_.channels = wasapi_capture_.channels();
    
    // Cache device info
    device_info_.id = config.device_id.empty() ? "default" : config.device_id;
    device_info_.name = "Windows Microphone";  // Could get actual name from enumeration
    device_info_.driver = "WASAPI";
    device_info_.default_sample_rate = actual_config_.sample_rate;
    device_info_.max_channels = actual_config_.channels;
    device_info_.is_default = (config.device_id.empty() || config.device_id == "default");
    
    return true;
}

bool AudioInputDevice_Windows::start() {
    if (is_capturing_.load()) {
        return true;  // Already capturing
    }
    
    should_stop_.store(false);
    is_capturing_.store(true);
    
    capture_thread_ = std::make_unique<std::thread>(
        &AudioInputDevice_Windows::capture_thread_func, this
    );
    
    return true;
}

void AudioInputDevice_Windows::stop() {
    if (!is_capturing_.load()) {
        return;
    }
    
    should_stop_.store(true);
    
    if (capture_thread_ && capture_thread_->joinable()) {
        capture_thread_->join();
    }
    
    capture_thread_.reset();
    wasapi_capture_.stop();
    is_capturing_.store(false);
}

void AudioInputDevice_Windows::capture_thread_func() {
    while (!should_stop_.load()) {
        // Read chunk from WASAPI (already converted to int16 mono)
        auto chunk = wasapi_capture_.read_chunk();
        
        if (chunk.empty()) {
            // No data yet, sleep briefly
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        
        // Call the audio callback
        if (audio_callback_) {
            audio_callback_(
                chunk.data(), 
                chunk.size(), 
                actual_config_.sample_rate,
                1  // WASAPI capture returns mono
            );
        }
        
        // Brief sleep to avoid tight loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    is_capturing_.store(false);
}

AudioDeviceInfo AudioInputDevice_Windows::get_device_info() const {
    return device_info_;
}

} // namespace audio
