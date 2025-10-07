#pragma once
#include <cstdint>
#include <vector>
#include <string>
namespace audio {
class WindowsWasapiCapture {
public:
    struct DeviceInfo {
        std::string id;   // device ID (UTF-8)
        std::string name; // friendly name (UTF-8)
    };

    // Enumerate active input devices (microphones)
    static std::vector<DeviceInfo> list_input_devices();

    bool start();
    bool start_with_device(const std::string& device_id_utf8);
    void stop();
    // Current capture sample rate (Hz) or 0 if not started
    int sample_rate() const;
    // Format details (valid after start())
    int channels() const;
    int bits_per_sample() const;
    bool is_float() const; // true if IEEE float format
    // Read a chunk of captured audio frames (converted to int16 mono). Size may vary.
    std::vector<int16_t> read_chunk();
};
}
