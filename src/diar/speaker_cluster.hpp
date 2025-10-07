#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include <deque>
#include <memory>

namespace diar {

// Forward declarations
class SpeakerClusterer;
class OnnxSpeakerEmbedder;

// Embedding extraction mode
enum class EmbeddingMode {
    HandCrafted,  // 53-dim MFCC+Delta+Pitch+Formants (Phase 2b)
    NeuralONNX    // 192-dim ECAPA-TDNN neural embeddings (Phase 2c)
};

// Speaker change point with timestamp
struct SpeakerSegment {
    int speaker_id;        // 0, 1, 2, etc.
    size_t start_sample;   // Start position in audio
    size_t end_sample;     // End position in audio
};

// Compute speaker embedding that captures voice characteristics
// Uses MFCCs + pitch + spectral features for speaker discrimination
// Returns a fixed-dim vector optimized for speaker identity (not content)
std::vector<float> compute_speaker_embedding(const int16_t* pcm16, size_t samples, int sample_rate);

// Enhanced v2: MFCC + Delta + Pitch + Formants + Spectral features (53-dim)
// More discriminative for speaker recognition than simple mel features
std::vector<float> compute_speaker_embedding_v2(const int16_t* pcm16, size_t samples, int sample_rate);

// Legacy: Simple log-mel features (40-dim, less discriminative)
std::vector<float> compute_logmel_embedding(const int16_t* pcm16, size_t samples, int sample_rate, int n_mels = 40);

// Detect speaker segments in audio using sliding window VAD + diarization
// Returns list of segments with speaker IDs and timestamps
// min_segment_ms: minimum segment duration (default 300ms)
// frame_ms: analysis frame size (default 100ms)
std::vector<SpeakerSegment> detect_speaker_segments(
    const int16_t* pcm16, 
    size_t samples, 
    int sample_rate,
    int max_speakers = 2,
    int min_segment_ms = 300,
    int frame_ms = 100
);

// Post-process transcribed segments with speaker diarization
// Takes Whisper segments (with word timestamps) and assigns speakers to each segment
// Uses overlapping speaker analysis to detect changes within segments
struct TranscriptSegment {
    std::string text;
    int speaker_id;
    int64_t t0_ms;
    int64_t t1_ms;
};

std::vector<TranscriptSegment> assign_speakers_to_segments(
    const std::vector<TranscriptSegment>& whisper_segments,
    const int16_t* audio,
    size_t total_samples,
    int sample_rate,
    int max_speakers = 2,
    bool verbose = false
);

// ============================================================================
// Phase 2: Continuous Frame Analysis for Real-Time Speaker Diarization
// ============================================================================
//
// This implements a fine-grained (250ms) frame-by-frame speaker analysis
// independent of Whisper's segmentation. Key principles:
// 
// 1. Extract embeddings every hop_ms (default 250ms)
// 2. Use consistent window_ms (default 1000ms) for each embedding
// 3. Store frames in circular buffer with timestamps
// 4. Enable global clustering and confidence-based output later
//
// This gives us:
// - High temporal resolution (4 frames per second)
// - Independent of transcription timing
// - Foundation for online clustering (Phase 3)
// - Can detect rapid speaker changes
//

class ContinuousFrameAnalyzer {
public:
    // A single analysis frame with speaker embedding and metadata
    struct Frame {
        std::vector<float> embedding;  // 55-dimensional speaker embedding
        int64_t t_start_ms;            // Start time in audio stream
        int64_t t_end_ms;              // End time (t_start_ms + window_ms)
        int speaker_id;                // Assigned speaker (or -1 if unknown)
        float confidence;              // Assignment confidence (0-1)
        
        Frame() : t_start_ms(0), t_end_ms(0), speaker_id(-1), confidence(0.0f) {}
    };
    
    // Configuration
    struct Config {
        int hop_ms = 250;       // Extract embedding every 250ms (4 fps)
        int window_ms = 1000;   // Use 1s window for each embedding
        int history_sec = 60;   // Keep last 60 seconds of frames
        EmbeddingMode embedding_mode = EmbeddingMode::NeuralONNX;  // Use neural embeddings
        std::string onnx_model_path = "models/speaker_embedding.onnx";
        bool verbose = false;
    };
    
    ContinuousFrameAnalyzer(int sample_rate, const Config& config = Config());
    ~ContinuousFrameAnalyzer();
    
    // Add audio chunk to internal buffer and extract new frames
    // Automatically extracts frames as audio accumulates
    // Returns number of new frames extracted
    int add_audio(const int16_t* samples, size_t n_samples);
    
    // Get all frames in time range [t0_ms, t1_ms)
    // Useful for mapping speaker IDs to Whisper segments
    std::vector<Frame> get_frames_in_range(int64_t t0_ms, int64_t t1_ms) const;
    
    // Get the most recent frame (for real-time display)
    const Frame* get_latest_frame() const;
    
    // Get all stored frames (for global re-clustering)
    const std::deque<Frame>& get_all_frames() const { return m_frames; }
    
    // Clear history (useful for long files to prevent unbounded memory)
    void clear_old_frames(int64_t before_ms);
    
    // Assign speaker IDs to frames (called by clustering manager)
    void update_speaker_ids(const std::vector<int>& speaker_ids);
    
    // Simple clustering of all frames into speakers
    // Call this after all audio is processed
    // Updates speaker_id field in each frame
    void cluster_frames(int max_speakers = 2, float threshold = 0.50f);
    
    // Get statistics
    size_t frame_count() const { return m_frames.size(); }
    int64_t duration_ms() const;
    
private:
    int m_sample_rate;
    Config m_config;
    
    // Audio buffer (mono int16)
    std::vector<int16_t> m_audio_buffer;
    int64_t m_total_samples_processed;  // Track position in stream
    
    // Frame storage (circular buffer via deque)
    std::deque<Frame> m_frames;
    
    // Next frame extraction time
    int64_t m_next_frame_ms;
    
    // ONNX embedder (lazy-initialized)
    std::unique_ptr<OnnxSpeakerEmbedder> m_onnx_embedder;
    
    // Extract frame at specific time point
    Frame extract_frame_at_ms(int64_t center_ms);
    
    // Helper: convert samples to milliseconds
    int64_t samples_to_ms(size_t samples) const;
    size_t ms_to_samples(int64_t ms) const;
};

// Continuous speaker tracking - for streaming audio (OLD - kept for compatibility)
// Analyzes speaker identity on sliding windows without fixed boundaries
class ContinuousSpeakerTracker {
public:
    struct SpeakerFrame {
        int speaker_id;         // Current speaker (0, 1, 2...) or -1 if uncertain
        size_t sample_position; // Position in audio stream
        float confidence;       // Confidence score (0-1)
    };

    ContinuousSpeakerTracker(int sample_rate, int max_speakers = 2, 
                             int frame_ms = 200, bool verbose = false);
    
    ~ContinuousSpeakerTracker();
    
    // Process new audio chunk, returns speaker ID for this chunk
    // Returns -1 if uncertain (silence, noise, etc.)
    SpeakerFrame process_chunk(const int16_t* pcm16, size_t samples);
    
    // Detect if speaker changed recently (for triggering transcription)
    bool speaker_changed() const { return m_speaker_changed; }
    
    // Detect if there's a pause (for triggering transcription)
    bool pause_detected() const { return m_pause_detected; }
    
    // Get current speaker
    int current_speaker() const { return m_current_speaker; }
    
    // Reset state (for new audio stream)
    void reset();

private:
    int m_sample_rate;
    int m_frame_samples;
    SpeakerClusterer* m_clusterer;  // Use pointer for incomplete type
    int m_current_speaker;
    bool m_speaker_changed;
    bool m_pause_detected;
    size_t m_total_samples_processed;
    std::vector<int16_t> m_buffer; // Buffer for incomplete frames
};

class SpeakerClusterer {
public:
    SpeakerClusterer(int max_speakers = 2, float sim_threshold = 0.60f, bool verbose = false)
        : m_max(max_speakers), m_thr(sim_threshold), m_current_speaker(-1), 
          m_frames_since_change(0), m_verbose(verbose) {}

    // Assign an embedding to a speaker cluster. Returns 0-based speaker index.
    // Uses hysteresis and confidence scoring to avoid rapid speaker switching
    // Returns -1 if not enough confidence to assign
    int assign(const std::vector<float>& emb);
    
    // Get current speaker (for continuity)
    int current_speaker() const { return m_current_speaker; }
    
    // Get number of discovered speakers
    int num_speakers() const { return static_cast<int>(m_centroids.size()); }

private:
    int m_max;
    float m_thr; // cosine similarity threshold
    std::vector<std::vector<float>> m_centroids;
    int m_current_speaker; // Track current speaker for continuity
    int m_frames_since_change; // Frames since last speaker change (for hysteresis)
    bool m_verbose; // Enable debug output
};

} // namespace diar
