#include "diar/speaker_cluster.hpp"
#include <cmath>
#include <algorithm>
#include <complex>
#include <vector>
#include <map>

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

// Wrapper: compute_speaker_embedding uses compute_logmel_embedding with good defaults
std::vector<float> compute_speaker_embedding(const int16_t* pcm16, size_t samples, int sample_rate) {
    // Use 40 mel filters for speaker discrimination
    return compute_logmel_embedding(pcm16, samples, sample_rate, 40);
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

// ============================================================================
// Whisper-first Post-Processing: Assign speakers to transcribed segments
// ============================================================================

std::vector<TranscriptSegment> assign_speakers_to_segments(
    const std::vector<TranscriptSegment>& whisper_segments,
    const int16_t* audio,
    size_t total_samples,
    int sample_rate,
    int max_speakers,
    bool verbose
) {
    if (whisper_segments.empty() || !audio || total_samples == 0) {
        return whisper_segments;
    }
    
    std::vector<TranscriptSegment> result;
    SpeakerClusterer clusterer(max_speakers, 0.60f, verbose);
    
    // Process each Whisper segment
    for (const auto& seg : whisper_segments) {
        int64_t duration_ms = seg.t1_ms - seg.t0_ms;
        
        // For short segments (<1s), analyze as single unit
        if (duration_ms < 1000) {
            size_t start_sample = (seg.t0_ms * sample_rate) / 1000;
            size_t end_sample = (seg.t1_ms * sample_rate) / 1000;
            
            if (end_sample > total_samples) end_sample = total_samples;
            if (start_sample >= end_sample) continue;
            
            size_t seg_samples = end_sample - start_sample;
            auto embedding = compute_speaker_embedding(audio + start_sample, seg_samples, sample_rate);
            
            int speaker = clusterer.assign(embedding);
            
            TranscriptSegment out = seg;
            out.speaker_id = speaker;
            result.push_back(out);
            
            if (verbose) {
                fprintf(stderr, "[Diar] Short segment [%lld-%lld ms]: assigned S%d\n",
                        seg.t0_ms, seg.t1_ms, speaker);
            }
            continue;
        }
        
        // For longer segments (>=1s), use sliding windows for majority vote
        // NOTE: We do NOT split segments because we can't split the text properly
        //       Keep Whisper's excellent transcription intact, just assign speaker
        const int64_t window_ms = 1000;   // 1s analysis window
        const int64_t hop_ms = 500;       // 0.5s hop (50% overlap)
        
        std::vector<int> frame_speakers;
        
        // Analyze segment with sliding windows
        for (int64_t t = seg.t0_ms; t < seg.t1_ms; t += hop_ms) {
            int64_t win_start = t;
            int64_t win_end = t + window_ms;
            
            // Clamp to segment bounds
            if (win_end > seg.t1_ms) win_end = seg.t1_ms;
            if (win_end - win_start < 500) break;  // Need at least 0.5s
            
            size_t start_sample = (win_start * sample_rate) / 1000;
            size_t end_sample = (win_end * sample_rate) / 1000;
            
            if (end_sample > total_samples) end_sample = total_samples;
            if (start_sample >= end_sample) break;
            
            size_t win_samples = end_sample - start_sample;
            auto embedding = compute_speaker_embedding(audio + start_sample, win_samples, sample_rate);
            
            int speaker = clusterer.assign(embedding);
            frame_speakers.push_back(speaker);
            
            if (verbose) {
                fprintf(stderr, "[Diar] Window [%lld-%lld ms]: S%d\n", win_start, win_end, speaker);
            }
        }
        
        if (frame_speakers.empty()) {
            // Fallback: treat as single segment
            TranscriptSegment out = seg;
            out.speaker_id = -1;
            result.push_back(out);
            continue;
        }
        
        // Assign speaker based on majority vote (DO NOT split text!)
        std::map<int, int> vote_counts;
        for (int spk : frame_speakers) {
            vote_counts[spk]++;
        }
        
        int majority_speaker = frame_speakers[0];
        int max_votes = 0;
        for (const auto& [spk, count] : vote_counts) {
            if (count > max_votes) {
                max_votes = count;
                majority_speaker = spk;
            }
        }
        
        TranscriptSegment out = seg;
        out.speaker_id = majority_speaker;
        result.push_back(out);
        
        if (verbose) {
            fprintf(stderr, "[Diar] Long segment [%lld-%lld ms]: S%d (majority vote)\n",
                    seg.t0_ms, seg.t1_ms, majority_speaker);
        }
    }
    
    return result;
}

// ============================================================================
// Phase 2: ContinuousFrameAnalyzer Implementation
// ============================================================================

ContinuousFrameAnalyzer::ContinuousFrameAnalyzer(int sample_rate, const Config& config)
    : m_sample_rate(sample_rate)
    , m_config(config)
    , m_total_samples_processed(0)
    , m_next_frame_ms(config.window_ms / 2)  // Start at window_ms/2 so first window fits
{
    if (m_config.verbose) {
        fprintf(stderr, "[ContinuousFrameAnalyzer] Init: hop=%dms, window=%dms, history=%ds, sr=%d, first_frame=%lldms\n",
                m_config.hop_ms, m_config.window_ms, m_config.history_sec, m_sample_rate, m_next_frame_ms);
    }
}

ContinuousFrameAnalyzer::~ContinuousFrameAnalyzer() {
    if (m_config.verbose) {
        fprintf(stderr, "[ContinuousFrameAnalyzer] Cleanup: extracted %zu frames over %.1fs\n",
                m_frames.size(), duration_ms() / 1000.0);
    }
}

int64_t ContinuousFrameAnalyzer::samples_to_ms(size_t samples) const {
    return (samples * 1000) / m_sample_rate;
}

size_t ContinuousFrameAnalyzer::ms_to_samples(int64_t ms) const {
    return (ms * m_sample_rate) / 1000;
}

int ContinuousFrameAnalyzer::add_audio(const int16_t* samples, size_t n_samples) {
    if (n_samples == 0) return 0;
    
    // Append to buffer
    m_audio_buffer.insert(m_audio_buffer.end(), samples, samples + n_samples);
    m_total_samples_processed += n_samples;
    
    int frames_extracted = 0;
    int64_t current_ms = samples_to_ms(m_total_samples_processed);
    
    static int debug_call_count = 0;
    if (m_config.verbose && debug_call_count < 10) {
        fprintf(stderr, "[add_audio #%d] n_samples=%zu, buffer_size=%zu, total_ms=%lld, next_frame=%lld\n",
                ++debug_call_count, n_samples, m_audio_buffer.size(), current_ms, m_next_frame_ms);
    }
    
    // Extract frames at regular intervals (hop_ms)
    while (m_next_frame_ms <= current_ms) {
        // Need enough audio for the window
        int64_t window_start_ms = m_next_frame_ms - m_config.window_ms / 2;
        int64_t window_end_ms = m_next_frame_ms + m_config.window_ms / 2;
        
        // Can only extract if we've processed enough audio for the full window
        if (window_end_ms > current_ms) {
            // Not enough audio yet, wait for more
            break;
        }
        
        // Check if we have enough audio in buffer (accounting for trimming)
        int64_t buffer_start_ms = samples_to_ms(m_total_samples_processed - m_audio_buffer.size());
        int64_t buffer_end_ms = samples_to_ms(m_total_samples_processed);
        
        if (m_config.verbose && m_frames.size() < 3) {
            fprintf(stderr, "[window_check] next_frame=%lld, window=[%lld,%lld], buffer=[%lld,%lld], check=%d\n",
                    m_next_frame_ms, window_start_ms, window_end_ms, buffer_start_ms, buffer_end_ms,
                    (window_start_ms >= buffer_start_ms && window_end_ms <= buffer_end_ms));
        }
        
        if (window_start_ms >= buffer_start_ms && window_end_ms <= buffer_end_ms) {
            Frame frame = extract_frame_at_ms(m_next_frame_ms);
            m_frames.push_back(frame);
            frames_extracted++;
            
            if (m_config.verbose && frames_extracted <= 5) {
                fprintf(stderr, "[Frame] t=%lld ms, emb_dim=%zu\n", 
                        frame.t_start_ms, frame.embedding.size());
            }
        } else {
            // Buffer was trimmed too much, can't extract this frame
            if (m_config.verbose && m_frames.size() < 5) {
                fprintf(stderr, "[skip_frame] t=%lld: window [%lld,%lld] not in buffer [%lld,%lld]\n",
                        m_next_frame_ms, window_start_ms, window_end_ms, buffer_start_ms, buffer_end_ms);
            }
        }
        
        m_next_frame_ms += m_config.hop_ms;
        
        // Safety: if we're way ahead, stop
        if (m_next_frame_ms > current_ms + m_config.hop_ms * 10) break;
    }
    
    // Trim old frames to maintain history limit
    if (m_config.history_sec > 0) {
        int64_t cutoff_ms = current_ms - (m_config.history_sec * 1000);
        clear_old_frames(cutoff_ms);
    }
    
    // Trim audio buffer (keep last 2*window_ms for overlap)
    size_t samples_to_keep = ms_to_samples(m_config.window_ms * 2);
    if (m_audio_buffer.size() > samples_to_keep + ms_to_samples(m_config.hop_ms * 10)) {
        size_t trim = m_audio_buffer.size() - samples_to_keep;
        m_audio_buffer.erase(m_audio_buffer.begin(), m_audio_buffer.begin() + trim);
    }
    
    return frames_extracted;
}

ContinuousFrameAnalyzer::Frame ContinuousFrameAnalyzer::extract_frame_at_ms(int64_t center_ms) {
    Frame frame;
    frame.t_start_ms = center_ms - m_config.window_ms / 2;
    frame.t_end_ms = center_ms + m_config.window_ms / 2;
    frame.speaker_id = -1;  // Unknown initially
    frame.confidence = 0.0f;
    
    // Calculate which part of buffer to extract
    int64_t buffer_start_ms = samples_to_ms(m_total_samples_processed - m_audio_buffer.size());
    size_t offset_samples = ms_to_samples(frame.t_start_ms - buffer_start_ms);
    size_t window_samples = ms_to_samples(m_config.window_ms);
    
    // Bounds check
    if (offset_samples + window_samples > m_audio_buffer.size()) {
        // Not enough audio - return empty frame
        if (m_config.verbose) {
            fprintf(stderr, "[Frame] WARNING: Not enough audio for frame at %lld ms\n", center_ms);
        }
        return frame;
    }
    
    // Extract embedding from this window
    const int16_t* window_audio = m_audio_buffer.data() + offset_samples;
    frame.embedding = compute_speaker_embedding(window_audio, window_samples, m_sample_rate);
    
    return frame;
}

std::vector<ContinuousFrameAnalyzer::Frame> 
ContinuousFrameAnalyzer::get_frames_in_range(int64_t t0_ms, int64_t t1_ms) const {
    std::vector<Frame> result;
    
    for (const auto& frame : m_frames) {
        // Frame overlaps with range if:
        // frame.t_start_ms < t1_ms AND frame.t_end_ms > t0_ms
        if (frame.t_start_ms < t1_ms && frame.t_end_ms > t0_ms) {
            result.push_back(frame);
        }
    }
    
    return result;
}

const ContinuousFrameAnalyzer::Frame* ContinuousFrameAnalyzer::get_latest_frame() const {
    if (m_frames.empty()) return nullptr;
    return &m_frames.back();
}

void ContinuousFrameAnalyzer::clear_old_frames(int64_t before_ms) {
    // Remove frames older than before_ms
    while (!m_frames.empty() && m_frames.front().t_end_ms < before_ms) {
        m_frames.pop_front();
    }
}

void ContinuousFrameAnalyzer::update_speaker_ids(const std::vector<int>& speaker_ids) {
    if (speaker_ids.size() != m_frames.size()) {
        fprintf(stderr, "[ContinuousFrameAnalyzer] ERROR: speaker_ids size mismatch: %zu vs %zu frames\n",
                speaker_ids.size(), m_frames.size());
        return;
    }
    
    for (size_t i = 0; i < m_frames.size(); ++i) {
        m_frames[i].speaker_id = speaker_ids[i];
    }
}

int64_t ContinuousFrameAnalyzer::duration_ms() const {
    if (m_frames.empty()) return 0;
    return m_frames.back().t_end_ms - m_frames.front().t_start_ms;
}

} // namespace diar
