#pragma once
#include <string>
namespace asr {
class WhisperBackend {
public:
    bool load_model(const std::string& model_name);
    std::string transcribe_chunk(const int16_t* data, size_t samples);
};
}
