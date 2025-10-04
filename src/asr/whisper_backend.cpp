#include "asr/whisper_backend.hpp"
#include <string>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <cstdlib>
#include <iostream>
#if defined(WHISPER_BACKEND_AVAILABLE)
#include "whisper.h"
#include <fstream>
// internal state for whisper backend
namespace {
struct whisper_state_holder {
	whisper_context* ctx = nullptr;
	bool initialized = false;
};
static whisper_state_holder g_ws;
static bool is_verbose() {
#if defined(_WIN32)
	return std::getenv("WHISPER_DEBUG") != nullptr;
#else
	return std::getenv("WHISPER_DEBUG") != nullptr;
#endif
}
} // anonymous namespace
#endif // WHISPER_BACKEND_AVAILABLE

namespace asr {

bool WhisperBackend::load_model(const std::string& model_name) {
#if defined(WHISPER_BACKEND_AVAILABLE)
	if (g_ws.initialized) return true;
	// Resolve model path
	std::string path = model_name;
	auto exists = [](const std::string& p){ return std::filesystem::exists(std::filesystem::u8path(p)); };
	// Try common GGUF and legacy GGML BIN patterns under models/
	const bool has_ext = (path.find(".gguf") != std::string::npos) || (path.find(".bin") != std::string::npos);
	if (!has_ext) {
		// GGUF candidates
		const std::string gguf1 = std::string("models/") + model_name + ".gguf";
		const std::string gguf2 = std::string("models/ggml-") + model_name + "-q5_1.gguf";
		const std::string gguf3 = std::string("models/ggml-") + model_name + ".gguf";
		// BIN candidates (legacy)
		const std::string bin1  = std::string("models/") + model_name + ".bin";
		const std::string bin2  = std::string("models/ggml-") + model_name + ".bin";
		const std::string bin3  = std::string("models/ggml-") + model_name + "-q5_1.bin";
		// Also try vendored ggml models in third_party/whisper.cpp/models
		const std::string tbin1 = std::string("third_party/whisper.cpp/models/ggml-") + model_name + ".bin";
		const std::string tbin2 = std::string("third_party/whisper.cpp/models/") + model_name + ".bin";

		if      (exists(gguf1)) path = gguf1;
		else if (exists(gguf2)) path = gguf2;
		else if (exists(gguf3)) path = gguf3;
		else if (exists(bin1))  path = bin1;
		else if (exists(bin2))  path = bin2;
		else if (exists(bin3))  path = bin3;
		else if (exists(tbin1)) path = tbin1;
		else if (exists(tbin2)) path = tbin2;
		else                    path = gguf1; // fallback, may fail
	}
	whisper_context_params cparams = whisper_context_default_params();
	cparams.use_gpu = false; // CPU path
	std::cerr << "[whisper] init from: " << path << "\n";
	g_ws.ctx = whisper_init_from_file_with_params(path.c_str(), cparams);
	g_ws.initialized = (g_ws.ctx != nullptr);
	if (!g_ws.initialized) {
		std::cerr << "[whisper] init FAILED for path: " << path << "\n";
	} else {
		std::cerr << "[whisper] init OK: " << path << "\n";
		std::cerr << "[whisper] system: " << whisper_print_system_info() << "\n";
	}
	return g_ws.initialized;
#else
	std::cerr << "[whisper] backend not available (built without whisper).\n";
	return true;
#endif
}

std::string WhisperBackend::transcribe_chunk(const int16_t* data, size_t samples) {
	if (!data || samples == 0) return {};
#if defined(WHISPER_BACKEND_AVAILABLE)
	if (!g_ws.ctx) return {};
	// Configure decoding similar to simple example: greedy, no timestamps, single segment
	whisper_full_params wparams = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
	wparams.print_realtime   = is_verbose();
	wparams.print_progress   = is_verbose();
	wparams.print_timestamps = false;
	wparams.no_timestamps    = true;       // plain text only
	wparams.single_segment   = true;       // one segment output
	wparams.no_context       = true;       // independent chunk decoding
	wparams.language         = "en";      // fixed language per user spec
	wparams.detect_language  = false;
	wparams.n_threads        = std::max(1u, std::thread::hardware_concurrency());
	wparams.greedy.best_of   = 1;
	wparams.temperature      = 0.0f;

	// Optional: progress callback to surface activity when verbose
	wparams.progress_callback = [](whisper_context*, whisper_state*, int progress, void*) {
		std::cerr << "[whisper] progress: " << progress << "%\n";
	};
	// Convert int16 PCM to float [-1,1]
	std::vector<float> pcm_f32;
	pcm_f32.reserve(samples);
	constexpr float scale = 1.0f / 32768.0f;
	for (size_t i = 0; i < samples; ++i) {
		pcm_f32.push_back(static_cast<float>(data[i]) * scale);
	}
	std::cerr << "[whisper] running on samples=" << pcm_f32.size() << ", threads=" << wparams.n_threads << "\n";
	if (!pcm_f32.empty()) {
		size_t nprint = std::min<size_t>(5, pcm_f32.size());
		std::cerr << "[whisper] pcm_f32[0:" << nprint << "]=";
		for (size_t i = 0; i < nprint; ++i) std::cerr << pcm_f32[i] << (i+1<nprint? ",":"");
		std::cerr << "\n";
	}
	int ret = whisper_full(g_ws.ctx, wparams, pcm_f32.data(), (int)pcm_f32.size());
	if (ret != 0) {
		std::cerr << "[whisper] whisper_full FAILED, ret=" << ret << "\n";
		return {};
	}
	std::string out;
	int n = whisper_full_n_segments(g_ws.ctx);
	std::cerr << "[whisper] segments=" << n << "\n";
	for (int i = 0; i < n; ++i) {
		const char* txt = whisper_full_get_segment_text(g_ws.ctx, i);
		if (txt) out += txt;
	}
	whisper_print_timings(g_ws.ctx);
	return out;
#else
	std::cerr << "[whisper] backend not available; returning no text.\n";
	return std::string();
#endif
}

} // namespace asr
