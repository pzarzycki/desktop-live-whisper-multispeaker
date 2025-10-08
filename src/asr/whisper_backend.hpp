#pragma once
#include <string>
#include <vector>

namespace asr {

struct WhisperSegment {
    std::string text;
    int64_t t0_ms;  // start time in milliseconds
    int64_t t1_ms;  // end time in milliseconds
};

struct WhisperWord {
    std::string word;
    int64_t t0_ms;  // start time in milliseconds
    int64_t t1_ms;  // end time in milliseconds
    float probability;  // confidence score
};

struct WhisperSegmentWithWords {
    std::string text;
    int64_t t0_ms;
    int64_t t1_ms;
    std::vector<WhisperWord> words;
};

class WhisperBackend {
public:
    bool load_model(const std::string& model_name);
    std::string transcribe_chunk(const int16_t* data, size_t samples);
    std::vector<WhisperSegment> transcribe_chunk_segments(const int16_t* data, size_t samples);
    std::vector<WhisperSegmentWithWords> transcribe_chunk_with_words(const int16_t* data, size_t samples);
    void set_threads(int n);
    void set_speed_up(bool on);
    void set_max_text_ctx(int n_tokens); // 0 = default
};
}
