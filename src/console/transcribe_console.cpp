#include <iostream>
#include <thread>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <string>
#include <vector>
#include "audio/windows_wasapi.hpp"
#include "asr/whisper_backend.hpp"

// Linear interpolation resampler to 16 kHz
static std::vector<int16_t> resample_to_16k(const std::vector<int16_t>& in, int in_hz) {
    const int target = 16000;
    if (in_hz == target || in_hz <= 0 || in.empty()) return in;
    const double ratio = static_cast<double>(target) / static_cast<double>(in_hz);
    const size_t out_len = static_cast<size_t>(std::llround(in.size() * ratio));
    std::vector<int16_t> out(out_len);
    for (size_t i = 0; i < out_len; ++i) {
        double src_pos = i / ratio;
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
    // Parse args: optional -v/--verbose and optional device id
    bool verbose = false;
    std::string deviceId;
    std::string modelArg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-v" || a == "--verbose") { verbose = true; continue; }
        if (a == "--model" && i + 1 < argc) { modelArg = argv[++i]; continue; }
        deviceId = a; // first non-flag arg is device id
    }

    // Capture
    audio::WindowsWasapiCapture cap;
    bool ok = false;
    if (!deviceId.empty()) {
        if (verbose) std::cout << "Using device id: " << deviceId << "\n";
        ok = cap.start_with_device(deviceId);
    } else {
        if (verbose) std::cout << "Using default input device\n";
        ok = cap.start();
    }
    if (!ok) { std::cerr << "Failed to start capture" << std::endl; return 1; }
    if (verbose) std::cout << "Input sample rate: " << cap.sample_rate() << " Hz\n";

    // Whisper
    asr::WhisperBackend whisper;
    // Pick model: explicit via --model or auto small.en/small
    bool model_ok = false;
    if (!modelArg.empty()) model_ok = whisper.load_model(modelArg);
    else model_ok = whisper.load_model("small.en") || whisper.load_model("small");
    if (!model_ok) {
        std::cerr << "Whisper model not found. Place a model under models/, e.g.:\n"
                     "  models/small.en.gguf or models/small.gguf (GGUF)\n"
                     "  models/small.en.bin  (legacy GGML BIN)\n";
    } else if (verbose) {
        std::cout << "Model loaded\n";
    }

    std::cout << "Transcribing... press Ctrl+C to stop" << std::endl;

    // Accumulate ~2 seconds before transcribing for better results
    std::vector<int16_t> acc16k;
    const int target_hz = 16000;
    const size_t window_samples = static_cast<size_t>(target_hz * 2); // 2 seconds
    const int loops = 1000; // ~20 seconds at 20ms sleeps
    for (int i = 0; i < loops; ++i) {
        auto chunk = cap.read_chunk();
        if (!chunk.empty()) {
            auto ds = resample_to_16k(chunk, cap.sample_rate());
            if (!ds.empty()) {
                acc16k.insert(acc16k.end(), ds.begin(), ds.end());
                if (verbose) { std::cout << "." << std::flush; }
            }
            if (acc16k.size() >= window_samples) {
                auto txt = whisper.transcribe_chunk(acc16k.data(), acc16k.size());
                if (!txt.empty()) {
                    if (verbose) std::cout << "\n"; // newline after dots
                    std::cout << txt << std::flush;
                }
                acc16k.clear();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    cap.stop();
    if (verbose) std::cout << "\n";
    return 0;
}
