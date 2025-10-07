#include "diar/speaker_cluster.hpp"
#include <cmath>
#include <algorithm>
#include <complex>
#include <vector>

namespace diar {

static float cosine(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || b.empty() || a.size() != b.size()) return 0.0f;
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) { dot += a[i]*b[i]; na += a[i]*a[i]; nb += b[i]*b[i]; }
    if (na <= 0.0 || nb <= 0.0) return 0.0f;
    return static_cast<float>(dot / (std::sqrt(na) * std::sqrt(nb) + 1e-8));
}

// Simple in-place Cooley-Tukey FFT (radix-2, decimation in time)
static void fft_inplace(std::vector<std::complex<double>>& x) {
    const size_t N = x.size();
    if (N <= 1) return;
    
    // Bit-reversal permutation
    size_t j = 0;
    for (size_t i = 0; i < N; ++i) {
        if (j > i) std::swap(x[i], x[j]);
        size_t m = N >> 1;
        while (m >= 1 && j >= m) { j -= m; m >>= 1; }
        j += m;
    }
    
    // Cooley-Tukey FFT
    const double pi = 3.14159265358979323846;
    for (size_t s = 1; s <= std::log2(N); ++s) {
        size_t m = 1 << s;
        std::complex<double> wm = std::exp(std::complex<double>(0, -2.0 * pi / m));
        for (size_t k = 0; k < N; k += m) {
            std::complex<double> w = 1.0;
            for (size_t j = 0; j < m / 2; ++j) {
                std::complex<double> t = w * x[k + j + m / 2];
                std::complex<double> u = x[k + j];
                x[k + j] = u + t;
                x[k + j + m / 2] = u - t;
                w *= wm;
            }
        }
    }
}

// Mel scale conversion
static double hz_to_mel(double hz) {
    return 2595.0 * std::log10(1.0 + hz / 700.0);
}

static double mel_to_hz(double mel) {
    return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0);
}

std::vector<float> compute_logmel_embedding(const int16_t* pcm16, size_t samples, int sample_rate, int n_mels) {
    if (!pcm16 || samples == 0 || sample_rate <= 0) return {};
    
    // FFT parameters
    const size_t fft_size = 512;
    const size_t hop_size = 160; // 10ms at 16kHz
    
    // Create mel filterbank
    const int n_fft = fft_size / 2 + 1;
    const double fmin = 80.0;
    const double fmax = sample_rate / 2.0;
    const double mel_min = hz_to_mel(fmin);
    const double mel_max = hz_to_mel(fmax);
    
    std::vector<std::vector<double>> mel_filters(n_mels, std::vector<double>(n_fft, 0.0));
    std::vector<double> mel_points(n_mels + 2);
    for (int i = 0; i < n_mels + 2; ++i) {
        double mel = mel_min + (mel_max - mel_min) * i / (n_mels + 1);
        mel_points[i] = mel_to_hz(mel);
    }
    
    // Build triangular filters
    for (int m = 0; m < n_mels; ++m) {
        double f_left = mel_points[m];
        double f_center = mel_points[m + 1];
        double f_right = mel_points[m + 2];
        
        for (int k = 0; k < n_fft; ++k) {
            double freq = k * sample_rate / (double)fft_size;
            if (freq >= f_left && freq <= f_center) {
                mel_filters[m][k] = (freq - f_left) / (f_center - f_left);
            } else if (freq > f_center && freq <= f_right) {
                mel_filters[m][k] = (f_right - freq) / (f_right - f_center);
            }
        }
    }
    
    // Compute STFT and aggregate mel energies
    std::vector<double> mel_energy(n_mels, 0.0);
    int frame_count = 0;
    
    for (size_t pos = 0; pos + fft_size <= samples; pos += hop_size) {
        // Window (Hann)
        std::vector<std::complex<double>> frame(fft_size);
        for (size_t i = 0; i < fft_size; ++i) {
            double window = 0.5 * (1.0 - std::cos(2.0 * 3.14159265358979323846 * i / (fft_size - 1)));
            frame[i] = std::complex<double>(pcm16[pos + i] / 32768.0 * window, 0.0);
        }
        
        // FFT
        fft_inplace(frame);
        
        // Power spectrum (first half + DC)
        std::vector<double> power(n_fft);
        for (int k = 0; k < n_fft; ++k) {
            power[k] = std::norm(frame[k]);
        }
        
        // Apply mel filters
        for (int m = 0; m < n_mels; ++m) {
            double energy = 0.0;
            for (int k = 0; k < n_fft; ++k) {
                energy += power[k] * mel_filters[m][k];
            }
            mel_energy[m] += energy;
        }
        frame_count++;
    }
    
    // Average over frames and convert to log scale
    std::vector<float> mel(n_mels);
    for (int m = 0; m < n_mels; ++m) {
        mel[m] = static_cast<float>(std::log(mel_energy[m] / std::max(1, frame_count) + 1e-10));
    }
    
    // Normalize
    double mean = 0.0;
    for (float v : mel) mean += v;
    mean /= mel.size();
    
    double var = 0.0;
    for (float v : mel) var += (v - mean) * (v - mean);
    var /= mel.size();
    double stdv = std::sqrt(var + 1e-8);
    
    for (float& v : mel) v = static_cast<float>((v - mean) / stdv);
    
    return mel;
}

int SpeakerClusterer::assign(const std::vector<float>& emb) {
    if (emb.empty()) return m_current_speaker >= 0 ? m_current_speaker : -1;
    
    // First frame - initialize first speaker
    if (m_centroids.empty()) {
        m_centroids.push_back(emb);
        m_current_speaker = 0;
        m_frames_since_change = 0;
        return 0;
    }
    
    // Calculate similarity to all existing speakers
    std::vector<float> similarities(m_centroids.size());
    for (size_t i = 0; i < m_centroids.size(); ++i) {
        similarities[i] = cosine(emb, m_centroids[i]);
    }
    
    // Find best match
    int best = -1;
    float bestSim = -1.0f;
    for (size_t i = 0; i < similarities.size(); ++i) {
        if (similarities[i] > bestSim) {
            bestSim = similarities[i];
            best = static_cast<int>(i);
        }
    }
    
    // Hysteresis: require stronger evidence to CHANGE speakers than to stay
    float switch_threshold = m_thr + 0.10f;  // Need +0.10 higher similarity to switch
    int min_frames_before_switch = 3;        // Need at least 3 consecutive frames suggesting switch
    
    // If current speaker exists and has good similarity, stay with them
    if (m_current_speaker >= 0 && m_current_speaker < (int)similarities.size()) {
        float current_sim = similarities[m_current_speaker];
        
        // Stay with current speaker if similarity is decent
        if (current_sim >= m_thr) {
            // Update centroid slowly
            auto& c = m_centroids[m_current_speaker];
            for (size_t i = 0; i < c.size(); ++i) {
                c[i] = 0.95f * c[i] + 0.05f * emb[i];
            }
            m_frames_since_change++;
            return m_current_speaker;
        }
        
        // Current speaker similarity dropped - consider switching only if:
        // 1. Best match is significantly better
        // 2. We've been stable for a while (avoid rapid switching)
        if (best != m_current_speaker && bestSim > current_sim + 0.15f && m_frames_since_change >= min_frames_before_switch) {
            // Switch to existing speaker
            m_current_speaker = best;
            m_frames_since_change = 0;
            return best;
        }
        
        // Try to create new speaker if room and similarity is low
        if ((int)m_centroids.size() < m_max && bestSim < switch_threshold && m_frames_since_change >= min_frames_before_switch) {
            m_centroids.push_back(emb);
            m_current_speaker = (int)m_centroids.size() - 1;
            m_frames_since_change = 0;
            return m_current_speaker;
        }
        
        // Default: stay with current speaker even if similarity is marginal
        m_frames_since_change++;
        return m_current_speaker;
    }
    
    // No current speaker or current speaker invalid
    // Assign to best match if above threshold
    if (best >= 0 && bestSim >= m_thr) {
        m_current_speaker = best;
        m_frames_since_change = 0;
        return best;
    }
    
    // Create new speaker if room
    if ((int)m_centroids.size() < m_max) {
        m_centroids.push_back(emb);
        m_current_speaker = (int)m_centroids.size() - 1;
        m_frames_since_change = 0;
        return m_current_speaker;
    }
    
    // Fallback: assign to best match
    if (best >= 0) {
        m_current_speaker = best;
        m_frames_since_change = 0;
        return best;
    }
    
    return -1;
}

} // namespace diar
