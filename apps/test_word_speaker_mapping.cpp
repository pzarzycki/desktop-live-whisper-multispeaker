#include "asr/whisper_backend.hpp"
#include "audio/file_capture.hpp"
#include "diar/speaker_cluster.hpp"
#include <iostream>
#include <iomanip>
#include <deque>

// Helper: Map word timestamps to speaker IDs based on frame overlap
std::vector<int> map_words_to_speakers(
    const std::vector<asr::WhisperWord>& words,
    const std::deque<diar::ContinuousFrameAnalyzer::Frame>& all_frames
) {
    std::vector<int> word_speakers(words.size(), -1);
    
    for (size_t i = 0; i < words.size(); ++i) {
        const auto& word = words[i];
        
        // Find frames that overlap this word's time window
        int votes[2] = {0, 0};  // Count votes for speaker 0 and 1
        int total_votes = 0;
        
        for (const auto& frame : all_frames) {
            // Check if frame overlaps word
            if (frame.t_end_ms <= word.t0_ms) continue;  // Frame ends before word
            if (frame.t_start_ms >= word.t1_ms) break;   // Frame starts after word (assuming sorted)
            
            // Frame overlaps word
            if (frame.speaker_id >= 0 && frame.speaker_id < 2) {
                votes[frame.speaker_id]++;
                total_votes++;
            }
        }
        
        // Assign speaker based on majority vote
        if (total_votes > 0) {
            word_speakers[i] = (votes[0] > votes[1]) ? 0 : 1;
        }
    }
    
    return word_speakers;
}

// Helper: Print segments with word-level speaker labels
void print_with_speaker_changes(
    const std::vector<asr::WhisperSegmentWithWords>& segments,
    const std::deque<diar::ContinuousFrameAnalyzer::Frame>& all_frames
) {
    std::cout << "\n============================================================\n";
    std::cout << "WORD-LEVEL SPEAKER ASSIGNMENT\n";
    std::cout << "============================================================\n\n";
    
    for (const auto& seg : segments) {
        auto word_speakers = map_words_to_speakers(seg.words, all_frames);
        
        if (seg.words.empty()) {
            std::cout << "[S?] " << seg.text << "\n";
            continue;
        }
        
        // Print words grouped by speaker
        int current_speaker = word_speakers[0];
        std::cout << "[S" << current_speaker << "] ";
        
        for (size_t i = 0; i < seg.words.size(); ++i) {
            if (word_speakers[i] != current_speaker) {
                // Speaker changed
                std::cout << "\n[S" << word_speakers[i] << "] ";
                current_speaker = word_speakers[i];
            }
            std::cout << seg.words[i].word << " ";
        }
        std::cout << "\n";
    }
    
    std::cout << "\n============================================================\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <audio_16k.wav>\n";
        std::cerr << "Note: Input must be 16kHz mono (Whisper requirement)\n";
        return 1;
    }

    // Load Whisper model
    asr::WhisperBackend whisper;
    if (!whisper.load_model("tiny.en")) {
        std::cerr << "Failed to load Whisper model\n";
        return 1;
    }
    std::cout << "✓ Whisper model loaded\n";

    // Load audio
    const std::string audio_path = argv[1];
    audio::FileCapture fileCap;
    if (!fileCap.start_from_wav(audio_path)) {
        std::cerr << "Failed to load audio file: " << audio_path << "\n";
        return 1;
    }
    
    std::cout << "✓ Audio loaded: " << fileCap.duration_seconds() << " seconds @ " 
              << fileCap.sample_rate() << " Hz\n";

    // Read all audio data
    std::vector<int16_t> audio_samples;
    while (true) {
        auto chunk = fileCap.read_chunk();
        if (chunk.empty()) break;
        audio_samples.insert(audio_samples.end(), chunk.begin(), chunk.end());
    }

    // Initialize frame analyzer for diarization
    diar::ContinuousFrameAnalyzer::Config frame_config;
    frame_config.embedding_mode = diar::EmbeddingMode::NeuralONNX;
    frame_config.onnx_model_path = "models/campplus_voxceleb.onnx";  // CAMPlus (better than WeSpeaker)
    frame_config.verbose = false;
    
    diar::ContinuousFrameAnalyzer frame_analyzer(fileCap.sample_rate(), frame_config);
    
    // Process audio for diarization
    std::cout << "✓ Extracting speaker frames...\n";
    frame_analyzer.add_audio(audio_samples.data(), audio_samples.size());
    
    // Cluster frames to assign speaker IDs
    std::cout << "✓ Clustering speakers (threshold=0.20 for CAMPlus - AGGRESSIVE)...\n";
    frame_analyzer.cluster_frames(2, 0.20f);  // Try lower threshold to force 2 speakers
    
    std::cout << "  Frame count: " << frame_analyzer.frame_count() << "\n";
    
    // Transcribe with word-level timestamps
    std::cout << "✓ Transcribing with word timestamps...\n";
    auto segments = whisper.transcribe_chunk_with_words(
        audio_samples.data(), 
        audio_samples.size()
    );

    std::cout << "  Segment count: " << segments.size() << "\n";
    
    // Get all frames for mapping
    const auto& all_frames = frame_analyzer.get_all_frames();
    
    // Print with word-level speaker assignment
    print_with_speaker_changes(segments, all_frames);
    
    // Statistics
    int speaker_counts[2] = {0, 0};
    int total_words = 0;
    
    for (const auto& seg : segments) {
        auto word_speakers = map_words_to_speakers(seg.words, all_frames);
        for (int spk : word_speakers) {
            if (spk >= 0 && spk < 2) {
                speaker_counts[spk]++;
                total_words++;
            }
        }
    }
    
    std::cout << "\nSTATISTICS:\n";
    std::cout << "  Total words: " << total_words << "\n";
    std::cout << "  Speaker 0: " << speaker_counts[0] << " words (" 
              << (100.0f * speaker_counts[0] / total_words) << "%)\n";
    std::cout << "  Speaker 1: " << speaker_counts[1] << " words (" 
              << (100.0f * speaker_counts[1] / total_words) << "%)\n";
    
    std::cout << "\n✅ Test complete!\n";
    return 0;
}
