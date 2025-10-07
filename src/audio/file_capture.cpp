#include "audio/file_capture.hpp"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace audio {

namespace {
struct WavHeader {
    char riff[4];
    uint32_t chunkSize;
    char wave[4];
    char fmt[4];
    uint32_t subchunk1Size;
    uint16_t audioFormat; // 1=PCM, 3=float
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};
} // namespace

bool FileCapture::start_from_wav(const std::string& path) {
    stop();
    source_path_.clear();
    mono_.clear();
    cursor_ = 0;
    sample_rate_ = 0;
    channels_ = 0;
    bits_per_sample_ = 0;
    duration_seconds_ = 0.0;

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    WavHeader hdr{};
    if (!f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr))) return false;
    if (std::strncmp(hdr.riff, "RIFF", 4) != 0 || std::strncmp(hdr.wave, "WAVE", 4) != 0) return false;

    // Find data chunk (skip optional fmt extension and other chunks)
    // We already read fmt header fixed part; advance to next chunk after possible extra fmt bytes
    uint32_t fmtExtra = hdr.subchunk1Size > 16 ? hdr.subchunk1Size - 16 : 0;
    if (fmtExtra) f.seekg(fmtExtra, std::ios::cur);

    char chunkId[4];
    uint32_t chunkSize = 0;
    std::streampos dataPos = {};
    while (f.read(chunkId, 4)) {
        if (!f.read(reinterpret_cast<char*>(&chunkSize), 4)) return false;
        if (std::strncmp(chunkId, "data", 4) == 0) {
            dataPos = f.tellg();
            break;
        }
        f.seekg(chunkSize, std::ios::cur);
    }
    if (dataPos == std::streampos{}) return false;

    sample_rate_ = static_cast<int>(hdr.sampleRate);
    channels_ = hdr.numChannels;
    bits_per_sample_ = hdr.bitsPerSample;
    const size_t bytesPerSample = hdr.bitsPerSample / 8;
    const size_t frameCount = chunkSize / (bytesPerSample * std::max<uint16_t>(1, hdr.numChannels));

    mono_.resize(frameCount);
    f.seekg(dataPos);

    if (hdr.audioFormat == 1 && hdr.bitsPerSample == 16) {
        // PCM16 interleaved
        std::vector<int16_t> buf(frameCount * hdr.numChannels);
        if (!f.read(reinterpret_cast<char*>(buf.data()), buf.size() * sizeof(int16_t))) return false;
        // downmix to mono by averaging channels
        for (size_t i = 0; i < frameCount; ++i) {
            int sum = 0;
            for (uint16_t c = 0; c < hdr.numChannels; ++c) {
                sum += buf[i * hdr.numChannels + c];
            }
            mono_[i] = static_cast<int16_t>(sum / std::max<uint16_t>(1, hdr.numChannels));
        }
    } else if (hdr.audioFormat == 3 && hdr.bitsPerSample == 32) {
        // float32 interleaved
        std::vector<float> buf(frameCount * hdr.numChannels);
        if (!f.read(reinterpret_cast<char*>(buf.data()), buf.size() * sizeof(float))) return false;
        for (size_t i = 0; i < frameCount; ++i) {
            float sum = 0.0f;
            for (uint16_t c = 0; c < hdr.numChannels; ++c) {
                sum += buf[i * hdr.numChannels + c];
            }
            float v = sum / static_cast<float>(std::max<uint16_t>(1, hdr.numChannels));
            v = std::clamp(v, -1.0f, 1.0f);
            mono_[i] = static_cast<int16_t>(std::lrint(v * 32767.0f));
        }
    } else {
        return false; // unsupported
    }

    duration_seconds_ = static_cast<double>(frameCount) / std::max<uint32_t>(1, hdr.sampleRate);
    source_path_ = path;
    return true;
}

void FileCapture::stop() {
    mono_.clear();
    cursor_ = 0;
    sample_rate_ = 0;
}

std::vector<int16_t> FileCapture::read_chunk() {
    std::vector<int16_t> out;
    if (sample_rate_ <= 0 || cursor_ >= mono_.size()) return out;
    // 20ms chunks - standard for audio streaming, reduces timing jitter
    size_t frames_per_chunk = static_cast<size_t>(sample_rate_ * 20 / 1000);
    frames_per_chunk = std::max<size_t>(1, frames_per_chunk);
    size_t remaining = mono_.size() - cursor_;
    size_t n = std::min(frames_per_chunk, remaining);
    out.insert(out.end(), mono_.begin() + cursor_, mono_.begin() + cursor_ + n);
    cursor_ += n;
    return out;
}

} // namespace audio
