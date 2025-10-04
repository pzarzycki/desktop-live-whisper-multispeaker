#include "asr/diarization_backend.hpp"

namespace asr {
bool DiarizationBackend::initialize() { return false; }
std::vector<Segment> DiarizationBackend::assign_speakers(const int16_t*, size_t) { return {}; }
}
