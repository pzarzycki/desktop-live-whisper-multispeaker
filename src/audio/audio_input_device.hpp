#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

namespace audio {

/**
 * @brief Metadata about an audio input device
 */
struct AudioDeviceInfo {
    std::string id;              // Unique device identifier (platform-specific)
    std::string name;            // Human-readable name ("Microphone (Realtek)")
    std::string driver;          // Driver/API name ("WASAPI", "CoreAudio", "Synthetic")
    int default_sample_rate;     // Native sample rate (48000, 44100, etc.)
    int max_channels;            // Maximum supported channels
    bool is_default;             // Is this the system default device?
    
    AudioDeviceInfo() 
        : default_sample_rate(48000)
        , max_channels(2)
        , is_default(false) {}
};

/**
 * @brief Configuration for audio input capture
 */
struct AudioInputConfig {
    std::string device_id;       // Device to use (empty = system default)
    int sample_rate = 48000;     // Requested sample rate
    int channels = 1;            // Mono = 1, Stereo = 2
    int buffer_size_ms = 100;    // Buffer size in milliseconds (affects latency)
    
    // For synthetic device only
    std::string synthetic_file_path;    // Path to WAV/audio file
    bool synthetic_loop = false;        // Loop playback?
    bool synthetic_playback = true;     // Play to speakers while reading?
};

/**
 * @brief Callback for audio data
 * 
 * Called from audio thread when new samples are available.
 * 
 * @param samples PCM16 audio samples (interleaved if stereo)
 * @param sample_count Number of samples (not frames - for stereo, this is frames * 2)
 * @param sample_rate Actual sample rate of the data
 * @param channels Number of channels (1=mono, 2=stereo)
 */
using AudioCallback = std::function<void(
    const int16_t* samples, 
    size_t sample_count,
    int sample_rate,
    int channels
)>;

/**
 * @brief Error callback for device issues
 * 
 * @param error_message Human-readable error description
 * @param is_fatal If true, device has stopped and needs restart
 */
using ErrorCallback = std::function<void(const std::string& error_message, bool is_fatal)>;

/**
 * @brief Abstract base class for audio input devices
 * 
 * Platform-specific implementations:
 * - AudioInputDevice_Windows (WASAPI)
 * - AudioInputDevice_macOS (CoreAudio)
 * - AudioInputDevice_Synthetic (file playback)
 */
class IAudioInputDevice {
public:
    virtual ~IAudioInputDevice() = default;
    
    /**
     * @brief Initialize the device with configuration
     * @param config Device configuration
     * @param audio_callback Called when audio data is ready
     * @param error_callback Called on errors
     * @return true if initialization succeeded
     */
    virtual bool initialize(
        const AudioInputConfig& config,
        AudioCallback audio_callback,
        ErrorCallback error_callback
    ) = 0;
    
    /**
     * @brief Start capturing audio
     * @return true if started successfully
     */
    virtual bool start() = 0;
    
    /**
     * @brief Stop capturing audio
     */
    virtual void stop() = 0;
    
    /**
     * @brief Check if device is currently capturing
     */
    virtual bool is_capturing() const = 0;
    
    /**
     * @brief Get device information
     */
    virtual AudioDeviceInfo get_device_info() const = 0;
    
    /**
     * @brief Get actual configuration being used (may differ from requested)
     */
    virtual AudioInputConfig get_actual_config() const = 0;
};

/**
 * @brief Factory for creating audio input devices
 */
class AudioInputFactory {
public:
    /**
     * @brief Enumerate all available audio input devices
     * @return List of available devices (includes synthetic if available)
     */
    static std::vector<AudioDeviceInfo> enumerate_devices();
    
    /**
     * @brief Create an audio input device
     * @param device_id Device ID from AudioDeviceInfo, or empty for default
     *                  Special values:
     *                  - "" or "default" = system default microphone
     *                  - "synthetic" = synthetic file playback device
     *                  - "synthetic:path/to/file.wav" = synthetic with file path
     * @return Device instance, or nullptr on failure
     */
    static std::unique_ptr<IAudioInputDevice> create_device(const std::string& device_id = "");
    
    /**
     * @brief Get the system default device ID
     */
    static std::string get_default_device_id();
    
    /**
     * @brief Check if a device ID is valid
     */
    static bool is_device_available(const std::string& device_id);
};

} // namespace audio
