// Unified streaming console: microphone or WAV-backed simulated microphone
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
#include <thread>
#include <cmath>
#include <iomanip>
#include <deque>
#include <cctype>
#include <atomic>
#include <mutex>
#include <map>
#include <set>
#include "asr/whisper_backend.hpp"
#include "audio/windows_wasapi.hpp"
#include "audio/file_capture.hpp"
#include "audio/windows_wasapi_out.hpp"
#include "audio/audio_queue.hpp"
#include "diar/speaker_cluster.hpp"

// WAV parsing moved into audio::FileCapture

// Thread-safe performance tracking
struct PerfMetrics {
    std::mutex mutex;
    double resample_acc = 0.0;
    double diar_acc = 0.0;
    double whisper_acc = 0.0;
    
    void add_resample(double t) { std::lock_guard<std::mutex> lock(mutex); resample_acc += t; }
    void add_diar(double t) { std::lock_guard<std::mutex> lock(mutex); diar_acc += t; }
    void add_whisper(double t) { std::lock_guard<std::mutex> lock(mutex); whisper_acc += t; }
    
    void get(double& r, double& d, double& w) {
        std::lock_guard<std::mutex> lock(mutex);
        r = resample_acc; d = diar_acc; w = whisper_acc;
    }
};

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

// Simple tokenizer that splits into lowercase alphanumeric "words"
static std::vector<std::string> tokenize_words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (unsigned char uc : s) {
        if (std::isalnum(uc)) {
            cur.push_back(static_cast<char>(std::tolower(uc)));
        } else {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Maintain a rolling history of last words to de-duplicate overlapped window text
class Deduper {
public:
    // Return the input text with any prefix that overlaps the history removed
    std::string merge(const std::string& text) {
        auto words = tokenize_words(text);
        if (words.empty()) return std::string();
        size_t max_overlap = std::min<size_t>({ history.size(), words.size(), 12 });
        size_t best = 0;
        for (size_t k = max_overlap; k > 0; --k) {
            bool match = true;
            for (size_t i = 0; i < k; ++i) {
                if (history[history.size() - k + i] != words[i]) { match = false; break; }
            }
            if (match) { best = k; break; }
        }
        // Reconstruct output as space-joined words from words[best:]
        std::string out;
        for (size_t i = best; i < words.size(); ++i) {
            if (!out.empty()) out.push_back(' ');
            out += words[i];
        }
        // Update history with all words from this segment (not just the non-overlapped)
        for (const auto& w : words) {
            history.push_back(w);
            if (history.size() > max_history) history.pop_front();
        }
        return out;
    }
private:
    std::deque<std::string> history;
    static constexpr size_t max_history = 64;
};

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
    bool print_levels = false;
    int limit_sec = 0; // 0 = no limit; applies to both file and mic modes
    int user_threads = 0; // 0 = auto
    int max_text_ctx = 0; // 0 = default
    bool speed_up = true;
    std::string path; // optional WAV path; if empty, use microphone
        std::string deviceId; // optional WASAPI device ID
    std::string modelArg; // can be bare name (small.en) or explicit path (.gguf/.bin)
    std::string save_mic_wav; // if non-empty and using mic, save raw captured mono int16 at input SR
    bool play_file = true;    // default: when using file as input, play to speakers in real-time
    bool enable_diar = true;  // allow disabling diarization for perf isolation
    bool enable_asr = true;   // allow disabling ASR to debug audio path
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-v" || a == "--verbose") { verbose = true; continue; }
        if (a == "--limit-seconds" && i + 1 < argc) { limit_sec = std::atoi(argv[++i]); continue; }
        if (a == "--print-levels") { print_levels = true; continue; }
        if (a == "--device" && i + 1 < argc) { deviceId = argv[++i]; continue; }
    if (a == "--model" && i + 1 < argc) { modelArg = argv[++i]; continue; }
    if (a == "--threads" && i + 1 < argc) { user_threads = std::atoi(argv[++i]); continue; }
    if (a == "--max-text-ctx" && i + 1 < argc) { max_text_ctx = std::atoi(argv[++i]); continue; }
    if (a == "--no-speed-up") { speed_up = false; continue; }
        if (a == "--save-mic-wav") {
            // Optional path argument
            if (i + 1 < argc) {
                std::string maybe = argv[i+1];
                if (!maybe.empty() && maybe.rfind("-", 0) != 0) { save_mic_wav = maybe; ++i; }
            }
            if (save_mic_wav.empty()) save_mic_wav = "output/test_mic.wav";
            continue;
        }
        if (a == "--play-file") { play_file = true; continue; }
        if (a == "--no-play-file") { play_file = false; continue; }
        if (a == "--no-diar") { enable_diar = false; continue; }
        if (a == "--no-asr") { enable_asr = false; continue; }
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
    // Choose capture: WAV-backed simulation or real mic
    audio::FileCapture fileCap;
    audio::WindowsWasapiCapture micCap;
    audio::WindowsWasapiOut speaker;
    bool useFile = !path.empty();
    int in_sr = 0;
    std::string actualPath = path;
    
    if (useFile) {
        if (!fileCap.start_from_wav(path)) {
            std::cerr << "[input] failed to open WAV: " << path << "\n";
            return 1;
        }
        in_sr = fileCap.sample_rate();
        
        // CRITICAL FIX: Linear interpolation resampler is BROKEN (verified with whisper-cli)
        // Use ffmpeg for professional-grade resampling
        if (in_sr != 16000) {
            std::cerr << "[input] Converting " << in_sr << "Hz to 16kHz using ffmpeg (linear interpolation produces garbage)...\n";
            actualPath = "output/temp_16k.wav";
            
            // Ensure output directory exists
            namespace fs = std::filesystem;
            try {
                fs::path outPath = fs::u8path(actualPath);
                if (outPath.has_parent_path()) {
                    fs::create_directories(outPath.parent_path());
                }
            } catch (...) {}
            
            std::string cmd = "ffmpeg -i \"" + path + "\" -ar 16000 -ac 1 -c:a pcm_s16le \"" + actualPath + "\" -y -loglevel error";
            int ret = std::system(cmd.c_str());
            if (ret != 0) {
                std::cerr << "[ERROR] ffmpeg conversion failed. Please install ffmpeg:\n";
                std::cerr << "        Windows: winget install ffmpeg  OR  choco install ffmpeg\n";
                std::cerr << "        Or provide 16kHz input directly.\n";
                return 1;
            }
            
            // Re-open the converted file
            fileCap.stop();
            if (!fileCap.start_from_wav(actualPath)) {
                std::cerr << "[input] failed to open converted WAV: " << actualPath << "\n";
                return 1;
            }
            in_sr = fileCap.sample_rate();
            std::cerr << "[input] Conversion successful, using: " << actualPath << "\n";
        }
        
        std::cerr << "[input] file: " << actualPath
                  << ", sr=" << in_sr
                  << ", ch=" << fileCap.channels()
                  << ", bps=" << fileCap.bits_per_sample()
                  << ", dur~" << fileCap.duration_seconds() << "s\n";
        // Optionally start speaker playback at input SR so we hear what Whisper sees (post-downmix but pre-resample)
        if (play_file) {
            if (speaker.start(in_sr, 2)) {
                std::cerr << "[play] output enabled at " << in_sr << " Hz to default device\n";
            } else {
                std::cerr << "[play] failed to start output; continuing silent\n";
                play_file = false;
            }
        }
    } else {
        bool micOk = false;
        if (!deviceId.empty()) {
            micOk = micCap.start_with_device(deviceId);
        } else {
            micOk = micCap.start();
        }
        if (!micOk) {
            std::cerr << "[input] failed to start microphone\n";
            return 1;
        }
        in_sr = micCap.sample_rate();
        std::cerr << "[input] microphone: sr=" << in_sr << " Hz";
        if (!deviceId.empty()) std::cerr << ", deviceId=" << deviceId;
        std::cerr << ", ch=" << micCap.channels() << ", bps=" << micCap.bits_per_sample() << (micCap.is_float()? ", float":"") << "\n";
    }

    asr::WhisperBackend whisper;
    if (enable_asr) {
        if (user_threads > 0) whisper.set_threads(user_threads);
        whisper.set_speed_up(speed_up);
        if (max_text_ctx > 0) whisper.set_max_text_ctx(max_text_ctx);
        bool model_ok = false;
        if (!modelArg.empty()) {
            model_ok = whisper.load_model(modelArg);
        } else {
            // Default to tiny.en for best transcription quality and real-time performance
            model_ok = whisper.load_model("tiny.en") || whisper.load_model("base.en") || whisper.load_model("small.en");
        }
        if (!model_ok) {
            std::cerr << "[whisper] Model load failed. Ensure a valid .gguf or .bin exists and path is correct.\n";
            return 1;
        }
    }

    // Stream in ~20ms chunks, accumulate to ~2s windows for better accuracy, then transcribe
    std::cout << "Transcribing... press Ctrl+C to stop" << std::endl;

    // Optional: setup WAV writer for mic recording
    struct WavWriter {
        std::ofstream out;
        uint32_t sampleRate = 0;
        uint32_t dataBytes = 0;
        bool open(const std::string& p, uint32_t sr) {
            namespace fs = std::filesystem;
            try {
                fs::path fp = fs::u8path(p);
                if (fp.has_parent_path()) fs::create_directories(fp.parent_path());
            } catch (...) {}
            out.open(p, std::ios::binary);
            if (!out) return false;
            sampleRate = sr;
            // Write placeholder header
            out.write("RIFF", 4);
            uint32_t chunkSize = 36; out.write(reinterpret_cast<char*>(&chunkSize), 4);
            out.write("WAVE", 4);
            out.write("fmt ", 4);
            uint32_t sub1 = 16; out.write(reinterpret_cast<char*>(&sub1), 4);
            uint16_t audioFormat = 1; out.write(reinterpret_cast<char*>(&audioFormat), 2);
            uint16_t numChannels = 1; out.write(reinterpret_cast<char*>(&numChannels), 2);
            uint32_t sr32 = sampleRate; out.write(reinterpret_cast<char*>(&sr32), 4);
            uint32_t byteRate = sampleRate * 2; out.write(reinterpret_cast<char*>(&byteRate), 4);
            uint16_t blockAlign = 2; out.write(reinterpret_cast<char*>(&blockAlign), 2);
            uint16_t bitsPerSample = 16; out.write(reinterpret_cast<char*>(&bitsPerSample), 2);
            out.write("data", 4);
            uint32_t dataSize = 0; out.write(reinterpret_cast<char*>(&dataSize), 4);
            return true;
        }
        void write(const int16_t* data, size_t n) {
            if (!out) return;
            out.write(reinterpret_cast<const char*>(data), n * sizeof(int16_t));
            dataBytes += static_cast<uint32_t>(n * sizeof(int16_t));
        }
        void close() {
            if (!out) return;
            // Fix sizes
            try {
                out.seekp(4, std::ios::beg);
                uint32_t chunkSize = 36 + dataBytes; out.write(reinterpret_cast<char*>(&chunkSize), 4);
                out.seekp(40, std::ios::beg);
                uint32_t dataSize = dataBytes; out.write(reinterpret_cast<char*>(&dataSize), 4);
            } catch (...) {}
            out.close();
        }
    };

    WavWriter wavOut;
    const bool recordMic = (!useFile && !save_mic_wav.empty());
    if (recordMic) {
        if (!wavOut.open(save_mic_wav, static_cast<uint32_t>(in_sr))) {
            std::cerr << "[save] failed to open WAV for writing: " << save_mic_wav << "\n";
        } else {
            std::cerr << "[save] recording mic to: " << save_mic_wav << "\n";
        }
    }
    
    // Save resampled audio (what Whisper actually sees) for debugging
    WavWriter wavResampledOut;
    bool recordResampled = useFile; // Always save for file input to debug crackling
    if (recordResampled) {
        std::string resampled_path = "output/whisper_input_16k.wav";
        if (!wavResampledOut.open(resampled_path, 16000)) {
            std::cerr << "[save] failed to open resampled WAV: " << resampled_path << "\n";
            recordResampled = false;
        } else {
            std::cerr << "[save] recording resampled audio (Whisper input) to: " << resampled_path << "\n";
        }
    }

    // Thread-safe queue for audio chunks
    // Large buffer (50 chunks ≈ 1 second) allows processing to catch up during slow periods
    audio::AudioQueue audioQueue(50);
    
    // Shared state
    std::atomic<bool> processing_done{false};
    std::atomic<uint64_t> processed_in_samples{0};
    PerfMetrics perf;
    std::mutex print_mtx;  // For thread-safe output
    
    // Processing thread: handles resampling, diarization, and ASR
    std::thread processing_thread;
    if (enable_asr) {
        processing_thread = std::thread([&]() {
            // NEW STREAMING ARCHITECTURE
            // - Accumulate audio in sliding window (5-10s)
            // - Let Whisper find natural boundaries (VAD)
            // - Only emit "stable" segments (not in overlap zone)
            // - Keep feeding infinite stream from microphone
            
            diar::SpeakerClusterer spk(2, 0.45f);
            const int target_hz = 16000;
            
            // REAL STREAMING: Fixed buffers, must handle infinite audio
            const size_t buffer_duration_s = 10;  // 10s window (reasonable latency)
            const size_t overlap_duration_s = 5;  // 5s overlap for MORE backward context (no latency impact!)
            const size_t emit_boundary_s = buffer_duration_s - overlap_duration_s;  // Emit first 5s
            const int64_t emit_boundary_ms = emit_boundary_s * 1000;
            const size_t max_buffer_samples = target_hz * buffer_duration_s;
            
            std::vector<int16_t> acc16k;
            acc16k.reserve(max_buffer_samples);
            int64_t buffer_start_time_ms = 0;
            
            struct EmittedSegment {
                std::string text;
                int64_t t_start_ms;
                int64_t t_end_ms;
                int speaker_id;
            };
            std::vector<EmittedSegment> all_segments;
            std::vector<EmittedSegment> held_segments;  // Segments in overlap zone (5-10s)
            int64_t last_emitted_end_ms = 0;  // Track last emitted timestamp to prevent duplicates
            
            // Phase 2: Continuous frame analyzer (PARALLEL)
            diar::ContinuousFrameAnalyzer::Config frame_config;
            frame_config.hop_ms = 250;
            frame_config.window_ms = 1000;
            frame_config.history_sec = 60;
            frame_config.verbose = verbose;
            frame_config.embedding_mode = diar::EmbeddingMode::NeuralONNX;
            diar::ContinuousFrameAnalyzer frame_analyzer(target_hz, frame_config);
            
            if (verbose) {
                fprintf(stderr, "[Stream] 10s buffer, emit first 5s only, 5s overlap for MAXIMUM backward context\n");
            }
            
            while (true) {
                audio::AudioQueue::Chunk chunk;
                if (!audioQueue.pop(chunk)) {
                    break;
                }
                
                // Resample to 16kHz (should be no-op if already 16kHz)
                auto t_res0 = std::chrono::steady_clock::now();
                auto ds = resample_to_16k(chunk.samples, chunk.sample_rate);
                auto t_res1 = std::chrono::steady_clock::now();
                perf.add_resample(std::chrono::duration<double>(t_res1 - t_res0).count());
                
                // Save resampled audio for debugging
                if (recordResampled && wavResampledOut.out) {
                    wavResampledOut.write(ds.data(), ds.size());
                }
                
                // Accumulate audio
                acc16k.insert(acc16k.end(), ds.begin(), ds.end());
                
                // Feed to frame analyzer (parallel)
                frame_analyzer.add_audio(ds.data(), ds.size());
                
                if (verbose) { std::cerr << "." << std::flush; }
                
                // Process when buffer is full
                if (acc16k.size() >= max_buffer_samples) {
                    // Gate mostly-silent buffers
                    double sum2 = 0.0;
                    for (auto s : acc16k) { double v = s / 32768.0; sum2 += v*v; }
                    double rms = std::sqrt(sum2 / std::max<size_t>(1, acc16k.size()));
                    double dbfs = (rms > 0) ? 20.0 * std::log10(rms) : -120.0;
                    
                    if (dbfs > -55.0) {
                        // Transcribe the full buffer - let Whisper segment naturally
                        auto t_w0 = std::chrono::steady_clock::now();
                        auto whisper_segments = whisper.transcribe_chunk_segments(acc16k.data(), acc16k.size());
                        auto t_w1 = std::chrono::steady_clock::now();
                        perf.add_whisper(std::chrono::duration<double>(t_w1 - t_w0).count());
                        
                        if (verbose) {
                            fprintf(stderr, "\n[Whisper] Buffer %lldms-%lldms: %zu segments\n",
                                    buffer_start_time_ms, buffer_start_time_ms + (acc16k.size() * 1000) / target_hz,
                                    whisper_segments.size());
                        }
                        
                        // Only emit segments that END before the emit boundary (5s mark)
                        // This ensures we never re-transcribe the same audio in next window
                        // The 5-10s segments are HELD and will be emitted when next window starts
                        for (const auto& wseg : whisper_segments) {
                            if (wseg.text.empty()) continue;
                            
                            // Calculate segment timestamps FIRST (needed for both emit and hold)
                            int64_t seg_start_ms = buffer_start_time_ms + wseg.t0_ms;
                            int64_t seg_end_ms = buffer_start_time_ms + wseg.t1_ms;
                            
                            // Compute speaker for this segment FIRST (needed for both emit and hold)
                            int sid = -1;
                            if (enable_diar) {
                                int start_sample = (wseg.t0_ms * target_hz) / 1000;
                                int end_sample = (wseg.t1_ms * target_hz) / 1000;
                                start_sample = std::max(0, std::min(start_sample, (int)acc16k.size()));
                                end_sample = std::max(start_sample, std::min(end_sample, (int)acc16k.size()));
                                
                                if (end_sample - start_sample > target_hz / 2) {
                                    auto t_d0 = std::chrono::steady_clock::now();
                                    auto emb = diar::compute_speaker_embedding(
                                        acc16k.data() + start_sample,
                                        end_sample - start_sample,
                                        target_hz
                                    );
                                    sid = spk.assign(emb);
                                    auto t_d1 = std::chrono::steady_clock::now();
                                    perf.add_diar(std::chrono::duration<double>(t_d1 - t_d0).count());
                                }
                            }
                            
                            // CRITICAL: Skip segments that were already emitted in previous window
                            // This handles Whisper's re-segmentation across window boundaries
                            if (seg_end_ms <= last_emitted_end_ms) {
                                // Segment entirely before what we've already emitted - skip
                                if (verbose) {
                                    fprintf(stderr, "[SKIP %.2f-%.2f] %s (already emitted in previous window)\n",
                                            seg_start_ms/1000.0, seg_end_ms/1000.0, wseg.text.c_str());
                                }
                                continue;
                            }
                            
                            // Only emit if segment ends BEFORE the emit boundary (5s mark)
                            if (wseg.t1_ms >= emit_boundary_ms) {
                                // HOLD this segment - it's in overlap zone (5-10s)
                                // We'll emit it when window slides (it won't be re-transcribed)
                                EmittedSegment held;
                                held.text = wseg.text;
                                held.t_start_ms = seg_start_ms;
                                held.t_end_ms = seg_end_ms;
                                held.speaker_id = sid;
                                held_segments.push_back(held);
                                
                                if (verbose) {
                                    fprintf(stderr, "[HOLD %.2f-%.2f] %s (in overlap, will emit on slide)\n",
                                            wseg.t0_ms/1000.0, wseg.t1_ms/1000.0, wseg.text.c_str());
                                }
                                continue;
                            }
                            
                            // Emit this segment immediately
                            EmittedSegment seg;
                            seg.text = wseg.text;
                            seg.t_start_ms = seg_start_ms;
                            seg.t_end_ms = seg_end_ms;
                            seg.speaker_id = sid;
                            all_segments.push_back(seg);
                            last_emitted_end_ms = std::max(last_emitted_end_ms, seg_end_ms);  // Track for duplicate prevention
                            
                            if (verbose) {
                                std::lock_guard<std::mutex> lock(print_mtx);
                                fprintf(stderr, "[EMIT S%d %.2f-%.2f] %s\n",
                                        sid, seg_start_ms/1000.0, seg_end_ms/1000.0, seg.text.c_str());
                            }
                        }
                    }
                    
                    // BEFORE sliding: Emit held segments from previous window
                    // (They're now confirmed and won't be re-transcribed)
                    for (const auto& held : held_segments) {
                        all_segments.push_back(held);
                        last_emitted_end_ms = std::max(last_emitted_end_ms, held.t_end_ms);  // Track for duplicate prevention
                        if (verbose) {
                            fprintf(stderr, "[EMIT-HELD S%d %.2f-%.2f] %s\n",
                                    held.speaker_id, held.t_start_ms/1000.0, held.t_end_ms/1000.0, held.text.c_str());
                        }
                    }
                    held_segments.clear();  // Clear for next window
                    
                    // Slide the window: keep last 5s as context for next iteration
                    // This gives MAXIMUM backward context without adding latency
                    const size_t overlap_samples = target_hz * overlap_duration_s;
                    if (acc16k.size() > overlap_samples) {
                        size_t discard = acc16k.size() - overlap_samples;
                        buffer_start_time_ms += (discard * 1000) / target_hz;
                        std::vector<int16_t> tail(acc16k.end() - overlap_samples, acc16k.end());
                        acc16k.swap(tail);
                    } else {
                        buffer_start_time_ms += (acc16k.size() * 1000) / target_hz;
                        acc16k.clear();
                    }
                }
            }
            
            // Final flush: FIRST emit any held segments from last window
            if (!held_segments.empty()) {
                fprintf(stderr, "\n[Final Flush] Emitting %zu held segments\n", held_segments.size());
                for (const auto& held : held_segments) {
                    all_segments.push_back(held);
                    if (verbose) {
                        fprintf(stderr, "[EMIT-HELD S%d %.2f-%.2f] %s\n",
                                held.speaker_id, held.t_start_ms/1000.0, held.t_end_ms/1000.0, held.text.c_str());
                    }
                }
                held_segments.clear();
            }
            
            // Final flush: process any remaining audio in buffer
            // IMPORTANT: Skip overlap samples - they were already transcribed in last window!
            const size_t overlap_samples = target_hz * overlap_duration_s;
            size_t flush_start_sample = std::min(overlap_samples, acc16k.size());
            size_t flush_sample_count = (acc16k.size() > flush_start_sample) ? (acc16k.size() - flush_start_sample) : 0;
            
            if (flush_sample_count >= target_hz / 2) {  // At least 0.5 second of NEW audio
                fprintf(stderr, "\n[Final Flush] Processing remaining %.2fs in buffer (skipping %.2fs overlap)\n",
                        flush_sample_count / (float)target_hz, flush_start_sample / (float)target_hz);
                
                // Calculate where the NEW audio starts in time
                int64_t flush_start_time_ms = buffer_start_time_ms + (flush_start_sample * 1000) / target_hz;
                
                const int16_t* flush_data = acc16k.data() + flush_start_sample;
                
                double sum2 = 0.0;
                for (size_t i = 0; i < flush_sample_count; i++) {
                    double v = flush_data[i] / 32768.0;
                    sum2 += v*v;
                }
                double rms = std::sqrt(sum2 / std::max<size_t>(1, flush_sample_count));
                double dbfs = (rms > 0) ? 20.0 * std::log10(rms) : -120.0;
                
                if (dbfs > -55.0) {  // Not silence
                    auto whisper_segments = whisper.transcribe_chunk_segments(flush_data, flush_sample_count);
                    
                    for (const auto& wseg : whisper_segments) {
                        if (wseg.text.empty()) continue;
                        
                        int64_t seg_start_ms = flush_start_time_ms + wseg.t0_ms;
                        int64_t seg_end_ms = flush_start_time_ms + wseg.t1_ms;
                        
                        // Compute speaker (within the flushed NEW audio region)
                        int sid = -1;
                        if (enable_diar) {
                            int start_sample = (wseg.t0_ms * target_hz) / 1000;
                            int end_sample = (wseg.t1_ms * target_hz) / 1000;
                            start_sample = std::max(0, std::min(start_sample, (int)flush_sample_count));
                            end_sample = std::max(start_sample, std::min(end_sample, (int)flush_sample_count));
                            
                            if (end_sample - start_sample > target_hz / 2) {
                                auto emb = diar::compute_speaker_embedding(
                                    flush_data + start_sample,
                                    end_sample - start_sample,
                                    target_hz
                                );
                                sid = spk.assign(emb);
                            }
                        }
                        
                        EmittedSegment seg;
                        seg.text = wseg.text;
                        seg.t_start_ms = seg_start_ms;
                        seg.t_end_ms = seg_end_ms;
                        seg.speaker_id = sid;
                        all_segments.push_back(seg);
                        
                        if (verbose) {
                            fprintf(stderr, "[FLUSH S%d %.2f-%.2f] %s\n",
                                    sid, seg_start_ms/1000.0, seg_end_ms/1000.0, seg.text.c_str());
                        }
                    }
                }
            }
            
            // Phase 2: Cluster frames for speaker assignment
            fprintf(stderr, "\n[Phase2] Clustering %zu frames...\n", frame_analyzer.frame_count());
            
            if (frame_analyzer.frame_count() > 0 && enable_diar) {
                frame_analyzer.cluster_frames(2, 0.35f);
            }
            
            // Reassign speakers to ALL segments using frame clustering
            if (enable_diar) {
                fprintf(stderr, "[Phase2] Reassigning speakers to %zu segments...\n", all_segments.size());
                
                // Map frame-level speakers to emitted segments
                for (auto& seg : all_segments) {
                    auto frames = frame_analyzer.get_frames_in_range(seg.t_start_ms, seg.t_end_ms);
                    
                    if (!frames.empty()) {
                        // Simple majority vote
                        std::map<int, int> votes;
                        for (const auto& frame : frames) {
                            votes[frame.speaker_id]++;
                        }
                        
                        int best_speaker = -1;
                        int best_count = 0;
                        for (const auto& [spk, count] : votes) {
                            if (count > best_count) {
                                best_count = count;
                                best_speaker = spk;
                            }
                        }
                        seg.speaker_id = best_speaker;
                    }
                }
            }
            
            // Output final segments
            fprintf(stderr, "\n\n=== Transcription with Speaker Diarization ===\n\n");
            for (const auto& seg : all_segments) {
                std::lock_guard<std::mutex> lock(print_mtx);
                fprintf(stderr, "[S%d] %s\n", seg.speaker_id, seg.text.c_str());
            }
            
            fprintf(stderr, "\n\n[Phase2] Frame statistics:\n");
            fprintf(stderr, "  - Total frames extracted: %zu\n", frame_analyzer.frame_count());
            fprintf(stderr, "  - Segments emitted: %zu\n", all_segments.size());
            
            processing_done = true;
        });
    }

    // Main thread: audio capture and playback (never blocks on processing)
    auto t0 = std::chrono::steady_clock::now();
    auto audio_start_time = t0; // Track when audio playback started
    uint64_t audio_frames_played = 0; // Track how many frames we've sent to speaker
    
    while (true) {
        std::vector<int16_t> chunk;
        if (useFile) chunk = fileCap.read_chunk();
        else chunk = micCap.read_chunk();

        if (!chunk.empty()) {
            if (recordMic && wavOut.out) {
                wavOut.write(chunk.data(), chunk.size());
            }
            
            // Play audio immediately (with real-time pacing for file mode)
            if (useFile && play_file) {
                // Before playing, wait if we're ahead of real-time
                audio_frames_played += chunk.size();
                double audio_time = static_cast<double>(audio_frames_played) / static_cast<double>(in_sr);
                auto target_time = audio_start_time + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(audio_time));
                auto now = std::chrono::steady_clock::now();
                if (now < target_time) {
                    std::this_thread::sleep_until(target_time);
                }
                
                speaker.write(reinterpret_cast<const short*>(chunk.data()), 
                            static_cast<unsigned long long>(chunk.size()));
            }
            
            if (print_levels) {
                double sum2 = 0.0;
                for (auto s : chunk) { double v = s / 32768.0; sum2 += v*v; }
                double rms = std::sqrt(sum2 / std::max<size_t>(1, chunk.size()));
                double dbfs = (rms > 0) ? 20.0 * std::log10(rms) : -120.0;
                std::cerr << "[level] " << std::fixed << std::setprecision(1) << dbfs << " dBFS\n";
            }
            
            // Save chunk size before move
            size_t chunk_size = chunk.size();
            
            // Queue for processing (never blocks - drops oldest if full)
            if (enable_asr) {
                audio::AudioQueue::Chunk qchunk;
                qchunk.samples = std::move(chunk);
                qchunk.sample_rate = in_sr;
                audioQueue.push(std::move(qchunk)); // Never blocks
            }
            
            if (useFile) {
                processed_in_samples += chunk_size;
            }
        } else if (useFile) {
            // EOF
            break;
        }

        // Check limits
        if (limit_sec > 0) {
            if (useFile) {
                if (processed_in_samples >= static_cast<uint64_t>(limit_sec) * static_cast<uint64_t>(in_sr)) {
                    break;
                }
            } else {
                auto now = std::chrono::steady_clock::now();
                double elapsed = std::chrono::duration<double>(now - t0).count();
                if (elapsed >= limit_sec) break;
            }
        }

        // Throttle only for mic
        if (!useFile) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    
    // Signal processing thread to stop
    audioQueue.stop();
    if (processing_thread.joinable()) {
        processing_thread.join();
    }
    
    if (recordMic && wavOut.out) wavOut.close();
    if (recordResampled && wavResampledOut.out) {
        wavResampledOut.close();
        std::cerr << "[save] resampled audio saved to: output/whisper_input_16k.wav\n";
    }
    if (!useFile) micCap.stop(); else fileCap.stop();
    if (useFile && play_file) speaker.stop();
    if (verbose) std::cerr << "\n";
    
    // Performance summary
    auto t1 = std::chrono::steady_clock::now();
    double wall = std::chrono::duration<double>(t1 - t0).count();
    double audio_sec = useFile ? (processed_in_samples.load() / double(std::max(1, in_sr))) : wall;
    double rt_factor = (wall > 0) ? (audio_sec / wall) : 0.0;
    
    double resample_acc, diar_acc, whisper_acc;
    perf.get(resample_acc, diar_acc, whisper_acc);
    
    size_t dropped = audioQueue.dropped_count();
    
    std::cerr << "\n[perf] audio_sec=" << std::fixed << std::setprecision(3) << audio_sec
              << ", wall_sec=" << wall
              << ", xRealtime=" << rt_factor
              << ", t_resample=" << resample_acc
              << ", t_diar=" << diar_acc
              << ", t_whisper=" << whisper_acc;
    if (dropped > 0) {
        std::cerr << "\n[warn] " << dropped << " chunks dropped (processing too slow)";
    }
    std::cerr << "\n";
    std::cerr << "[done] exit=0\n";
    return 0;
}
