/**
 * SEGMENT-LEVEL speaker assignment
 * 
 * Key insight: Whisper segments already align with speaker turns!
 * Just compute segment embeddings and cluster those.
 */

#include "asr/whisper_backend.hpp"
#include "audio/file_capture.hpp"
#include "diar/speaker_cluster.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <map>

// Compute average embedding for a segment from all its words
std::vector<float> get_segment_embedding(
    const asr::WhisperSegmentWithWords& segment,
    const std::deque<diar::ContinuousFrameAnalyzer::Frame>& all_frames
) {
    std::vector<float> avg_embedding;
    int count = 0;
    
    // Use all frames that overlap this segment
    int seg_start_ms = static_cast<int>(segment.t0_ms);
    int seg_end_ms = static_cast<int>(segment.t1_ms);
    
    for (const auto& frame : all_frames) {
        if (frame.t_end_ms > seg_start_ms && frame.t_start_ms < seg_end_ms) {
            if (avg_embedding.empty()) {
                avg_embedding = frame.embedding;
                count = 1;
            } else {
                for (size_t i = 0; i < frame.embedding.size(); ++i) {
                    avg_embedding[i] += frame.embedding[i];
                }
                count++;
            }
        }
    }
    
    if (count > 1) {
        for (auto& val : avg_embedding) {
            val /= count;
        }
    }
    
    return avg_embedding;
}

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

// Sequential speaker assignment at SEGMENT level
std::vector<int> assign_speakers_to_segments(
    const std::vector<asr::WhisperSegmentWithWords>& segments,
    const std::vector<std::vector<float>>& segment_embeddings,
    float change_threshold = 0.85f
) {
    std::vector<int> assignments(segments.size(), 0);
    
    if (segments.empty()) return assignments;
    
    std::cout << "\n📊 SEGMENT-LEVEL SPEAKER ASSIGNMENT\n";
    std::cout << "Total segments: " << segments.size() << "\n";
    std::cout << "Change threshold: " << std::fixed << std::setprecision(2) 
              << change_threshold << "\n\n";
    
    // Start with segment 0 as Speaker 0
    int current_speaker = 0;
    std::vector<float> speaker0_embedding = segment_embeddings[0];
    std::vector<float> speaker1_embedding;  // Will be set when S1 is first encountered
    
    assignments[0] = 0;
    std::cout << "Segment 0: " << segments[0].text << " → S0 (initial)\n";
    
    // Process each segment sequentially
    for (size_t i = 1; i < segment_embeddings.size(); ++i) {
        if (segment_embeddings[i].empty()) {
            assignments[i] = current_speaker;
            continue;
        }
        
        // Compare to both speakers (if we have both)
        std::vector<float>& current_embedding = (current_speaker == 0) ? speaker0_embedding : speaker1_embedding;
        float sim_current = cosine_similarity(segment_embeddings[i], current_embedding);
        
        std::cout << "Segment " << i << ": \"" << segments[i].text << "\" @ " 
                  << std::fixed << std::setprecision(2) << (segments[i].t0_ms / 1000.0) 
                  << "s, sim_to_S" << current_speaker << "=" << std::setprecision(3) << sim_current;
        
        // Decision: which speaker is this most similar to?
        int best_speaker = current_speaker;
        
        if (!speaker1_embedding.empty()) {
            // We have both speakers - compare to both
            float sim_s0 = cosine_similarity(segment_embeddings[i], speaker0_embedding);
            float sim_s1 = cosine_similarity(segment_embeddings[i], speaker1_embedding);
            std::cout << ", sim_to_S0=" << sim_s0 << ", sim_to_S1=" << sim_s1;
            
            // Pick the best match
            best_speaker = (sim_s1 > sim_s0) ? 1 : 0;
        } else if (sim_current < change_threshold) {
            // First time encountering dissimilar segment - must be new speaker
            best_speaker = 1;
            speaker1_embedding = segment_embeddings[i];
        }
        
        if (best_speaker != current_speaker) {
            std::cout << " → S" << best_speaker << " (CHANGE)\n";
            current_speaker = best_speaker;
        } else {
            std::cout << " → S" << best_speaker << " (continue)\n";
        }
        
        assignments[i] = best_speaker;
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
        std::cerr << "Usage: " << argv[0] << " <audio_16k.wav> [change_threshold]\n";
        std::cerr << "  change_threshold: 0.0-1.0, default 0.85\n";
        return 1;
    }
    
    const std::string audio_path = argv[1];
    float change_threshold = (argc > 2) ? std::stof(argv[2]) : 0.85f;
    
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
    
    // Transcribe with word timestamps
    std::cout << "✓ Transcribing...\n";
    auto segments = whisper.transcribe_chunk_with_words(
        audio_samples.data(),
        audio_samples.size()
    );
    std::cout << "✓ Got " << segments.size() << " segments\n\n";
    
    // Get embedding for each segment
    std::cout << "✓ Computing segment embeddings...\n";
    std::vector<std::vector<float>> segment_embeddings;
    for (const auto& seg : segments) {
        segment_embeddings.push_back(get_segment_embedding(seg, frames));
    }
    
    // Assign speakers sequentially
    auto assignments = assign_speakers_to_segments(segments, segment_embeddings, change_threshold);
    
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
