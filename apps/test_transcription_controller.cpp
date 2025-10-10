#include "core/transcription_controller.hpp"
#include "audio/audio_input_device.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

// Global counters for tracking
std::atomic<int> total_segments{0};
std::atomic<size_t> total_audio_samples{0};
std::mutex console_mutex;
std::chrono::steady_clock::time_point start_time;

void print_segment(const core::TranscriptionSegment& seg) {
    std::lock_guard<std::mutex> lock(console_mutex);
    std::cout << "\n[" << (seg.start_ms / 1000.0) << "s -> " << (seg.end_ms / 1000.0) << "s] ";
    std::cout << "Speaker " << seg.speaker_id << ": " << seg.text << "\n";
    total_segments++;
}

void print_stats(const std::vector<core::SpeakerStats>& stats) {
    // Only print every few updates to avoid spamming console
    static int update_count = 0;
    if (++update_count % 5 != 0) return;
    
    std::lock_guard<std::mutex> lock(console_mutex);
    std::cout << "\n--- Speaker Statistics ---\n";
    for (const auto& s : stats) {
        std::cout << "  Speaker " << s.speaker_id << ": "
                  << (s.total_speaking_time_ms / 1000.0) << "s ("
                  << s.segment_count << " segments)\n";
        if (!s.last_text.empty()) {
            std::cout << "    Last: \"" << s.last_text << "\"\n";
        }
    }
}

void print_status(const std::string& msg, bool is_error) {
    std::lock_guard<std::mutex> lock(console_mutex);
    if (is_error) {
        std::cerr << "[ERROR] " << msg << "\n";
    } else {
        std::cout << "[INFO] " << msg << "\n";
    }
}

int main(int argc, char** argv) {
    std::cout << "=== TranscriptionController Test ===\n\n";
    
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <model_path> <audio_file.wav> [--limit-seconds N]\n";
        std::cerr << "Example: " << argv[0] << " models/ggml-base.en.bin test_data/Sean_Carroll_podcast.wav --limit-seconds 20\n";
        return 1;
    }
    
    std::string model_path = argv[1];
    std::string audio_path = argv[2];
    int limit_seconds = 0;  // 0 = no limit
    
    // Parse optional arguments
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--limit-seconds" && i + 1 < argc) {
            limit_seconds = std::atoi(argv[++i]);
        }
    }
    
    if (limit_seconds > 0) {
        std::cout << "Limiting audio to first " << limit_seconds << " seconds\n\n";
    }
    
    // 1. Create TranscriptionController
    std::cout << "Initializing TranscriptionController...\n";
    core::TranscriptionController controller;
    
    core::TranscriptionController::Config config;
    config.model_path = model_path;
    config.language = "en";
    config.n_threads = 0;  // Auto
    config.buffer_duration_s = 3;  // 3 seconds for faster response
    config.overlap_duration_s = 1;  // 1 second overlap
    config.enable_diarization = true;
    config.max_speakers = 2;
    config.speaker_threshold = 0.35f;
    
    // Set up callbacks
    config.on_segment = print_segment;
    config.on_stats = print_stats;
    config.on_status = print_status;
    
    if (!controller.initialize(config)) {
        std::cerr << "Failed to initialize controller\n";
        return 1;
    }
    
    std::cout << "Controller initialized successfully\n\n";
    
    // 2. Create audio device
    std::cout << "Setting up audio device (synthetic file playback)...\n";
    auto device = audio::AudioInputFactory::create_device("synthetic");
    if (!device) {
        std::cerr << "Failed to create audio device\n";
        return 1;
    }
    
    audio::AudioInputConfig audio_config;
    audio_config.device_id = "synthetic";
    audio_config.synthetic_file_path = audio_path;
    audio_config.synthetic_playback = true;  // Play to speakers
    audio_config.synthetic_loop = false;
    audio_config.buffer_size_ms = 100;
    
    // Track if we should stop due to time limit
    std::atomic<bool> should_stop_playback{false};
    
    // Audio callback: feed to controller
    bool init_ok = device->initialize(
        audio_config,
        // Audio callback
        [&controller, &should_stop_playback, limit_seconds](const int16_t* samples, size_t sample_count, int sample_rate, int channels) {
            total_audio_samples.fetch_add(sample_count);
            
            // Check time limit
            if (limit_seconds > 0) {
                double current_time = total_audio_samples.load() / (double)sample_rate;
                if (current_time >= limit_seconds) {
                    should_stop_playback.store(true);
                    return;  // Stop processing
                }
            }
            
            controller.add_audio(samples, sample_count, sample_rate);
            
            // Print progress every 2 seconds of audio
            static size_t last_print = 0;
            size_t current_samples = total_audio_samples.load();
            if (current_samples - last_print >= sample_rate * 2) {
                double elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - start_time
                ).count();
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cout << "\r[" << std::fixed << std::setprecision(1) 
                          << (current_samples / (double)sample_rate) << "s audio, "
                          << elapsed << "s elapsed] Processing...";
                std::cout.flush();
                last_print = current_samples;
            }
        },
        // Error callback
        [](const std::string& error, bool is_fatal) {
            std::lock_guard<std::mutex> lock(console_mutex);
            std::cerr << "\n[AUDIO " << (is_fatal ? "FATAL" : "ERROR") << "] " << error << "\n";
        }
    );
    
    if (!init_ok) {
        std::cerr << "Failed to initialize audio device\n";
        return 1;
    }
    
    auto device_info = device->get_device_info();
    std::cout << "Audio device: " << device_info.name << "\n";
    std::cout << "  Sample rate: " << device_info.default_sample_rate << " Hz\n";
    std::cout << "  Channels: " << device_info.max_channels << "\n\n";
    
    // 3. Start everything
    std::cout << "Starting transcription (you should hear audio playing)...\n";
    std::cout << "==========================================================\n\n";
    
    start_time = std::chrono::steady_clock::now();
    
    if (!controller.start()) {
        std::cerr << "Failed to start controller\n";
        return 1;
    }
    
    if (!device->start()) {
        std::cerr << "Failed to start audio device\n";
        return 1;
    }
    
    // 4. Wait for audio to finish or time limit
    while (device->is_capturing() && !should_stop_playback.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Stop device if time limit was reached
    if (should_stop_playback.load()) {
        device->stop();
        std::cout << "\n\nTime limit reached, stopping playback...\n";
    } else {
        std::cout << "\n\nAudio playback finished, waiting for processing to complete...\n";
    }
    
    // Give controller time to process remaining audio
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 5. Stop everything
    controller.stop();
    
    auto end_time = std::chrono::steady_clock::now();
    double wall_clock_s = std::chrono::duration<double>(end_time - start_time).count();
    
    // 6. Print comprehensive metrics
    std::cout << "\n==========================================================\n";
    std::cout << "=== Final Results ===\n\n";
    
    // Audio metrics
    double audio_duration_s = total_audio_samples.load() / (double)device_info.default_sample_rate;
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Audio clip length: " << audio_duration_s << "s\n";
    std::cout << "Wall-clock time: " << wall_clock_s << "s\n";
    std::cout << "Overall realtime factor: " << (wall_clock_s / audio_duration_s) << "x\n\n";
    
    // Controller performance metrics
    auto perf = controller.get_performance_metrics();
    std::cout << "--- Transcription Performance ---\n";
    std::cout << "Windows processed: " << perf.windows_processed << "\n";
    std::cout << "Segments transcribed: " << perf.segments_processed << "\n";
    std::cout << "Whisper total time: " << perf.whisper_time_s << "s "
              << "(RTF=" << (perf.whisper_time_s / audio_duration_s) << "x)\n";
    std::cout << "Diarization total time: " << perf.diarization_time_s << "s "
              << "(RTF=" << (perf.diarization_time_s / audio_duration_s) << "x)\n";
    std::cout << "Processing realtime factor: " << perf.realtime_factor << "x\n";
    std::cout << "Dropped frames: " << perf.dropped_frames << "\n\n";
    
    // Speaker statistics
    auto stats = controller.get_speaker_stats();
    std::cout << "--- Speaker Analysis ---\n";
    if (stats.empty()) {
        std::cout << "No speakers detected\n";
    } else {
        for (const auto& s : stats) {
            std::cout << "Speaker " << s.speaker_id << ":\n";
            std::cout << "  Speaking time: " << (s.total_speaking_time_ms / 1000.0) << "s\n";
            std::cout << "  Segments: " << s.segment_count << "\n";
            if (!s.last_text.empty()) {
                std::cout << "  Last: \"" << s.last_text << "\"\n";
            }
        }
    }
    
    std::cout << "\n--- Full Transcription ---\n";
    auto segments = controller.get_all_segments();
    if (segments.empty()) {
        std::cout << "(No segments transcribed)\n";
    } else {
        for (const auto& seg : segments) {
            std::cout << "[" << (seg.start_ms / 1000.0) << "s -> " 
                      << (seg.end_ms / 1000.0) << "s] "
                      << "Speaker " << seg.speaker_id << ": " 
                      << seg.text << "\n";
        }
    }
    
    std::cout << "\n=== Test Complete ===\n";
    
    // Validate performance
    if (perf.realtime_factor > 1.5) {
        std::cout << "\n⚠️  WARNING: Processing slower than 1.5x realtime\n";
        std::cout << "    This may cause audio drops on live input\n";
    } else if (perf.realtime_factor < 1.0) {
        std::cout << "\n✓ Excellent performance: Faster than realtime!\n";
    } else {
        std::cout << "\n✓ Good performance: Within acceptable range\n";
    }
    
    return 0;
}
