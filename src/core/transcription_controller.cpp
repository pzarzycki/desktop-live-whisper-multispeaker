// Copyright (c) 2025 VAM Desktop Live Whisper
// TranscriptionController - Asynchronous Real-Time Transcription
//
// Architecture (Phase 6 - Async Processing):
// ==========================================
// 
// Audio Thread (non-blocking):
//   - add_audio() called from audio callback
//   - Pushes audio to AudioQueue (instant, never blocks)
//   - Returns immediately
//
// Processing Thread (background):
//   - Pops audio from AudioQueue (blocks waiting for data)
//   - Accumulates into sliding window buffer (3s default)
//   - When buffer full:
//     * Emit held segments from PREVIOUS window
//     * Transcribe ONLY NEW audio (skip overlap zone already transcribed)
//     * Compute speaker embeddings for segments
//     * Hold segments in overlap zone for next window
//     * Slide window forward
//
// Key Features:
//   - Zero audio stuttering (audio thread never blocks)
//   - Fast response (~4s with 3s buffer)
//   - No duplicate text (overlap zone not re-transcribed)
//   - No overlapping timestamps (segments trimmed if needed)
//   - Real-time capable (processing RTF < 1.0x)
//
// See: docs/PHASE6_ASYNC_ARCHITECTURE.md

#include "transcription_controller.hpp"
#include "asr/whisper_backend.hpp"
#include "diar/speaker_cluster.hpp"
#include "audio/audio_queue.hpp"

#include <thread>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <deque>

namespace {
    // Simple linear interpolation resampler to 16kHz
    std::vector<int16_t> resample_to_16k(const int16_t* in, size_t in_samples, int in_hz) {
        const int target = 16000;
        if (in_hz == target || in_hz <= 0 || in_samples == 0) {
            return std::vector<int16_t>(in, in + in_samples);
        }
        const double ratio = static_cast<double>(target) / static_cast<double>(in_hz);
        const size_t out_len = static_cast<size_t>(std::llround(in_samples * ratio));
        std::vector<int16_t> out(out_len);
        for (size_t i = 0; i < out_len; ++i) {
            double src_pos = i / ratio;
            size_t i0 = static_cast<size_t>(src_pos);
            size_t i1 = std::min(i0 + 1, in_samples - 1);
            double frac = src_pos - static_cast<double>(i0);
            double v = (1.0 - frac) * static_cast<double>(in[i0]) + frac * static_cast<double>(in[i1]);
            int vi = static_cast<int>(std::lrint(v));
            vi = std::clamp(vi, -32768, 32767);
            out[i] = static_cast<int16_t>(vi);
        }
        return out;
    }
}
#include <cmath>

namespace core {

/**
 * @brief Internal implementation of TranscriptionController (PIMPL pattern)
 */
class TranscriptionController::Impl {
public:
    Config config;
    
    // Processing components
    std::unique_ptr<asr::WhisperBackend> whisper;
    std::unique_ptr<diar::ContinuousFrameAnalyzer> frame_analyzer;
    std::unique_ptr<diar::SpeakerClusterer> speaker_clusterer;
    
    // Async processing
    audio::AudioQueue audio_queue_{500};  // Queue for incoming audio chunks (large for 20ms chunks)
    std::thread processing_thread_;       // Background processing thread
    std::atomic<bool> running{false};
    
    // Audio buffer (streaming with overlap)
    std::vector<int16_t> audio_buffer;
    int64_t buffer_start_time_ms = 0;
    size_t max_buffer_samples = 0;
    
    // Segment management
    struct HeldSegment {
        std::string text;
        int64_t start_ms;
        int64_t end_ms;
        int speaker_id;
    };
    std::vector<TranscriptionSegment> all_segments;
    std::vector<HeldSegment> held_segments;
    int64_t last_emitted_end_ms = 0;
    
    // Speaker statistics
    std::map<int, SpeakerStats> speaker_stats_map;
    
    // Performance tracking
    double total_whisper_time_s = 0.0;
    double total_diar_time_s = 0.0;
    size_t segments_processed = 0;
    size_t windows_processed = 0;
    size_t dropped_frames = 0;
    
    // Thread safety
    mutable std::mutex segments_mutex;
    mutable std::mutex stats_mutex;
    
    bool initialized = false;
    
    Impl() = default;
    
    bool initialize(const Config& cfg) {
        config = cfg;
        
        // Initialize Whisper
        whisper = std::make_unique<asr::WhisperBackend>();
        if (!whisper->load_model(config.model_path)) {
            if (config.on_status) {
                config.on_status("Failed to load Whisper model: " + config.model_path, true);
            }
            return false;
        }
        
        // Initialize speaker diarization if enabled
        if (config.enable_diarization) {
            diar::ContinuousFrameAnalyzer::Config frame_config;
            frame_config.hop_ms = 250;        // Extract frames every 250ms
            frame_config.window_ms = 1000;    // 1s window for each frame
            frame_analyzer = std::make_unique<diar::ContinuousFrameAnalyzer>(16000, frame_config);
            
            speaker_clusterer = std::make_unique<diar::SpeakerClusterer>(
                config.max_speakers, 
                config.speaker_threshold
            );
        }
        
        // Calculate buffer sizes
        max_buffer_samples = 16000 * config.buffer_duration_s;  // 16kHz sample rate
        audio_buffer.reserve(max_buffer_samples);
        
        initialized = true;
        
        if (config.on_status) {
            config.on_status("Transcription controller initialized", false);
        }
        
        return true;
    }
    
    void add_audio(const int16_t* samples, size_t sample_count, int sample_rate) {
        if (!running.load()) return;
        
        // Push to queue - NEVER BLOCKS
        audio::AudioQueue::Chunk chunk;
        chunk.samples = std::vector<int16_t>(samples, samples + sample_count);
        chunk.sample_rate = sample_rate;
        
        if (!audio_queue_.push(std::move(chunk))) {
            // Queue stopped - increment dropped frames
            dropped_frames++;
        }
    }
    
    // Processing thread function - runs in background
    void processing_loop() {
        while (running.load()) {
            audio::AudioQueue::Chunk chunk;
            
            // Pop from queue (blocks waiting for data)
            if (!audio_queue_.pop(chunk)) {
                break;  // Queue stopped
            }
            
            // Resample to 16kHz if needed
            const int16_t* samples = chunk.samples.data();
            size_t sample_count = chunk.samples.size();
            std::vector<int16_t> resampled;
            
            if (chunk.sample_rate != 16000) {
                resampled = resample_to_16k(samples, sample_count, chunk.sample_rate);
                samples = resampled.data();
                sample_count = resampled.size();
            }
            
            // Add to buffer
            audio_buffer.insert(audio_buffer.end(), samples, samples + sample_count);
            
            // Feed to frame analyzer for diarization (parallel processing)
            if (frame_analyzer) {
                frame_analyzer->add_audio(samples, sample_count);
            }
            
            // Process when buffer is full
            if (audio_buffer.size() >= max_buffer_samples) {
                process_buffer();
            }
        }
        
        // Process any remaining audio in buffer
        if (!audio_buffer.empty()) {
            process_buffer();
        }
        
        // Flush any held segments
        emit_held_segments();
    }
    
    void process_buffer() {
        // First: Emit held segments from PREVIOUS window
        emit_held_segments();
        
        // Determine which part of the buffer is NEW audio (not already transcribed)
        const size_t overlap_samples = 16000 * config.overlap_duration_s;
        size_t transcribe_start_sample = (windows_processed > 0) ? std::min(overlap_samples, audio_buffer.size()) : 0;
        size_t transcribe_sample_count = (audio_buffer.size() > transcribe_start_sample) 
            ? (audio_buffer.size() - transcribe_start_sample) : 0;
        
        // Skip if less than 1 second of NEW audio
        if (transcribe_sample_count < 16000) {
            slide_window();
            return;
        }
        
        // Check if NEW audio is mostly silence
        const int16_t* transcribe_data = audio_buffer.data() + transcribe_start_sample;
        double sum2 = 0.0;
        for (size_t i = 0; i < transcribe_sample_count; i++) {
            double v = transcribe_data[i] / 32768.0;
            sum2 += v * v;
        }
        double rms = std::sqrt(sum2 / std::max<size_t>(1, transcribe_sample_count));
        double dbfs = (rms > 0) ? 20.0 * std::log10(rms) : -120.0;
        
        if (dbfs <= -55.0) {
            // Silence - skip processing, just slide window
            slide_window();
            return;
        }
        
        // Calculate where the NEW audio starts in absolute time
        int64_t transcribe_start_time_ms = buffer_start_time_ms + (transcribe_start_sample * 1000) / 16000;
        
        // Transcribe ONLY the NEW audio (but Whisper sees full buffer for context)
        // We pass full buffer but adjust timestamps to skip overlap
        auto t_start = std::chrono::steady_clock::now();
        auto whisper_segments = whisper->transcribe_chunk_segments(
            transcribe_data,
            transcribe_sample_count
        );
        auto t_end = std::chrono::steady_clock::now();
        total_whisper_time_s += std::chrono::duration<double>(t_end - t_start).count();
        
        windows_processed++;
        
        // Calculate emit boundary (relative to NEW audio, not full buffer)
        const size_t new_audio_duration_s = transcribe_sample_count / 16000;
        const size_t emit_boundary_s = (new_audio_duration_s > config.overlap_duration_s) 
            ? (new_audio_duration_s - config.overlap_duration_s) : new_audio_duration_s;
        const int64_t emit_boundary_ms = emit_boundary_s * 1000;
        
        // Process each segment
        for (const auto& wseg : whisper_segments) {
            if (wseg.text.empty()) continue;
            
            // Calculate absolute timestamps (relative to NEW audio start)
            int64_t seg_start_ms = transcribe_start_time_ms + wseg.t0_ms;
            int64_t seg_end_ms = transcribe_start_time_ms + wseg.t1_ms;
            
            // Compute speaker ID if diarization enabled
            int speaker_id = -1;
            if (config.enable_diarization && speaker_clusterer) {
                auto t_diar_start = std::chrono::steady_clock::now();
                
                // Calculate sample range within NEW audio
                int start_sample = (wseg.t0_ms * 16000) / 1000;
                int end_sample = (wseg.t1_ms * 16000) / 1000;
                start_sample = std::max(0, std::min(start_sample, (int)transcribe_sample_count));
                end_sample = std::max(start_sample, std::min(end_sample, (int)transcribe_sample_count));
                
                // Need at least 0.5s for reliable embedding
                if (end_sample - start_sample >= 8000) {
                    auto embedding = diar::compute_speaker_embedding(
                        transcribe_data + start_sample,
                        end_sample - start_sample,
                        16000
                    );
                    speaker_id = speaker_clusterer->assign(embedding);
                }
                
                auto t_diar_end = std::chrono::steady_clock::now();
                total_diar_time_s += std::chrono::duration<double>(t_diar_end - t_diar_start).count();
            }
            
            // Skip if already emitted (duplicate prevention)
            if (seg_end_ms <= last_emitted_end_ms) {
                continue;
            }
            
            // Check if should hold or emit
            if (wseg.t1_ms >= emit_boundary_ms) {
                // HOLD this segment (in overlap zone of NEW audio)
                HeldSegment held;
                held.text = wseg.text;
                held.start_ms = seg_start_ms;
                held.end_ms = seg_end_ms;
                held.speaker_id = speaker_id;
                held_segments.push_back(held);
            } else {
                // EMIT this segment immediately
                emit_segment(wseg.text, seg_start_ms, seg_end_ms, speaker_id);
            }
        }
        
        // Slide the window
        slide_window();
    }
    
    void emit_segment(const std::string& text, int64_t start_ms, int64_t end_ms, int speaker_id) {
        // Trim start time if it overlaps with last emitted segment
        if (start_ms < last_emitted_end_ms) {
            start_ms = last_emitted_end_ms;
        }
        
        // Skip if segment became invalid after trimming
        if (start_ms >= end_ms) {
            return;
        }
        
        TranscriptionSegment seg;
        seg.text = text;
        seg.start_ms = start_ms;
        seg.end_ms = end_ms;
        seg.speaker_id = speaker_id;
        
        {
            std::lock_guard<std::mutex> lock(segments_mutex);
            all_segments.push_back(seg);
            last_emitted_end_ms = std::max(last_emitted_end_ms, end_ms);
        }
        
        // Update speaker stats
        update_speaker_stats(seg);
        
        segments_processed++;
        
        // Notify callback
        if (config.on_segment) {
            config.on_segment(seg);
        }
    }
    
    void emit_held_segments() {
        for (const auto& held : held_segments) {
            // Trim start time if it overlaps with last emitted segment
            int64_t start_ms = held.start_ms;
            int64_t end_ms = held.end_ms;
            
            if (start_ms < last_emitted_end_ms) {
                start_ms = last_emitted_end_ms;
            }
            
            // Skip if segment became invalid after trimming
            if (start_ms >= end_ms) {
                continue;
            }
            
            TranscriptionSegment seg;
            seg.text = held.text;
            seg.start_ms = start_ms;
            seg.end_ms = end_ms;
            seg.speaker_id = held.speaker_id;
            
            {
                std::lock_guard<std::mutex> lock(segments_mutex);
                all_segments.push_back(seg);
                last_emitted_end_ms = std::max(last_emitted_end_ms, end_ms);
            }
            
            // Update speaker stats
            update_speaker_stats(seg);
            
            segments_processed++;
            
            // Notify callback
            if (config.on_segment) {
                config.on_segment(seg);
            }
        }
        
        held_segments.clear();
    }
    
    void update_speaker_stats(const TranscriptionSegment& seg) {
        if (seg.speaker_id < 0) return;
        
        std::lock_guard<std::mutex> lock(stats_mutex);
        
        auto& stats = speaker_stats_map[seg.speaker_id];
        if (stats.speaker_id != seg.speaker_id) {
            stats.speaker_id = seg.speaker_id;
        }
        
        stats.total_speaking_time_ms += seg.duration_ms();
        stats.segment_count++;
        stats.last_text = seg.text;
        
        // Notify stats callback
        if (config.on_stats) {
            std::vector<SpeakerStats> all_stats;
            for (const auto& [id, s] : speaker_stats_map) {
                all_stats.push_back(s);
            }
            config.on_stats(all_stats);
        }
    }
    
    void slide_window() {
        const size_t overlap_samples = 16000 * config.overlap_duration_s;
        
        if (audio_buffer.size() > overlap_samples) {
            // Keep last overlap_duration_s of audio
            size_t discard = audio_buffer.size() - overlap_samples;
            buffer_start_time_ms += (discard * 1000) / 16000;
            
            std::vector<int16_t> tail(audio_buffer.end() - overlap_samples, audio_buffer.end());
            audio_buffer.swap(tail);
        } else {
            // Buffer smaller than overlap - just clear and update time
            buffer_start_time_ms += (audio_buffer.size() * 1000) / 16000;
            audio_buffer.clear();
        }
    }
    
    void flush_remaining() {
        // Emit any held segments
        emit_held_segments();
        
        // Process remaining audio in buffer
        const size_t overlap_samples = 16000 * config.overlap_duration_s;
        size_t flush_start_sample = std::min(overlap_samples, audio_buffer.size());
        size_t flush_sample_count = (audio_buffer.size() > flush_start_sample) 
                                    ? (audio_buffer.size() - flush_start_sample) 
                                    : 0;
        
        if (flush_sample_count >= 8000) {  // At least 0.5s
            // Calculate start time for new audio
            int64_t flush_start_time_ms = buffer_start_time_ms + (flush_start_sample * 1000) / 16000;
            
            // Transcribe remaining NEW audio only
            const int16_t* flush_data = audio_buffer.data() + flush_start_sample;
            
            auto whisper_segments = whisper->transcribe_chunk_segments(flush_data, flush_sample_count);
            
            for (const auto& wseg : whisper_segments) {
                if (wseg.text.empty()) continue;
                
                int64_t seg_start_ms = flush_start_time_ms + wseg.t0_ms;
                int64_t seg_end_ms = flush_start_time_ms + wseg.t1_ms;
                
                int speaker_id = -1;
                if (config.enable_diarization && speaker_clusterer) {
                    // Compute speaker from the flush buffer region
                    int start_sample = (wseg.t0_ms * 16000) / 1000;
                    int end_sample = (wseg.t1_ms * 16000) / 1000;
                    start_sample = std::max(0, std::min(start_sample, (int)flush_sample_count));
                    end_sample = std::max(start_sample, std::min(end_sample, (int)flush_sample_count));
                    
                    if (end_sample - start_sample >= 8000) {
                        auto embedding = diar::compute_speaker_embedding(
                            flush_data + start_sample,
                            end_sample - start_sample,
                            16000
                        );
                        speaker_id = speaker_clusterer->assign(embedding);
                    }
                }
                
                emit_segment(wseg.text, seg_start_ms, seg_end_ms, speaker_id);
            }
        }
        
        // Final speaker clustering if enabled
        // TODO: Implement proper final clustering using ContinuousFrameAnalyzer API
        /*
        if (config.enable_diarization && frame_analyzer && speaker_clusterer) {
            auto frames = frame_analyzer->get_frames();
            if (!frames.empty()) {
                // Cluster all frames
                speaker_clusterer->cluster_frames(frames);
                
                // Reassign speakers to all segments based on frame voting
                reassign_speakers_from_frames(frames);
            }
        }
        */
    }
    
    // TODO: Fix these methods to use correct ContinuousFrameAnalyzer API
    /*
    void reassign_speakers_from_frames(const std::vector<diar::SpeakerFrame>& frames) {
        std::lock_guard<std::mutex> lock(segments_mutex);
        
        for (auto& seg : all_segments) {
            // Find frames that overlap with this segment
            std::map<int, int> speaker_votes;
            
            for (const auto& frame : frames) {
                if (frame.speaker_id < 0) continue;
                
                int64_t frame_start_ms = frame.time_ms;
                int64_t frame_end_ms = frame.time_ms + 1000;  // 1s window
                
                // Check for overlap
                if (frame_start_ms < seg.end_ms && frame_end_ms > seg.start_ms) {
                    speaker_votes[frame.speaker_id]++;
                }
            }
            
            // Assign speaker with most votes
            int best_speaker = -1;
            int max_votes = 0;
            for (const auto& [spk, votes] : speaker_votes) {
                if (votes > max_votes) {
                    max_votes = votes;
                    best_speaker = spk;
                }
            }
            
            if (best_speaker >= 0 && seg.speaker_id != best_speaker) {
                // Update speaker ID
                int old_speaker = seg.speaker_id;
                seg.speaker_id = best_speaker;
                
                // Recalculate stats
                recalculate_speaker_stats();
            }
        }
    }
    
    void recalculate_speaker_stats() {
        std::lock_guard<std::mutex> lock(stats_mutex);
        
        speaker_stats_map.clear();
        
        for (const auto& seg : all_segments) {
            if (seg.speaker_id < 0) continue;
            
            auto& stats = speaker_stats_map[seg.speaker_id];
            if (stats.speaker_id != seg.speaker_id) {
                stats.speaker_id = seg.speaker_id;
            }
            
            stats.total_speaking_time_ms += seg.duration_ms();
            stats.segment_count++;
            stats.last_text = seg.text;
        }
        
        // Notify stats callback
        if (config.on_stats) {
            std::vector<SpeakerStats> all_stats;
            for (const auto& [id, s] : speaker_stats_map) {
                all_stats.push_back(s);
            }
            config.on_stats(all_stats);
        }
    }
    */
    // End of commented-out methods that need API fixes
    
    std::vector<TranscriptionSegment> get_all_segments() const {
        std::lock_guard<std::mutex> lock(segments_mutex);
        return all_segments;
    }
    
    std::vector<SpeakerStats> get_speaker_stats() const {
        std::lock_guard<std::mutex> lock(stats_mutex);
        std::vector<SpeakerStats> stats;
        for (const auto& [id, s] : speaker_stats_map) {
            stats.push_back(s);
        }
        return stats;
    }
    
    int64_t get_total_time_ms() const {
        std::lock_guard<std::mutex> lock(segments_mutex);
        if (all_segments.empty()) return 0;
        return all_segments.back().end_ms - all_segments.front().start_ms;
    }
    
    void clear() {
        std::lock_guard<std::mutex> seg_lock(segments_mutex);
        std::lock_guard<std::mutex> stats_lock(stats_mutex);
        
        all_segments.clear();
        held_segments.clear();
        speaker_stats_map.clear();
        audio_buffer.clear();
        
        buffer_start_time_ms = 0;
        last_emitted_end_ms = 0;
        total_whisper_time_s = 0.0;
        total_diar_time_s = 0.0;
        segments_processed = 0;
        windows_processed = 0;
        
        if (frame_analyzer) {
            // Reset frame analyzer (would need to add reset() method)
        }
        if (speaker_clusterer) {
            // Reset speaker clusterer (would need to add reset() method)
        }
    }
};

// =============================================================================
// Public API Implementation
// =============================================================================

TranscriptionController::TranscriptionController() 
    : impl_(std::make_unique<Impl>()) {
}

TranscriptionController::~TranscriptionController() {
    stop();
}

bool TranscriptionController::initialize(const Config& config) {
    return impl_->initialize(config);
}

bool TranscriptionController::start() {
    if (running_.load()) return false;
    if (!impl_->initialized) return false;
    
    running_.store(true);
    paused_.store(false);
    
    // Start async processing
    impl_->running.store(true);
    impl_->processing_thread_ = std::thread([this]() {
        impl_->processing_loop();
    });
    
    if (impl_->config.on_status) {
        impl_->config.on_status("Transcription started", false);
    }
    
    return true;
}

void TranscriptionController::stop() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    // Stop async processing
    impl_->running.store(false);
    impl_->audio_queue_.stop();
    
    // Wait for processing thread to finish
    if (impl_->processing_thread_.joinable()) {
        impl_->processing_thread_.join();
    }
    
    if (impl_->config.on_status) {
        impl_->config.on_status("Transcription stopped", false);
    }
}

void TranscriptionController::pause() {
    paused_.store(true);
    if (impl_->config.on_status) {
        impl_->config.on_status("Transcription paused", false);
    }
}

void TranscriptionController::resume() {
    paused_.store(false);
    if (impl_->config.on_status) {
        impl_->config.on_status("Transcription resumed", false);
    }
}

void TranscriptionController::add_audio(const int16_t* samples, size_t sample_count, int sample_rate) {
    if (!running_.load() || paused_.load()) return;
    impl_->add_audio(samples, sample_count, sample_rate);
}

std::vector<TranscriptionSegment> TranscriptionController::get_all_segments() const {
    return impl_->get_all_segments();
}

std::vector<SpeakerStats> TranscriptionController::get_speaker_stats() const {
    return impl_->get_speaker_stats();
}

int64_t TranscriptionController::get_total_time_ms() const {
    return impl_->get_total_time_ms();
}

void TranscriptionController::clear() {
    impl_->clear();
}

TranscriptionController::PerformanceMetrics TranscriptionController::get_performance_metrics() const {
    PerformanceMetrics metrics;
    metrics.realtime_factor = 0.0;
    
    int64_t audio_duration_ms = impl_->get_total_time_ms();
    if (audio_duration_ms > 0) {
        double audio_duration_s = audio_duration_ms / 1000.0;
        double processing_time_s = impl_->total_whisper_time_s + impl_->total_diar_time_s;
        metrics.realtime_factor = processing_time_s / audio_duration_s;
    }
    
    metrics.whisper_time_s = impl_->total_whisper_time_s;
    metrics.diarization_time_s = impl_->total_diar_time_s;
    metrics.segments_processed = impl_->segments_processed;
    metrics.windows_processed = impl_->windows_processed;
    metrics.dropped_frames = impl_->audio_queue_.dropped_count();
    
    return metrics;
}

} // namespace core
