#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace asr {
struct Segment {
    std::string speaker; // "Speaker N"
    float start_s{};
    float end_s{};
    std::string text;
};

class DiarizationBackend {
public:
    bool initialize();
    std::vector<Segment> assign_speakers(const int16_t* data, size_t samples);
};
}
