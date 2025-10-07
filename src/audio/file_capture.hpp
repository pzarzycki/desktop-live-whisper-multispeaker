#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace audio {

// Simple WAV-backed capture that simulates a microphone by returning ~20ms chunks
class FileCapture {
public:
    bool start_from_wav(const std::string& path);
    void stop();
    int sample_rate() const { return sample_rate_; }
    // Returns next chunk of mono int16 frames at the original file sample rate.
    // Empty when no more data.
    std::vector<int16_t> read_chunk();

    // Basic file info for reporting
    int channels() const { return channels_; }
    int bits_per_sample() const { return bits_per_sample_; }
    double duration_seconds() const { return duration_seconds_; }
    std::string source_path() const { return source_path_; }

private:
    std::string source_path_;
    std::vector<int16_t> mono_; // decoded mono PCM16
    size_t cursor_ = 0;
    int sample_rate_ = 0;
    int channels_ = 0;
    int bits_per_sample_ = 0;
    double duration_seconds_ = 0.0;
};

} // namespace audio
