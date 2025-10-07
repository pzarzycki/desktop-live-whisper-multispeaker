#pragma once
#include <vector>
#include <cstdint>

namespace diar {

// Compute a log-mel spectrogram embedding over a window (mono int16 at 16 kHz)
// Returns a fixed-dim vector (n_mels)
std::vector<float> compute_logmel_embedding(const int16_t* pcm16, size_t samples, int sample_rate,
                                            int n_mels = 40);

class SpeakerClusterer {
public:
    SpeakerClusterer(int max_speakers = 2, float sim_threshold = 0.65f)
        : m_max(max_speakers), m_thr(sim_threshold), m_current_speaker(-1), m_frames_since_change(0) {}

    // Assign an embedding to a speaker cluster. Returns 0-based speaker index.
    // Uses hysteresis to avoid rapid speaker switching
    int assign(const std::vector<float>& emb);
    
    // Get current speaker (for continuity without re-computing)
    int current_speaker() const { return m_current_speaker; }

private:
    int m_max;
    float m_thr; // cosine similarity threshold to join
    std::vector<std::vector<float>> m_centroids;
    int m_current_speaker; // Track current speaker for continuity
    int m_frames_since_change; // Frames since last speaker change (for hysteresis)
};

} // namespace diar
