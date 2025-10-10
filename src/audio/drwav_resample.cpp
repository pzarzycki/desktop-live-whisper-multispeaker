// Use whisper.cpp's audio reading with proper resampling via ffmpeg
#include "drwav_resample.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

// Include dr_wav from whisper.cpp
#define DR_WAV_IMPLEMENTATION
#include "../../third_party/whisper.cpp/examples/dr_wav.h"

namespace audio {

bool read_wav_with_drwav(const std::string& path, std::vector<int16_t>& mono_16k, int& sample_rate) {
    // Use temporary 16kHz converted file if input is not 16kHz
    drwav wav;
    if (!drwav_init_file(&wav, path.c_str(), nullptr)) {
        return false;
    }
    
    // Check if conversion is needed
    if (wav.sampleRate != 16000) {
        drwav_uninit(&wav);
        
        // Use ffmpeg to convert to 16kHz mono
        std::string temp_path = "output/temp_16k.wav";
        std::filesystem::create_directories("output");
        
        std::string cmd = "ffmpeg -i \"" + path + "\" -ar 16000 -ac 1 -c:a pcm_s16le \"" + temp_path + "\" -y -loglevel error";
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            fprintf(stderr, "ffmpeg conversion failed\n");
            return false;
        }
        
        // Now read the converted file
        if (!drwav_init_file(&wav, temp_path.c_str(), nullptr)) {
            return false;
        }
    }
    
    if (wav.sampleRate != 16000) {
        fprintf(stderr, "WAV file must be 16kHz after conversion\n");
        drwav_uninit(&wav);
        return false;
    }
    
    const uint64_t n = wav.totalPCMFrameCount;
    std::vector<int16_t> pcm16;
    pcm16.resize(n * wav.channels);
    drwav_read_pcm_frames_s16(&wav, n, pcm16.data());
    drwav_uninit(&wav);
    
    // Convert to mono if stereo
    mono_16k.resize(n);
    if (wav.channels == 1) {
        mono_16k = pcm16;
    } else {
        for (uint64_t i = 0; i < n; i++) {
            int32_t sum = 0;
            for (unsigned c = 0; c < wav.channels; ++c) {
                sum += pcm16[i * wav.channels + c];
            }
            mono_16k[i] = static_cast<int16_t>(sum / wav.channels);
        }
    }
    
    sample_rate = 16000;
    return true;
}

} // namespace audio
