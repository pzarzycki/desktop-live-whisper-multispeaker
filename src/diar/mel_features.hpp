#pragma once

#include <vector>
#include <cstdint>

namespace diar {

/**
 * Standalone mel filterbank feature extraction for speaker embeddings.
 * This is INDEPENDENT of Whisper's mel extraction to avoid any interference.
 * 
 * Extracts 80-dimensional Fbank (mel filterbank) features suitable for
 * speaker embedding models like WeSpeaker ResNet34.
 */
class MelFeatureExtractor {
public:
    struct Config {
        int sample_rate = 16000;
        int n_fft = 400;           // 25ms at 16kHz
        int hop_length = 160;      // 10ms at 16kHz
        int n_mels = 80;           // Number of mel bins
        float fmin = 0.0f;
        float fmax = 8000.0f;      // Nyquist frequency at 16kHz
    };

    explicit MelFeatureExtractor(const Config& config = Config{});
    ~MelFeatureExtractor();

    /**
     * Extract mel filterbank features from audio samples.
     * 
     * @param samples Float audio samples (normalized to [-1, 1])
     * @param n_samples Number of samples
     * @return Vector of shape [n_frames, n_mels], stored row-major (time x features)
     */
    std::vector<float> extract_features(const float* samples, int n_samples);

    /**
     * Get number of time frames that would be generated
     */
    int get_num_frames(int n_samples) const;

private:
    Config m_config;
    std::vector<float> m_mel_filters;  // Mel filterbank matrix [n_mels x n_fft_bins]
    std::vector<float> m_hann_window;  // Hann window

    void init_hann_window();
    void init_mel_filters();
    
    // Helper: Convert Hz to mel scale
    static float hz_to_mel(float hz);
    static float mel_to_hz(float mel);
};

} // namespace diar
