#pragma once

namespace audio {

/**
 * @brief CoreAudio output for macOS audio playback
 * 
 * Mirrors the functionality of WindowsWasapiOut for macOS platform.
 * Uses AudioQueue API for simple audio playback.
 */
class CoreAudioOutput {
public:
    CoreAudioOutput() = default;
    ~CoreAudioOutput();
    
    // No copy/move
    CoreAudioOutput(const CoreAudioOutput&) = delete;
    CoreAudioOutput& operator=(const CoreAudioOutput&) = delete;
    
    /**
     * @brief Start audio output with specified format
     * @param sample_rate Sample rate in Hz (e.g., 16000, 44100)
     * @param channels Number of channels (1 for mono, 2 for stereo)
     * @return true if successfully started
     */
    bool start(int sample_rate, int channels = 1);
    
    /**
     * @brief Stop audio output
     */
    void stop();
    
    /**
     * @brief Write PCM16 audio samples to output
     * @param data Pointer to interleaved PCM16 samples
     * @param frames Number of frames (samples per channel)
     */
    void write(const short* data, unsigned long long frames);
    
    /**
     * @brief Check if output is currently active
     */
    bool is_running() const { return running_; }

private:
    void* audio_queue_ = nullptr;  // AudioQueueRef (opaque pointer)
    void* buffers_[3] = {nullptr, nullptr, nullptr};  // AudioQueueBufferRef array
    int sample_rate_ = 0;
    int channels_ = 0;
    bool running_ = false;
};

}  // namespace audio
