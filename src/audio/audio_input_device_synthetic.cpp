#include "audio_input_device_synthetic.hpp"
#include <chrono>
#include <thread>
#include <algorithm>

namespace audio {

AudioInputDevice_Synthetic::AudioInputDevice_Synthetic() = default;

AudioInputDevice_Synthetic::~AudioInputDevice_Synthetic() {
    stop();
}

bool AudioInputDevice_Synthetic::initialize(
    const AudioInputConfig& config,
    AudioCallback audio_callback,
    ErrorCallback error_callback
) {
    config_ = config;
    audio_callback_ = audio_callback;
    error_callback_ = error_callback;
    
    // Load WAV file using existing FileCapture
    if (config.synthetic_file_path.empty()) {
        if (error_callback_) {
            error_callback_("Synthetic device requires synthetic_file_path", true);
        }
        return false;
    }
    
    if (!file_capture_.start_from_wav(config.synthetic_file_path)) {
        if (error_callback_) {
            error_callback_("Failed to load WAV file: " + config.synthetic_file_path, true);
        }
        return false;
    }
    
    // Setup playback if requested
    if (config.synthetic_playback) {
        if (playback_out_.start(file_capture_.sample_rate(), 1)) {
            playback_enabled_ = true;
        }
    }
    
    return true;
}

bool AudioInputDevice_Synthetic::start() {
    if (is_capturing_.load()) {
        return true;  // Already capturing
    }
    
    should_stop_.store(false);
    is_capturing_.store(true);
    
    capture_thread_ = std::make_unique<std::thread>(
        &AudioInputDevice_Synthetic::capture_thread_func, this
    );
    
    return true;
}

void AudioInputDevice_Synthetic::stop() {
    if (!is_capturing_.load()) {
        return;
    }
    
    should_stop_.store(true);
    
    if (capture_thread_ && capture_thread_->joinable()) {
        capture_thread_->join();
    }
    
    is_capturing_.store(false);
    capture_thread_.reset();
    
    if (playback_enabled_) {
        playback_out_.stop();
        playback_enabled_ = false;
    }
}

void AudioInputDevice_Synthetic::capture_thread_func() {
    auto next_callback_time = std::chrono::steady_clock::now();
    
    while (!should_stop_.load()) {
        // Read chunk from file (FileCapture returns ~20ms chunks)
        auto chunk = file_capture_.read_chunk();
        
        if (chunk.empty()) {
            if (config_.synthetic_loop) {
                // Restart from beginning
                file_capture_.stop();
                if (!file_capture_.start_from_wav(config_.synthetic_file_path)) {
                    if (error_callback_) {
                        error_callback_("Failed to restart file", false);
                    }
                    break;
                }
                continue;
            } else {
                // End of file, stop capturing
                break;
            }
        }
        
        // Play to speakers if enabled
        if (playback_enabled_ && !chunk.empty()) {
            playback_out_.write(chunk.data(), chunk.size());
        }
        
        // Call the audio callback
        if (audio_callback_) {
            audio_callback_(
                chunk.data(), 
                chunk.size(), 
                file_capture_.sample_rate(),
                1  // FileCapture returns mono
            );
        }
        
        // Sleep based on actual chunk duration (simulate real-time playback)
        // FileCapture returns ~20ms chunks, so calculate exact duration
        double chunk_duration_s = static_cast<double>(chunk.size()) / file_capture_.sample_rate();
        auto chunk_duration = std::chrono::duration<double>(chunk_duration_s);
        next_callback_time += std::chrono::duration_cast<std::chrono::steady_clock::duration>(chunk_duration);
        std::this_thread::sleep_until(next_callback_time);
    }
    
    is_capturing_.store(false);
}

AudioDeviceInfo AudioInputDevice_Synthetic::get_device_info() const {
    AudioDeviceInfo info;
    info.id = "synthetic";
    info.name = "Synthetic Device (File: " + config_.synthetic_file_path + ")";
    info.driver = "Synthetic";
    info.default_sample_rate = file_capture_.sample_rate();
    info.max_channels = 1;  // FileCapture returns mono
    info.is_default = false;
    return info;
}

} // namespace audio
