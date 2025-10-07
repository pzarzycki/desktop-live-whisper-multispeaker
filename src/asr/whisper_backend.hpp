#pragma once
#include <string>
namespace asr {
class WhisperBackend {
public:
    bool load_model(const std::string& model_name);
    std::string transcribe_chunk(const int16_t* data, size_t samples);
    void set_threads(int n);
    void set_speed_up(bool on);
    void set_max_text_ctx(int n_tokens); // 0 = default
};
}
