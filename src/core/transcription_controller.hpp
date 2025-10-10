#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace core {

/**
 * @brief Represents a transcribed segment with speaker identification
 */
struct TranscriptionSegment {
    std::string text;           ///< Transcribed text
    int64_t start_ms;           ///< Start timestamp in milliseconds
    int64_t end_ms;             ///< End timestamp in milliseconds
    int speaker_id;             ///< Speaker ID (-1 if unknown)
    
    int64_t duration_ms() const { return end_ms - start_ms; }
};

/**
 * @brief Speaker statistics for tracking speaking time
 */
struct SpeakerStats {
    int speaker_id;
    int64_t total_speaking_time_ms;  ///< Total time this speaker has spoken
    int segment_count;                ///< Number of segments from this speaker
    std::string last_text;            ///< Last thing this speaker said
    
    SpeakerStats() : speaker_id(-1), total_speaking_time_ms(0), segment_count(0) {}
    SpeakerStats(int id) : speaker_id(id), total_speaking_time_ms(0), segment_count(0) {}
};

/**
 * @brief Controller for real-time transcription with speaker diarization
 * 
 * This controller implements the proven streaming strategy:
 * - 10s sliding window with 5s overlap (maximum backward context)
 * - Hold-and-emit to prevent re-transcription
 * - Beam search (beam_size=5) for quality
 * - Timestamp-based duplicate prevention
 * - Speaker identification and time tracking
 * 
 * Event-based API allows real-time updates to GUI.
 */
class TranscriptionController {
public:
    /**
     * @brief Callback for new transcription segments
     * Called when a segment is ready to be displayed
     */
    using SegmentCallback = std::function<void(const TranscriptionSegment&)>;
    
    /**
     * @brief Callback for speaker statistics updates
     * Called periodically as speaking times change
     */
    using StatsCallback = std::function<void(const std::vector<SpeakerStats>&)>;
    
    /**
     * @brief Callback for status updates
     * @param message Status message
     * @param is_error True if this is an error message
     */
    using StatusCallback = std::function<void(const std::string& message, bool is_error)>;
    
    /**
     * @brief Configuration for transcription controller
     */
    struct Config {
        std::string model_path;         ///< Path to Whisper model file
        std::string language = "en";    ///< Language code
        int n_threads = 0;              ///< Number of threads (0 = auto)
        
        // Streaming parameters
        size_t buffer_duration_s = 10;  ///< Window size in seconds
        size_t overlap_duration_s = 5;  ///< Overlap for context
        
        // Diarization parameters
        bool enable_diarization = true; ///< Enable speaker identification
        int max_speakers = 2;           ///< Maximum number of speakers
        float speaker_threshold = 0.35f; ///< Cosine similarity threshold
        
        // Callbacks
        SegmentCallback on_segment;     ///< Called when new segment ready
        StatsCallback on_stats;         ///< Called when stats update
        StatusCallback on_status;       ///< Called for status messages
    };
    
    TranscriptionController();
    ~TranscriptionController();
    
    /**
     * @brief Initialize the controller with configuration
     * @param config Configuration settings
     * @return true if initialization succeeded
     */
    bool initialize(const Config& config);
    
    /**
     * @brief Start transcription (begins processing audio)
     * @return true if started successfully
     */
    bool start();
    
    /**
     * @brief Stop transcription (finishes processing remaining audio)
     */
    void stop();
    
    /**
     * @brief Pause transcription (keeps processing but doesn't emit)
     */
    void pause();
    
    /**
     * @brief Resume transcription after pause
     */
    void resume();
    
    /**
     * @brief Check if controller is currently running
     */
    bool is_running() const { return running_.load(); }
    
    /**
     * @brief Check if controller is paused
     */
    bool is_paused() const { return paused_.load(); }
    
    /**
     * @brief Add audio samples for processing
     * @param samples Audio data (16-bit PCM)
     * @param sample_count Number of samples
     * @param sample_rate Sample rate (will be resampled to 16kHz)
     */
    void add_audio(const int16_t* samples, size_t sample_count, int sample_rate);
    
    /**
     * @brief Get all transcribed segments so far
     */
    std::vector<TranscriptionSegment> get_all_segments() const;
    
    /**
     * @brief Get speaker statistics
     */
    std::vector<SpeakerStats> get_speaker_stats() const;
    
    /**
     * @brief Get total transcription time (from first to last segment)
     */
    int64_t get_total_time_ms() const;
    
    /**
     * @brief Clear all transcription data (reset)
     */
    void clear();
    
    /**
     * @brief Get performance metrics
     */
    struct PerformanceMetrics {
        double realtime_factor;       ///< <1.0 = faster than realtime
        double whisper_time_s;        ///< Total Whisper processing time
        double diarization_time_s;    ///< Total diarization time
        size_t segments_processed;    ///< Number of segments processed
        size_t windows_processed;     ///< Number of windows processed
        size_t dropped_frames;        ///< Number of audio frames dropped
    };
    PerformanceMetrics get_performance_metrics() const;

private:
    class Impl;  // Forward declaration for PIMPL
    std::unique_ptr<Impl> impl_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
};

} // namespace core
