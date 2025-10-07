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
    if (useFile) {
        if (!fileCap.start_from_wav(path)) {
            std::cerr << "[input] failed to open WAV: " << path << "\n";
            return 1;
        }
        in_sr = fileCap.sample_rate();
        std::cerr << "[input] file: " << path
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
            // ORIGINAL PROVEN SEGMENTATION STRATEGY
            // - Accumulate audio in acc16k buffer
            // - Transcribe when window is full (1.5s)
            // - Keep 0.5s overlap for context
            // DO NOT MODIFY - see specs/architecture.md for why this works
            
            std::vector<int16_t> acc16k;
            diar::SpeakerClusterer spk(2, 0.45f);  // Lowered from 0.60 to make 2nd speaker easier to create
            Deduper dedup;
            const int target_hz = 16000;
            const size_t window_samples = static_cast<size_t>(target_hz * 1.5);
            const size_t overlap_samples = static_cast<size_t>(target_hz * 0.5);
            
            // Track segments for frame-based speaker assignment
            struct SegmentInfo {
                std::string text;
                int64_t t_start_ms;
                int64_t t_end_ms;
                int speaker_id;  // Will be assigned from frames
            };
            std::vector<SegmentInfo> segments;
            int64_t audio_time_ms = 0;  // Track position in audio stream
            
            // Phase 2: Continuous frame analyzer (PARALLEL - does not affect Whisper)
            diar::ContinuousFrameAnalyzer::Config frame_config;
            frame_config.hop_ms = 250;      // 4 frames per second
            frame_config.window_ms = 1000;  // 1s window for each embedding
            frame_config.history_sec = 60;  // Keep last 60s
            frame_config.verbose = verbose;
            diar::ContinuousFrameAnalyzer frame_analyzer(target_hz, frame_config);
            
            if (verbose) {
                fprintf(stderr, "[Phase2] Frame analyzer: hop=%dms, window=%dms\n",
                        frame_config.hop_ms, frame_config.window_ms);
            }
            
            while (true) {
                audio::AudioQueue::Chunk chunk;
                if (!audioQueue.pop(chunk)) {
                    break; // Queue stopped
                }
                
                // Resample to 16kHz
                auto t_res0 = std::chrono::steady_clock::now();
                auto ds = resample_to_16k(chunk.samples, chunk.sample_rate);
                auto t_res1 = std::chrono::steady_clock::now();
                perf.add_resample(std::chrono::duration<double>(t_res1 - t_res0).count());
                
                // Original: Accumulate in acc16k
                acc16k.insert(acc16k.end(), ds.begin(), ds.end());
                
                // Phase 2 (PARALLEL): Feed to frame analyzer (read-only, doesn't affect Whisper)
                int new_frames = frame_analyzer.add_audio(ds.data(), ds.size());
                if (verbose && new_frames > 0) {
                    fprintf(stderr, "[+%d] ", new_frames);
                }
                
                if (verbose) { std::cerr << "." << std::flush; }
                
                // Process window if ready (ORIGINAL LOGIC - DO NOT MODIFY)
                if (acc16k.size() >= window_samples) {
                    if (verbose) {
                        fprintf(stderr, "\n[DEBUG] Processing window: acc16k.size()=%zu, window_samples=%zu\n",
                                acc16k.size(), window_samples);
                    }
                    
                    // Gate mostly-silent windows
                    double sum2 = 0.0;
                    for (auto s : acc16k) { double v = s / 32768.0; sum2 += v*v; }
                    double rms = std::sqrt(sum2 / std::max<size_t>(1, acc16k.size()));
                    double dbfs = (rms > 0) ? 20.0 * std::log10(rms) : -120.0;
                    
                    if (verbose) {
                        fprintf(stderr, "[DEBUG] dbfs=%.2f (threshold=-55.0)\n", dbfs);
                    }
                    
                    std::string txt;
                    if (dbfs > -55.0) {
                        int sid = -1;
                        if (enable_diar) {
                            auto t_d0 = std::chrono::steady_clock::now();
                            // Use 40 mel bands for better speaker discrimination
                            auto emb = diar::compute_speaker_embedding(acc16k.data(), acc16k.size(), target_hz);
                            sid = spk.assign(emb);
                            auto t_d1 = std::chrono::steady_clock::now();
                            perf.add_diar(std::chrono::duration<double>(t_d1 - t_d0).count());
                        }
                        
                        // Whisper
                        auto t_w0 = std::chrono::steady_clock::now();
                        std::string segTxt = whisper.transcribe_chunk(acc16k.data(), acc16k.size());
                        auto t_w1 = std::chrono::steady_clock::now();
                        perf.add_whisper(std::chrono::duration<double>(t_w1 - t_w0).count());
                        
                        // Remove overlap-duplicated prefix words
                        std::string merged = dedup.merge(segTxt);
                        
                        if (verbose) {
                            fprintf(stderr, "[DEBUG] Whisper returned: '%s', merged: '%s'\n", 
                                    segTxt.c_str(), merged.c_str());
                        }
                        
                        // Store segment with timing for frame-based speaker assignment
                        if (!merged.empty()) {
                            SegmentInfo seg;
                            seg.text = merged;
                            seg.t_start_ms = audio_time_ms;
                            seg.t_end_ms = audio_time_ms + (acc16k.size() * 1000) / target_hz;
                            seg.speaker_id = sid;  // Temporary, will be reassigned from frames
                            segments.push_back(seg);
                            
                            if (verbose) {
                                std::lock_guard<std::mutex> lock(print_mtx);
                                fprintf(stderr, "\n[temp S%d] %s", sid, merged.c_str());
                                fflush(stderr);
                            }
                        }
                    }
                    
                    // Update audio time position
                    audio_time_ms += (acc16k.size() * 1000) / target_hz;
                    
                    // Keep overlap (don't update audio_time_ms for overlap portion)
                    if (acc16k.size() > overlap_samples) {
                        std::vector<int16_t> tail(acc16k.end() - overlap_samples, acc16k.end());
                        acc16k.swap(tail);
                    } else {
                        acc16k.clear();
                    }
                }
            }
            
            // Final flush if we have a full window (ORIGINAL LOGIC)
            if (acc16k.size() >= window_samples) {
                int sid = -1;
                if (enable_diar) {
                    // Use 40 mel bands for better speaker discrimination
                    auto emb = diar::compute_speaker_embedding(acc16k.data(), acc16k.size(), target_hz);
                    sid = spk.assign(emb);
                }
                auto segTxt = whisper.transcribe_chunk(acc16k.data(), acc16k.size());
                auto merged = dedup.merge(segTxt);
                
                if (!merged.empty()) {
                    SegmentInfo seg;
                    seg.text = merged;
                    seg.t_start_ms = audio_time_ms;
                    seg.t_end_ms = audio_time_ms + (acc16k.size() * 1000) / target_hz;
                    seg.speaker_id = sid;
                    segments.push_back(seg);
                }
            }
            
            // Phase 2b: Cluster frames and reassign speakers
            fprintf(stderr, "\n\n[Phase2] Clustering %zu frames...\n", frame_analyzer.frame_count());
            fprintf(stderr, "[DEBUG] Before clustering: segments.size()=%zu\n", segments.size());
            
            if (frame_analyzer.frame_count() > 0) {
                frame_analyzer.cluster_frames(2, 0.50f);  // max_speakers=2, threshold=0.50
                
                fprintf(stderr, "[Phase2] Reassigning speakers to %zu segments...\n", segments.size());
                
                // Reassign speaker IDs to segments based on frames
                int seg_num = 0;
                for (auto& seg : segments) {
                    auto frames = frame_analyzer.get_frames_in_range(seg.t_start_ms, seg.t_end_ms);
                    
                    if (seg_num < 3) {
                        fprintf(stderr, "[seg %d] t=[%lld, %lld]ms, found %zu frames\n", 
                                seg_num, seg.t_start_ms, seg.t_end_ms, frames.size());
                    }
                    
                    if (!frames.empty()) {
                        // Majority vote from frames
                        std::map<int, int> vote_counts;
                        for (const auto& frame : frames) {
                            if (frame.speaker_id >= 0) {
                                vote_counts[frame.speaker_id]++;
                            }
                        }
                        
                        if (seg_num < 3) {
                            fprintf(stderr, "  vote_counts: ");
                            for (const auto& pair : vote_counts) {
                                fprintf(stderr, "S%d=%d ", pair.first, pair.second);
                            }
                            fprintf(stderr, "\n");
                        }
                        
                        int best_speaker = -1;
                        int max_votes = 0;
                        for (const auto& pair : vote_counts) {
                            if (pair.second > max_votes) {
                                max_votes = pair.second;
                                best_speaker = pair.first;
                            }
                        }
                        
                        if (best_speaker >= 0) {
                            seg.speaker_id = best_speaker;
                        }
                    }
                    seg_num++;
                }
            }
            
            // Output all segments with final speaker assignments
            fprintf(stderr, "\n=== Transcription with Speaker Diarization ===\n");
            for (const auto& seg : segments) {
                if (seg.speaker_id >= 0) {
                    fprintf(stderr, "\n[S%d] %s", seg.speaker_id, seg.text.c_str());
                } else {
                    fprintf(stderr, "\n%s", seg.text.c_str());
                }
            }
            fprintf(stderr, "\n");
            
            // Phase 2: Frame statistics
            fprintf(stderr, "\n\n[Phase2] Frame statistics:\n");
            fprintf(stderr, "  - Total frames extracted: %zu\n", frame_analyzer.frame_count());
            fprintf(stderr, "  - Coverage duration: %.1fs\n", frame_analyzer.duration_ms() / 1000.0);
            fprintf(stderr, "  - Frames per second: %.1f\n", 
                    frame_analyzer.duration_ms() > 0 
                        ? (frame_analyzer.frame_count() * 1000.0) / frame_analyzer.duration_ms()
                        : 0.0);
            fprintf(stderr, "\n");
            
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
