#pragma once
#include <vector>
#include <string>

namespace audio {

// Read WAV file and convert to 16kHz mono using ffmpeg if needed
// Returns true on success
bool read_wav_with_drwav(const std::string& path, std::vector<int16_t>& mono_16k, int& sample_rate);

} // namespace audio
