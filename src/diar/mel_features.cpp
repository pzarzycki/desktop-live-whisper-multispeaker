#include "mel_features.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace diar {

// FFT implementation - based on whisper.cpp's Cooley-Tukey radix-2 FFT
namespace {
    constexpr int FFT_SIZE = 512; // Must be >= n_fft (400), power of 2
    constexpr int SIN_COS_COUNT = FFT_SIZE;
    
    struct FFTCache {
        float sin_vals[SIN_COS_COUNT];
        float cos_vals[SIN_COS_COUNT];
        
        FFTCache() {
            for (int i = 0; i < SIN_COS_COUNT; i++) {
                double theta = (2.0 * M_PI * i) / SIN_COS_COUNT;
                sin_vals[i] = static_cast<float>(sin(theta));
                cos_vals[i] = static_cast<float>(cos(theta));
            }
        }
    };
    
    FFTCache fft_cache;
    
    // DFT fallback for non-power-of-2 sizes
    void dft(const float* in, int N, float* out) {
        const int sin_cos_step = SIN_COS_COUNT / N;
        for (int k = 0; k < N; k++) {
            double re = 0.0, im = 0.0;
            for (int n = 0; n < N; n++) {
                int idx = (k * n * sin_cos_step) % SIN_COS_COUNT;
                re += in[n] * fft_cache.cos_vals[idx];
                im -= in[n] * fft_cache.sin_vals[idx];
            }
            out[k * 2 + 0] = static_cast<float>(re);
            out[k * 2 + 1] = static_cast<float>(im);
        }
    }
    
    // Cooley-Tukey radix-2 FFT (recursive)
    void fft_radix2(float* in, int N, float* out, float* temp) {
        if (N == 1) {
            out[0] = in[0];
            out[1] = 0.0f;
            return;
        }
        
        const int half_N = N / 2;
        if (N - half_N * 2 == 1) {
            // Odd size, fall back to DFT
            dft(in, N, out);
            return;
        }
        
        // Split into even and odd
        float* even = temp;
        float* odd = temp + N;
        for (int i = 0; i < half_N; ++i) {
            even[i] = in[2 * i];
            odd[i] = in[2 * i + 1];
        }
        
        float* even_fft = out + N;
        float* odd_fft = even_fft + N;
        fft_radix2(even, half_N, even_fft, temp + 2 * N);
        fft_radix2(odd, half_N, odd_fft, temp + 2 * N);
        
        const int sin_cos_step = SIN_COS_COUNT / N;
        for (int k = 0; k < half_N; k++) {
            int idx = k * sin_cos_step;
            float re = fft_cache.cos_vals[idx];
            float im = -fft_cache.sin_vals[idx];
            
            float re_odd = odd_fft[2 * k + 0];
            float im_odd = odd_fft[2 * k + 1];
            
            float twiddle_re = re * re_odd - im * im_odd;
            float twiddle_im = re * im_odd + im * re_odd;
            
            out[2 * k + 0] = even_fft[2 * k + 0] + twiddle_re;
            out[2 * k + 1] = even_fft[2 * k + 1] + twiddle_im;
            
            out[2 * (k + half_N) + 0] = even_fft[2 * k + 0] - twiddle_re;
            out[2 * (k + half_N) + 1] = even_fft[2 * k + 1] - twiddle_im;
        }
    }
}

// Compute power spectrum using FFT
static void compute_power_spectrum(const std::vector<float>& windowed, std::vector<float>& power_out, int n_fft) {
    // Allocate temp buffers for FFT
    std::vector<float> fft_in(n_fft, 0.0f);
    std::vector<float> fft_out(n_fft * 8); // Extra space for FFT recursion
    
    // Copy windowed data
    size_t copy_size = std::min(windowed.size(), static_cast<size_t>(n_fft));
    std::copy(windowed.begin(), windowed.begin() + copy_size, fft_in.begin());
    
    // Compute FFT
    fft_radix2(fft_in.data(), n_fft, fft_out.data(), fft_out.data() + n_fft * 2);
    
    // Compute power spectrum (magnitude squared)
    int n_bins = n_fft / 2 + 1;
    power_out.resize(n_bins);
    for (int i = 0; i < n_bins; i++) {
        float re = fft_out[i * 2 + 0];
        float im = fft_out[i * 2 + 1];
        power_out[i] = re * re + im * im;
    }
}

float MelFeatureExtractor::hz_to_mel(float hz) {
    return 2595.0f * log10(1.0f + hz / 700.0f);
}

float MelFeatureExtractor::mel_to_hz(float mel) {
    return 700.0f * (pow(10.0f, mel / 2595.0f) - 1.0f);
}

MelFeatureExtractor::MelFeatureExtractor(const Config& config) : m_config(config) {
    init_hann_window();
    init_mel_filters();
}

MelFeatureExtractor::~MelFeatureExtractor() = default;

void MelFeatureExtractor::init_hann_window() {
    m_hann_window.resize(m_config.n_fft);
    for (int i = 0; i < m_config.n_fft; i++) {
        m_hann_window[i] = 0.5f * (1.0f - cos(2.0f * M_PI * i / (m_config.n_fft - 1)));
    }
}

void MelFeatureExtractor::init_mel_filters() {
    const int n_fft_bins = m_config.n_fft / 2 + 1;
    m_mel_filters.resize(m_config.n_mels * n_fft_bins, 0.0f);

    // Create mel scale frequency points
    float mel_min = hz_to_mel(m_config.fmin);
    float mel_max = hz_to_mel(m_config.fmax);
    
    std::vector<float> mel_points(m_config.n_mels + 2);
    for (int i = 0; i < m_config.n_mels + 2; i++) {
        mel_points[i] = mel_min + (mel_max - mel_min) * i / (m_config.n_mels + 1);
    }

    // Convert mel points to Hz and then to FFT bins
    std::vector<int> bin_points(m_config.n_mels + 2);
    for (int i = 0; i < m_config.n_mels + 2; i++) {
        float hz = mel_to_hz(mel_points[i]);
        bin_points[i] = (int)floor((m_config.n_fft + 1) * hz / m_config.sample_rate);
    }

    // Create triangular mel filters
    for (int m = 0; m < m_config.n_mels; m++) {
        int left = bin_points[m];
        int center = bin_points[m + 1];
        int right = bin_points[m + 2];

        // Left slope
        for (int k = left; k < center; k++) {
            if (center > left) {
                m_mel_filters[m * n_fft_bins + k] = (float)(k - left) / (center - left);
            }
        }

        // Right slope
        for (int k = center; k < right; k++) {
            if (right > center) {
                m_mel_filters[m * n_fft_bins + k] = (float)(right - k) / (right - center);
            }
        }
    }
}

int MelFeatureExtractor::get_num_frames(int n_samples) const {
    if (n_samples < m_config.n_fft) {
        return 0;
    }
    return 1 + (n_samples - m_config.n_fft) / m_config.hop_length;
}

std::vector<float> MelFeatureExtractor::extract_features(const float* samples, int n_samples) {
    const int n_frames = get_num_frames(n_samples);
    if (n_frames <= 0) {
        std::cerr << "[MelFeatures] Warning: Audio too short for feature extraction\n";
        return std::vector<float>(m_config.n_mels, -80.0f);  // Return silence frame
    }

    std::vector<float> features(n_frames * m_config.n_mels);
    const int n_fft_bins = m_config.n_fft / 2 + 1;

    // Process each frame
    for (int frame = 0; frame < n_frames; frame++) {
        const int offset = frame * m_config.hop_length;

        // Apply window and prepare input
        std::vector<float> windowed(m_config.n_fft);
        for (int i = 0; i < m_config.n_fft && offset + i < n_samples; i++) {
            windowed[i] = samples[offset + i] * m_hann_window[i];
        }

        // Compute power spectrum via FFT
        std::vector<float> power_spectrum;
        compute_power_spectrum(windowed, power_spectrum, m_config.n_fft);

        // Apply mel filterbank
        for (int m = 0; m < m_config.n_mels; m++) {
            float mel_energy = 0.0f;
            for (int k = 0; k < n_fft_bins; k++) {
                mel_energy += power_spectrum[k] * m_mel_filters[m * n_fft_bins + k];
            }
            
            // Convert to log scale (dB)
            float log_mel = 10.0f * log10(std::max(mel_energy, 1e-10f));
            
            // Store in row-major order [time, features]
            features[frame * m_config.n_mels + m] = log_mel;
        }
    }

    return features;
}

} // namespace diar
