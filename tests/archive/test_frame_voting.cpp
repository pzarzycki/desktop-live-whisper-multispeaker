/**
 * FRAME-BY-FRAME analysis within segments
 * 
 * Key insight: A segment might start with one speaker but transition to another.
 * Analyze frame-level similarities throughout each segment to detect this.
 */

#include "asr/whisper_backend.hpp"
#include "audio/file_capture.hpp"
#include "diar/speaker_cluster.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <map>
#include <algorithm>

float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || b.empty() || a.size() != b.size()) return 0.0f;
    
    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    
    if (norm_a == 0.0f || norm_b == 0.0f) return 0.0f;
    return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

// Get all frames that overlap a segment
std::vector<const diar::ContinuousFrameAnalyzer::Frame*> get_segment_frames(
    const asr::WhisperSegmentWithWords& segment,
    const std::deque<diar::ContinuousFrameAnalyzer::Frame>& all_frames
) {
    std::vector<const diar::ContinuousFrameAnalyzer::Frame*> frames;
    
    for (const auto& frame : all_frames) {
        if (frame.t_end_ms > segment.t0_ms && frame.t_start_ms < segment.t1_ms) {
            frames.push_back(&frame);
        }
    }
    
    return frames;
}

// Assign speaker to segment based on frame-level voting
int assign_segment_by_frame_voting(
    int segment_idx,
    const asr::WhisperSegmentWithWords& segment,
    const std::vector<const diar::ContinuousFrameAnalyzer::Frame*>& frames,
    const std::vector<float>& speaker0_embedding,
    const std::vector<float>& speaker1_embedding
) {
    if (speaker1_embedding.empty()) {
        // Only have S0, can't vote yet
        return 0;
    }
    
    std::cout << "\n  Segment " << segment_idx << ": \"" << segment.text << "\"\n";
    std::cout << "  " << frames.size() << " frames:\n";
    
    int votes_s0 = 0;
    int votes_s1 = 0;
    
    for (size_t i = 0; i < frames.size(); ++i) {
        float sim_s0 = cosine_similarity(frames[i]->embedding, speaker0_embedding);
        float sim_s1 = cosine_similarity(frames[i]->embedding, speaker1_embedding);
        
        int vote = (sim_s1 > sim_s0) ? 1 : 0;
        if (vote == 0) votes_s0++; else votes_s1++;
        
        std::cout << "    Frame " << i << " @ " << frames[i]->t_start_ms << "ms: "
                  << "sim_S0=" << std::fixed << std::setprecision(3) << sim_s0
                  << ", sim_S1=" << sim_s1
                  << " → S" << vote << "\n";
    }
    
    int winner = (votes_s1 > votes_s0) ? 1 : 0;
    std::cout << "  VOTES: S0=" << votes_s0 << ", S1=" << votes_s1 
              << " → Winner: S" << winner << "\n";
    
    return winner;
}

std::vector<int> assign_speakers_by_frame_voting(
    const std::vector<asr::WhisperSegmentWithWords>& segments,
    const std::deque<diar::ContinuousFrameAnalyzer::Frame>& all_frames,
    float init_threshold = 0.85f
) {
    std::vector<int> assignments(segments.size(), 0);
    
    if (segments.empty()) return assignments;
    
    std::cout << "\n📊 FRAME-LEVEL VOTING WITHIN SEGMENTS\n";
    std::cout << "Total segments: " << segments.size() << "\n";
    std::cout << "Init threshold: " << init_threshold << "\n";
    
    // Get speaker embeddings from first two segments
    auto seg0_frames = get_segment_frames(segments[0], all_frames);
    std::vector<float> speaker0_embedding;
    for (const auto* frame : seg0_frames) {
        if (speaker0_embedding.empty()) {
            speaker0_embedding = frame->embedding;
        } else {
            for (size_t i = 0; i < frame->embedding.size(); ++i) {
                speaker0_embedding[i] += frame->embedding[i];
            }
        }
    }
    for (auto& v : speaker0_embedding) v /= seg0_frames.size();
    
    assignments[0] = 0;
    std::cout << "\nSegment 0: \"" << segments[0].text << "\" → S0 (initial)\n";
    std::cout << "  Used " << seg0_frames.size() << " frames for S0 embedding\n";
    
    // Find S1 by comparing segment 1 to S0
    auto seg1_frames = get_segment_frames(segments[1], all_frames);
    std::vector<float> seg1_avg;
    for (const auto* frame : seg1_frames) {
        if (seg1_avg.empty()) {
            seg1_avg = frame->embedding;
        } else {
            for (size_t i = 0; i < frame->embedding.size(); ++i) {
                seg1_avg[i] += frame->embedding[i];
            }
        }
    }
    for (auto& v : seg1_avg) v /= seg1_frames.size();
    
    float sim = cosine_similarity(seg1_avg, speaker0_embedding);
    std::cout << "\nSegment 1: \"" << segments[1].text << "\"\n";
    std::cout << "  Avg similarity to S0: " << std::fixed << std::setprecision(3) << sim << "\n";
    
    std::vector<float> speaker1_embedding;
    if (sim < init_threshold) {
        // Different speaker
        speaker1_embedding = seg1_avg;
        assignments[1] = 1;
        std::cout << "  → S1 (NEW SPEAKER)\n";
        std::cout << "  Used " << seg1_frames.size() << " frames for S1 embedding\n";
    } else {
        assignments[1] = 0;
        std::cout << "  → S0 (same speaker)\n";
    }
    
    // Now vote on remaining segments
    for (size_t i = 2; i < segments.size(); ++i) {
        auto frames = get_segment_frames(segments[i], all_frames);
        assignments[i] = assign_segment_by_frame_voting(
            i, segments[i], frames, speaker0_embedding, speaker1_embedding
        );
    }
    
    return assignments;
}

void print_with_speakers(
    const std::vector<asr::WhisperSegmentWithWords>& segments,
    const std::vector<int>& assignments
) {
    std::cout << "\n============================================================\n";
    std::cout << "TRANSCRIPTION WITH SPEAKERS\n";
    std::cout << "============================================================\n\n";
    
    for (size_t i = 0; i < segments.size(); ++i) {
        std::cout << "[S" << assignments[i] << "] " << segments[i].text << "\n";
    }
    
    std::cout << "\n============================================================\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <audio_16k.wav> [init_threshold]\n";
        return 1;
    }
    
    const std::string audio_path = argv[1];
    float init_threshold = (argc > 2) ? std::stof(argv[2]) : 0.85f;
    
    // Load Whisper
    asr::WhisperBackend whisper;
    if (!whisper.load_model("tiny.en")) {
        std::cerr << "Failed to load Whisper model\n";
        return 1;
    }
    std::cout << "✓ Whisper loaded\n";
    
    // Load audio
    audio::FileCapture fileCap;
    if (!fileCap.start_from_wav(audio_path)) {
        std::cerr << "Failed to load audio: " << audio_path << "\n";
        return 1;
    }
    std::cout << "✓ Audio loaded: " << fileCap.duration_seconds() << "s @ " 
              << fileCap.sample_rate() << " Hz\n";
    
    // Read audio
    std::vector<int16_t> audio_samples;
    while (true) {
        auto chunk = fileCap.read_chunk();
        if (chunk.empty()) break;
        audio_samples.insert(audio_samples.end(), chunk.begin(), chunk.end());
    }
    
    // Extract frame embeddings
    diar::ContinuousFrameAnalyzer::Config frame_config;
    frame_config.embedding_mode = diar::EmbeddingMode::NeuralONNX;
    frame_config.onnx_model_path = "models/campplus_voxceleb.onnx";
    frame_config.hop_ms = 250;
    frame_config.window_ms = 1000;
    frame_config.verbose = false;
    
    diar::ContinuousFrameAnalyzer frame_analyzer(fileCap.sample_rate(), frame_config);
    std::cout << "✓ Extracting frame embeddings (every 250ms)...\n";
    frame_analyzer.add_audio(audio_samples.data(), audio_samples.size());
    std::cout << "✓ Extracted " << frame_analyzer.frame_count() << " frames\n";
    
    const auto& frames = frame_analyzer.get_all_frames();
    
    // Transcribe
    std::cout << "✓ Transcribing...\n";
    auto segments = whisper.transcribe_chunk_with_words(
        audio_samples.data(),
        audio_samples.size()
    );
    std::cout << "✓ Got " << segments.size() << " segments\n";
    
    // Assign speakers by frame-level voting
    auto assignments = assign_speakers_by_frame_voting(segments, frames, init_threshold);
    
    // Statistics
    std::map<int, int> counts;
    for (int s : assignments) counts[s]++;
    
    std::cout << "\n📈 STATISTICS:\n";
    for (const auto& [speaker, count] : counts) {
        float pct = 100.0f * count / assignments.size();
        std::cout << "  Speaker " << speaker << ": " << count << " segments (" 
                  << std::fixed << std::setprecision(1) << pct << "%)\n";
    }
    
    // Print results
    print_with_speakers(segments, assignments);
    
    std::cout << "\n✅ Complete!\n";
    return 0;
}
