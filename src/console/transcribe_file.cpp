#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include "asr/whisper_backend.hpp"

#pragma pack(push, 1)
struct WavHeader {
    char riff[4];      // "RIFF"
    uint32_t chunkSize;
    char wave[4];      // "WAVE"
    char fmt[4];       // "fmt "
    uint32_t subchunk1Size; // 16 for PCM
    uint16_t audioFormat;   // 1 for PCM, 3 for IEEE float
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};
#pragma pack(pop)

static bool read_wav_pcm(const std::string &path, int &sr, std::vector<int16_t> &mono) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::cerr << "[wav] open failed: " << path << "\n"; return false; }
    WavHeader hdr{};
    in.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!in) { std::cerr << "[wav] header read failed\n"; return false; }
    std::cerr << "[wav] riff=" << std::string(hdr.riff,4) << ", wave=" << std::string(hdr.wave,4)
              << ", fmt_tag_size=" << hdr.subchunk1Size << ", audioFormat=" << hdr.audioFormat
              << ", channels=" << hdr.numChannels << ", sampleRate=" << hdr.sampleRate
              << ", bitsPerSample=" << hdr.bitsPerSample << "\n";
    if (std::string(hdr.riff, 4) != "RIFF" || std::string(hdr.wave, 4) != "WAVE") { std::cerr << "[wav] not RIFF/WAVE\n"; return false; }
    // Skip any extra fmt bytes if present
    if (hdr.subchunk1Size > 16) in.seekg(hdr.subchunk1Size - 16, std::ios::cur);
    // Find "data" chunk
    char tag[4];
    uint32_t dataSize = 0;
    while (in.read(tag, 4)) {
        uint32_t size = 0;
        in.read(reinterpret_cast<char*>(&size), 4);
        if (std::string(tag, 4) == "data") { dataSize = size; break; }
        in.seekg(size, std::ios::cur);
    }
    if (dataSize == 0) { std::cerr << "[wav] no data chunk\n"; return false; }
    sr = static_cast<int>(hdr.sampleRate);
    const int ch = hdr.numChannels;
    const int bps = hdr.bitsPerSample;
    const size_t frames = (dataSize * 8ull) / (bps * ch);
    mono.resize(frames);
    if (hdr.audioFormat == 1 && bps == 16) {
        // PCM int16
        std::vector<int16_t> buf(frames * ch);
        in.read(reinterpret_cast<char*>(buf.data()), buf.size() * sizeof(int16_t));
        if (!in) { std::cerr << "[wav] pcm16 read failed\n"; return false; }
        for (size_t i = 0; i < frames; ++i) {
            int sum = 0;
            for (int c = 0; c < ch; ++c) sum += buf[i * ch + c];
            mono[i] = static_cast<int16_t>(sum / (ch ? ch : 1));
        }
    } else if (hdr.audioFormat == 3 && bps == 32) {
        // IEEE float32
        std::vector<float> buf(frames * ch);
        in.read(reinterpret_cast<char*>(buf.data()), buf.size() * sizeof(float));
        if (!in) { std::cerr << "[wav] float32 read failed\n"; return false; }
        for (size_t i = 0; i < frames; ++i) {
            float sum = 0.0f;
            for (int c = 0; c < ch; ++c) sum += buf[i * ch + c];
            float m = sum / (ch ? ch : 1);
            int v = static_cast<int>(m * 32767.0f);
            v = std::clamp(v, -32768, 32767);
            mono[i] = static_cast<int16_t>(v);
        }
    } else {
        std::cerr << "[wav] unsupported format: audioFormat=" << hdr.audioFormat << ", bps=" << bps << "\n";
        return false;
    }
    return true;
}

static std::vector<int16_t> resample_to_16k(const std::vector<int16_t>& in, int in_hz) {
    const int target = 16000;
    if (in_hz == target || in_hz <= 0 || in.empty()) return in;
    const double ratio = static_cast<double>(target) / static_cast<double>(in_hz);
    const size_t out_len = static_cast<size_t>(std::llround(in.size() * ratio));
    std::vector<int16_t> out(out_len);
    // Linear interpolation on sample positions
    for (size_t i = 0; i < out_len; ++i) {
        double src_pos = i / ratio; // position in input samples
        size_t i0 = static_cast<size_t>(src_pos);
        size_t i1 = std::min(i0 + 1, in.size() - 1);
        double frac = src_pos - static_cast<double>(i0);
        double v = (1.0 - frac) * static_cast<double>(in[i0]) + frac * static_cast<double>(in[i1]);
        int vi = static_cast<int>(std::lrint(v));
        vi = std::clamp(vi, -32768, 32767);
        out[i] = static_cast<int16_t>(vi);
    }
    return out;
}

int main(int argc, char** argv) {
    // Force sync and print an early init line
    std::ios::sync_with_stdio(true);
    try {
    auto cwd = std::filesystem::current_path();
    std::cerr << "[init] app_transcribe_file starting; argc=" << argc << ", cwd=" << cwd.string() << "\n";
    } catch (...) {
        std::cerr << "[init] app_transcribe_file starting; argc=" << argc << ", cwd=<unavailable>\n";
    }
    // Append a quick debug log so we can confirm start even if console drops output
    try {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::ofstream dbg("transcribe_file_debug.log", std::ios::app);
        dbg << "\n=== run at " << std::ctime(&now);
        dbg << "argv:";
        for (int i = 0; i < argc; ++i) dbg << " [" << i << "]=" << argv[i];
        dbg << "\n";
    } catch (...) {
        // ignore file logging errors
    }
    bool verbose = false;
    int limit_sec = 0; // 0 = no limit
    std::string path;
    std::string modelArg; // can be bare name (small.en) or explicit path (.gguf/.bin)
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-v" || a == "--verbose") { verbose = true; continue; }
        if (a == "--limit-seconds" && i + 1 < argc) { limit_sec = std::atoi(argv[++i]); continue; }
        if (a == "--model" && i + 1 < argc) { modelArg = argv[++i]; continue; }
        if (path.empty()) path = a;
    }
    if (verbose) {
#if defined(_WIN32)
        _putenv("WHISPER_DEBUG=1");
#else
        setenv("WHISPER_DEBUG", "1", 1);
#endif
    if (!modelArg.empty()) std::cerr << "Model arg: " << modelArg << "\n";
    }
    if (path.empty()) {
        std::cerr << "Usage: app_transcribe_file [-v] [--limit-seconds N] [--model <path-or-name>] <path-to-wav>\n";
        return 1;
    }

    int sr = 0;
    std::vector<int16_t> mono;
    if (!read_wav_pcm(path, sr, mono)) {
        std::cerr << "Failed to read WAV: " << path << "\n";
        return 1;
    }

    std::cerr << "[wav] decoded mono: sr=" << sr << ", samples=" << mono.size() << ", duration~" << (sr>0? (mono.size() / (double)sr):0.0) << "s\n";
    // Optionally limit duration before resample
    if (limit_sec > 0) {
        size_t max_samples = static_cast<size_t>(std::max(1, limit_sec) * sr);
        if (mono.size() > max_samples) mono.resize(max_samples);
    }
    auto pcm16k = resample_to_16k(mono, sr);
    std::cerr << "[resample] to 16k: samples=" << pcm16k.size() << ", duration~" << (pcm16k.size() / 16000.0) << "s\n";

    asr::WhisperBackend whisper;
    bool model_ok = false;
    if (!modelArg.empty()) {
        model_ok = whisper.load_model(modelArg);
    } else {
        model_ok = whisper.load_model("small.en") || whisper.load_model("small");
    }
    if (!model_ok) {
        std::cerr << "[whisper] Model load failed. Ensure a valid .gguf or .bin exists and path is correct.\n";
        return 1;
    }

    auto text = whisper.transcribe_chunk(pcm16k.data(), pcm16k.size());
    if (text.empty()) {
        std::cerr << "[whisper] Empty transcription output.\n";
        std::cerr << "[done] exit=2\n";
        return 2;
    }
    std::cout << text << "\n";
    std::cerr << "[done] exit=0\n";
    return 0;
}
