/**
 * Word-level speaker assignment - CLEAN SEQUENTIAL APPROACH
 * 
 * Key insight: Speakers alternate in turns, so we process words sequentially
 * and detect speaker changes by comparing each word to the current turn's embedding.
 */

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

// Sequential speaker assignment
std::vector<int> assign_speakers_sequential(
    const std::vector<asr::WhisperWord>& words,
    const std::vector<std::vector<float>>& word_embeddings,
    float change_threshold = 0.70f,  // When similarity drops below this, consider speaker change
    int min_turn_words = 3           // Minimum words per turn before allowing change
) {
    std::vector<int> assignments(words.size(), 0);
    
    if (words.empty()) return assignments;
    
    std::cout << "\n📊 SEQUENTIAL SPEAKER ASSIGNMENT\n";
    std::cout << "Total words: " << words.size() << "\n";
    std::cout << "Change threshold: " << std::fixed << std::setprecision(2) 
              << change_threshold << " (similarity below this = potential change)\n";
    std::cout << "Min turn words: " << min_turn_words << "\n\n";
    
    // Start with first word as Speaker 0
    int current_speaker = 0;
    std::vector<float> current_turn_embedding = word_embeddings[0];
    int words_in_turn = 1;
    
    assignments[0] = 0;
    std::cout << "Word 0: \"" << words[0].word << "\" @ " << words[0].t0_ms 
              << "ms → S0 (initial)\n";
    
    // Process each word sequentially
    for (size_t i = 1; i < word_embeddings.size(); ++i) {
        if (word_embeddings[i].empty()) {
            assignments[i] = current_speaker;
            continue;
        }
        
        // Compare this word to the current turn's embedding
        float similarity = cosine_similarity(word_embeddings[i], current_turn_embedding);
        
        bool should_change = false;
        
        // Decision logic: change speaker if similarity is low AND we've had enough words
        if (similarity < change_threshold && words_in_turn >= min_turn_words) {
            should_change = true;
        }
        
        if (should_change) {
            // Speaker change!
            int new_speaker = (current_speaker == 0) ? 1 : 0;  // Alternate
            
            std::cout << "Word " << i << ": \"" << words[i].word << "\" @ " 
                      << words[i].t0_ms << "ms, sim=" << std::fixed << std::setprecision(3) 
                      << similarity << " → S" << new_speaker 
                      << " (CHANGE after " << words_in_turn << " words)\n";
            
            // Start new turn
            current_speaker = new_speaker;
            current_turn_embedding = word_embeddings[i];
            words_in_turn = 1;
            assignments[i] = current_speaker;
        } else {
            // Continue current turn
            assignments[i] = current_speaker;
            words_in_turn++;
            
            // Gentle rolling average to track turn's embedding (prevents drift but allows some adaptation)
            for (size_t d = 0; d < current_turn_embedding.size(); ++d) {
                current_turn_embedding[d] = 0.9f * current_turn_embedding[d] + 
                                            0.1f * word_embeddings[i][d];
            }
            
            if (i < 10 || similarity < change_threshold + 0.05f) {
                std::cout << "Word " << i << ": \"" << words[i].word 
                          << "\" sim=" << std::fixed << std::setprecision(3) 
                          << similarity << " → S" << current_speaker << " (continue)\n";
            }
        }
    }
    
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
    int current_speaker = all_assignments[0];
    std::cout << "[S" << current_speaker << "] ";
    
    for (const auto& seg : segments) {
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
    }
    std::cout << "\n\n============================================================\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <audio_16k.wav> [change_threshold] [min_turn_words]\n";
        std::cerr << "  change_threshold: 0.0-1.0, default 0.70 (lower = more sensitive to changes)\n";
        std::cerr << "  min_turn_words: minimum words per turn, default 3\n";
        return 1;
    }
    
    const std::string audio_path = argv[1];
    float change_threshold = (argc > 2) ? std::stof(argv[2]) : 0.70f;
    int min_turn_words = (argc > 3) ? std::stoi(argv[3]) : 3;
    
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
    
    // Assign speakers sequentially
    auto assignments = assign_speakers_sequential(all_words, word_embeddings, 
                                                   change_threshold, min_turn_words);
    
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
