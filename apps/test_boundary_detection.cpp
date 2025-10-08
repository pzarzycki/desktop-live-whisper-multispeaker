/**
 * Word-level speaker assignment - BOUNDARY DETECTION APPROACH
 * 
 * Instead of sequential decisions, find speaker boundaries by detecting
 * large drops in word-to-word similarity.
 */

#include "asr/whisper_backend.hpp"
#include "audio/file_capture.hpp"
#include "diar/speaker_cluster.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <map>
#include <vector>
#include <algorithm>

// Compute average embedding for a word from overlapping frames
std::vector<float> get_word_embedding(
    const asr::WhisperWord& word,
    const std::deque<diar::ContinuousFrameAnalyzer::Frame>& all_frames
) {
    std::vector<float> avg_embedding;
    int count = 0;
    
    for (const auto& frame : all_frames) {
        if (frame.t_end_ms > word.t0_ms && frame.t_start_ms < word.t1_ms) {
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

// Find speaker boundaries by detecting similarity drops
std::vector<int> assign_speakers_by_boundaries(
    const std::vector<asr::WhisperWord>& words,
    const std::vector<std::vector<float>>& word_embeddings
) {
    std::vector<int> assignments(words.size(), 0);
    
    if (words.size() < 4) {
        // Too few words, assign all to S0
        return assignments;
    }
    
    std::cout << "\n📊 BOUNDARY DETECTION APPROACH\n";
    std::cout << "Total words: " << words.size() << "\n\n";
    
    // Compute word-to-word similarities
    std::vector<float> similarities;
    std::cout << "Word-to-word similarities:\n";
    for (size_t i = 0; i < word_embeddings.size() - 1; ++i) {
        float sim = cosine_similarity(word_embeddings[i], word_embeddings[i+1]);
        similarities.push_back(sim);
        std::cout << "  " << i << "→" << (i+1) << " \"" << words[i].word << "\"→\"" 
                  << words[i+1].word << "\" sim=" << std::fixed << std::setprecision(3) 
                  << sim << "\n";
    }
    
    // Find the biggest drops (potential boundaries)
    std::vector<std::pair<int, float>> drops;  // (position, similarity)
    for (size_t i = 0; i < similarities.size(); ++i) {
        drops.push_back({static_cast<int>(i), similarities[i]});
    }
    
    // Sort by similarity (lowest first = biggest drops)
    std::sort(drops.begin(), drops.end(), 
              [](const auto& a, const auto& b) { return a.second < b.second; });
    
    std::cout << "\nBiggest drops (candidate boundaries):\n";
    for (size_t i = 0; i < std::min(size_t(5), drops.size()); ++i) {
        int pos = drops[i].first;
        float sim = drops[i].second;
        std::cout << "  #" << (i+1) << ": position " << pos << "→" << (pos+1) 
                  << " \"" << words[pos].word << "\"→\"" << words[pos+1].word 
                  << "\" @ " << words[pos+1].t0_ms << "ms, sim=" << std::fixed 
                  << std::setprecision(3) << sim << "\n";
    }
    
    // Take top 3 drops as boundaries, sort by position
    std::vector<int> boundaries;
    for (size_t i = 0; i < std::min(size_t(3), drops.size()); ++i) {
        boundaries.push_back(drops[i].first + 1);  // Boundary is AFTER this word
    }
    std::sort(boundaries.begin(), boundaries.end());
    
    std::cout << "\nSelected boundaries: ";
    for (int b : boundaries) {
        std::cout << b << " ";
    }
    std::cout << "\n\n";
    
    // Assign speakers alternating at boundaries
    int current_speaker = 0;
    size_t next_boundary_idx = 0;
    
    for (size_t i = 0; i < words.size(); ++i) {
        if (next_boundary_idx < boundaries.size() && 
            static_cast<int>(i) == boundaries[next_boundary_idx]) {
            // Reached a boundary - switch speaker
            current_speaker = (current_speaker == 0) ? 1 : 0;
            next_boundary_idx++;
            std::cout << "Boundary at word " << i << " \"" << words[i].word 
                      << "\" @ " << words[i].t0_ms << "ms → S" << current_speaker << "\n";
        }
        assignments[i] = current_speaker;
    }
    
    return assignments;
}

void print_with_speakers(
    const std::vector<asr::WhisperWord>& words,
    const std::vector<int>& assignments
) {
    std::cout << "\n============================================================\n";
    std::cout << "TRANSCRIPTION WITH SPEAKERS\n";
    std::cout << "============================================================\n\n";
    
    int current_speaker = assignments[0];
    std::cout << "[S" << current_speaker << "] ";
    
    for (size_t i = 0; i < words.size(); ++i) {
        if (assignments[i] != current_speaker) {
            std::cout << "\n[S" << assignments[i] << "] ";
            current_speaker = assignments[i];
        }
        std::cout << words[i].word << " ";
    }
    std::cout << "\n\n============================================================\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <audio_16k.wav>\n";
        return 1;
    }
    
    const std::string audio_path = argv[1];
    
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
    std::cout << "✓ Transcribing with word timestamps...\n";
    auto segments = whisper.transcribe_chunk_with_words(
        audio_samples.data(),
        audio_samples.size()
    );
    std::cout << "✓ Got " << segments.size() << " segments\n";
    
    // Collect all words
    std::vector<asr::WhisperWord> all_words;
    for (const auto& seg : segments) {
        all_words.insert(all_words.end(), seg.words.begin(), seg.words.end());
    }
    std::cout << "✓ Total words: " << all_words.size() << "\n";
    
    // Get embeddings for each word
    std::cout << "✓ Computing word embeddings from overlapping frames...\n";
    std::vector<std::vector<float>> word_embeddings;
    for (const auto& word : all_words) {
        word_embeddings.push_back(get_word_embedding(word, frames));
    }
    
    // Find boundaries and assign speakers
    auto assignments = assign_speakers_by_boundaries(all_words, word_embeddings);
    
    // Statistics
    std::map<int, int> counts;
    for (int s : assignments) counts[s]++;
    
    std::cout << "\n📈 STATISTICS:\n";
    for (const auto& [speaker, count] : counts) {
        float pct = 100.0f * count / assignments.size();
        std::cout << "  Speaker " << speaker << ": " << count << " words (" 
                  << std::fixed << std::setprecision(1) << pct << "%)\n";
    }
    
    // Print results
    print_with_speakers(all_words, assignments);
    
    std::cout << "\n✅ Complete!\n";
    return 0;
}
