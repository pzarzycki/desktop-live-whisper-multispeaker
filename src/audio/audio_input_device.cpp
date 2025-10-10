#include "audio_input_device.hpp"
#include "audio_input_device_synthetic.hpp"

#ifdef _WIN32
#include "win/audio_input_device_windows.hpp"
#elif __APPLE__
// #include "mac/audio_input_device_macos.hpp"  // TODO
#endif

namespace audio {

std::vector<AudioDeviceInfo> AudioInputFactory::enumerate_devices() {
    std::vector<AudioDeviceInfo> devices;
    
#ifdef _WIN32
    // Add Windows WASAPI devices
    auto windows_devices = AudioInputDevice_Windows::enumerate_windows_devices();
    devices.insert(devices.end(), windows_devices.begin(), windows_devices.end());
#elif __APPLE__
    // Add macOS CoreAudio devices
    // auto macos_devices = AudioInputDevice_macOS::enumerate_macos_devices();
    // devices.insert(devices.end(), macos_devices.begin(), macos_devices.end());
#endif
    
    // Always add synthetic device
    AudioDeviceInfo synthetic;
    synthetic.id = "synthetic";
    synthetic.name = "Synthetic Device (File Playback)";
    synthetic.driver = "Synthetic";
    synthetic.default_sample_rate = 48000;
    synthetic.max_channels = 2;
    synthetic.is_default = false;
    devices.push_back(synthetic);
    
    return devices;
}

std::unique_ptr<IAudioInputDevice> AudioInputFactory::create_device(const std::string& device_id) {
    // Synthetic device
    if (device_id.empty() == false && 
        (device_id == "synthetic" || device_id.find("synthetic:") == 0)) {
        return std::make_unique<AudioInputDevice_Synthetic>();
    }
    
    // Platform-specific default or specific device
#ifdef _WIN32
    return std::make_unique<AudioInputDevice_Windows>();
#elif __APPLE__
    // return std::make_unique<AudioInputDevice_macOS>();
    return nullptr;  // TODO
#else
    #error "Unsupported platform"
#endif
}

std::string AudioInputFactory::get_default_device_id() {
#ifdef _WIN32
    auto devices = AudioInputDevice_Windows::enumerate_windows_devices();
#elif __APPLE__
    // auto devices = AudioInputDevice_macOS::enumerate_macos_devices();
    std::vector<AudioDeviceInfo> devices;  // TODO
#endif
    
    for (const auto& dev : devices) {
        if (dev.is_default) {
            return dev.id;
        }
    }
    
    return "";
}

bool AudioInputFactory::is_device_available(const std::string& device_id) {
    auto devices = enumerate_devices();
    for (const auto& dev : devices) {
        if (dev.id == device_id) {
            return true;
        }
    }
    return false;
}

} // namespace audio
