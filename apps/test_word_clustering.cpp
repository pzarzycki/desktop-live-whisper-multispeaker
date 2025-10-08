/**
 * Test word-level clustering directly (not frame-based)
 * 
 * Approach:
 * 1. Extract word timestamps from Whisper
 * 2. For each word, compute embedding from overlapping frames
 * 3. Cluster words directly using their embeddings
 * 4. No frame clustering needed!    // Cluster words directly (sequential, respects time)
    auto assignments = cluster_words(all_words, word_embeddings, 2, 0.30f); */

#include "asr/whisper_backend.hpp"
#include "audio/file_capture.hpp"
#include "diar/speaker_cluster.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <map>

// Compute average embedding for a word from overlapping frames
std::vector<float> get_word_embedding(
    const asr::WhisperWord& word,
    const std::deque<diar::ContinuousFrameAnalyzer::Frame>& all_frames
) {
    std::vector<float> avg_embedding;
    int count = 0;
    
    for (const auto& frame : all_frames) {
        // Check if frame overlaps word
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
    
    // Average
    if (count > 1) {
        for (auto& val : avg_embedding) {
            val /= count;
        }
    }
    
    return avg_embedding;
}

// Cosine similarity
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

// Sequential clustering: respect time order and speaker continuity
std::vector<int> cluster_words(
    const std::vector<asr::WhisperWord>& words,
    const std::vector<std::vector<float>>& word_embeddings,
    int max_speakers = 2,
    float threshold = 0.50f
) {
    std::vector<int> assignments(words.size(), 0);
    
    if (words.empty()) return assignments;
    
    std::cout << "\n📊 SEQUENTIAL WORD-LEVEL CLUSTERING\n";
    std::cout << "Total words: " << words.size() << "\n";
    std::cout << "Max speakers: " << max_speakers << "\n";
    std::cout << "Threshold: " << std::fixed << std::setprecision(2) << threshold << "\n\n";
    
    // Track speaker embeddings (running average)
    std::vector<std::vector<float>> speaker_embeddings;
    std::vector<int> speaker_counts;
    
    // First word = Speaker 0
    speaker_embeddings.push_back(word_embeddings[0]);
    speaker_counts.push_back(1);
    assignments[0] = 0;
    
    int current_speaker = 0;
    std::cout << "Word 0 \"" << words[0].word << "\" @ " << words[0].t0_ms << "ms → S0 (first)\n";
    
    // Process remaining words sequentially
    for (size_t i = 1; i < word_embeddings.size(); ++i) {
        if (word_embeddings[i].empty()) {
            assignments[i] = current_speaker;
            continue;
        }
        
        // Compare to current speaker
        float current_sim = cosine_similarity(word_embeddings[i], speaker_embeddings[current_speaker]);
        
        // Debug: show similarities
        if (i < 15) {  // First 15 words
            std::cout << "Word " << i << " \"" << words[i].word << "\" sim_to_S" 
                      << current_speaker << "=" << std::fixed << std::setprecision(3) << current_sim;
        }
        
        // If similar to current speaker, continue the turn
        if (current_sim >= threshold) {
            assignments[i] = current_speaker;
            if (i < 15) std::cout << " → SAME\n";
            
            // Update running average for current speaker
            for (size_t d = 0; d < word_embeddings[i].size(); ++d) {
                speaker_embeddings[current_speaker][d] = 
                    (speaker_embeddings[current_speaker][d] * speaker_counts[current_speaker] + word_embeddings[i][d]) /
                    (speaker_counts[current_speaker] + 1);
            }
            speaker_counts[current_speaker]++;
        } else {
            // Not similar to current speaker - check other speakers
            int best_speaker = -1;
            float best_sim = threshold;
            
            for (size_t s = 0; s < speaker_embeddings.size(); ++s) {
                if (static_cast<int>(s) == current_speaker) continue;
                
                float sim = cosine_similarity(word_embeddings[i], speaker_embeddings[s]);
                if (sim > best_sim) {
                    best_sim = sim;
                    best_speaker = static_cast<int>(s);
                }
            }
            
            if (best_speaker >= 0) {
                // Similar to known speaker - switch to them
                current_speaker = best_speaker;
                assignments[i] = current_speaker;
                
                std::cout << "Word " << i << " \"" << words[i].word << "\" @ " 
                          << words[i].t0_ms << "ms → S" << current_speaker 
                          << " (switch, sim=" << std::fixed << std::setprecision(3) << best_sim << ")\n";
                
                // Update running average
                for (size_t d = 0; d < word_embeddings[i].size(); ++d) {
                    speaker_embeddings[current_speaker][d] = 
                        (speaker_embeddings[current_speaker][d] * speaker_counts[current_speaker] + word_embeddings[i][d]) /
                        (speaker_counts[current_speaker] + 1);
                }
                speaker_counts[current_speaker]++;
            } else if (static_cast<int>(speaker_embeddings.size()) < max_speakers) {
                // Create new speaker
                speaker_embeddings.push_back(word_embeddings[i]);
                speaker_counts.push_back(1);
                current_speaker = static_cast<int>(speaker_embeddings.size()) - 1;
                assignments[i] = current_speaker;
                
                std::cout << "Word " << i << " \"" << words[i].word << "\" @ " 
                          << words[i].t0_ms << "ms → S" << current_speaker 
                          << " (new speaker, sim=" << std::fixed << std::setprecision(3) << current_sim << ")\n";
            } else {
                // Max speakers reached, assign to current
                assignments[i] = current_speaker;
                
                // Update anyway (centroid drift is OK for last resort)
                for (size_t d = 0; d < word_embeddings[i].size(); ++d) {
                    speaker_embeddings[current_speaker][d] = 
                        (speaker_embeddings[current_speaker][d] * speaker_counts[current_speaker] + word_embeddings[i][d]) /
                        (speaker_counts[current_speaker] + 1);
                }
                speaker_counts[current_speaker]++;
            }
        }
    }
    
    std::cout << "\nFound " << speaker_embeddings.size() << " speakers\n\n";
    
    return assignments;
}

void print_with_speakers(
    const std::vector<asr::WhisperSegmentWithWords>& segments,
    const std::vector<int>& all_assignments
) {
    std::cout << "\n============================================================\n";
    std::cout << "TRANSCRIPTION WITH SPEAKERS\n";
    std::cout << "============================================================\n\n";
    
    size_t word_idx = 0;
    for (const auto& seg : segments) {
        int current_speaker = all_assignments[word_idx];
        std::cout << "[S" << current_speaker << "] ";
        
        for (const auto& word : seg.words) {
            if (word_idx < all_assignments.size()) {
                if (all_assignments[word_idx] != current_speaker) {
                    std::cout << "\n[S" << all_assignments[word_idx] << "] ";
                    current_speaker = all_assignments[word_idx];
                }
            }
            std::cout << word.word << " ";
            word_idx++;
        }
        std::cout << "\n";
    }
    
    std::cout << "\n============================================================\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <audio_16k.wav>\n";
        std::cerr << "Note: Input must be 16kHz mono\n";
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
    
    // Extract frames (no clustering!)
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
    std::cout << "✓ Computing word embeddings...\n";
    std::vector<std::vector<float>> word_embeddings;
    for (const auto& word : all_words) {
        word_embeddings.push_back(get_word_embedding(word, frames));
    }
    
    // Cluster words directly (sequential, respects time)
    auto assignments = cluster_words(all_words, word_embeddings, 2, 0.30f);
    
    // Simple smoothing: merge turns shorter than 3 words
    std::cout << "\n🔧 SMOOTHING SPEAKER TURNS (min 3 words per turn)\n";
    int changes = 0;
    for (size_t i = 1; i < assignments.size() - 1; ++i) {
        // If this word is different from neighbors, and neighbors are same
        if (assignments[i] != assignments[i-1] && 
            assignments[i] != assignments[i+1] &&
            assignments[i-1] == assignments[i+1]) {
            std::cout << "  Merging isolated word " << i << " \"" << all_words[i].word 
                      << "\" (S" << assignments[i] << " → S" << assignments[i-1] << ")\n";
            assignments[i] = assignments[i-1];
            changes++;
        }
    }
    
    // Merge single-word turns at boundaries
    for (size_t i = 1; i < assignments.size(); ++i) {
        if (assignments[i] != assignments[i-1]) {
            // Check if this is a very short turn (1-2 words)
            size_t turn_length = 1;
            size_t j = i + 1;
            while (j < assignments.size() && assignments[j] == assignments[i]) {
                turn_length++;
                j++;
            }
            
            if (turn_length <= 2) {
                // Merge with previous speaker
                std::cout << "  Merging short turn at word " << i << " (" << turn_length 
                          << " words, S" << assignments[i] << " → S" << assignments[i-1] << ")\n";
                for (size_t k = i; k < j; ++k) {
                    assignments[k] = assignments[i-1];
                    changes++;
                }
            }
        }
    }
    std::cout << "Total changes: " << changes << "\n";
    
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
    print_with_speakers(segments, assignments);
    
    std::cout << "\n✅ Complete!\n";
    return 0;
}
