#include "asr/whisper_backend.hpp"
#include "audio/file_capture.hpp"
#include <iostream>
#include <iomanip>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <audio.wav>\n";
        return 1;
    }

    // Load Whisper model
    asr::WhisperBackend whisper;
    if (!whisper.load_model("tiny.en")) {
        std::cerr << "Failed to load Whisper model\n";
        return 1;
    }
    std::cout << "✓ Whisper model loaded\n\n";

    // Load audio
    const std::string audio_path = argv[1];
    audio::FileCapture fileCap;
    if (!fileCap.start_from_wav(audio_path)) {
        std::cerr << "Failed to load audio file: " << audio_path << "\n";
        return 1;
    }
    
    std::cout << "✓ Audio loaded: " << fileCap.duration_seconds() << " seconds @ " 
              << fileCap.sample_rate() << " Hz\n";
    std::cout << "  Channels: " << fileCap.channels() << "\n";
    std::cout << "  Bits per sample: " << fileCap.bits_per_sample() << "\n\n";

    // Read all audio data
    std::vector<int16_t> audio_samples;
    while (true) {
        auto chunk = fileCap.read_chunk();
        if (chunk.empty()) break;
        audio_samples.insert(audio_samples.end(), chunk.begin(), chunk.end());
    }

    // Limit to first 10 seconds for testing
    const size_t max_samples = std::min<size_t>(
        audio_samples.size(), 
        fileCap.sample_rate() * 10
    );
    
    // Transcribe with word-level timestamps
    std::cout << "Transcribing with word-level timestamps...\n\n";
    auto segments = whisper.transcribe_chunk_with_words(
        audio_samples.data(), 
        max_samples
    );

    std::cout << "============================================================\n";
    std::cout << "WORD-LEVEL TIMESTAMPS TEST RESULTS\n";
    std::cout << "============================================================\n\n";

    for (size_t i = 0; i < segments.size(); ++i) {
        const auto& seg = segments[i];
        
        std::cout << "Segment " << (i+1) << " [" 
                  << std::fixed << std::setprecision(2)
                  << (seg.t0_ms / 1000.0) << "s - " 
                  << (seg.t1_ms / 1000.0) << "s]:\n";
        std::cout << "  Full text: " << seg.text << "\n";
        std::cout << "  Word count: " << seg.words.size() << "\n";
        
        if (!seg.words.empty()) {
            std::cout << "  Words:\n";
            for (const auto& word : seg.words) {
                std::cout << "    [" 
                          << std::fixed << std::setprecision(3)
                          << (word.t0_ms / 1000.0) << "s - " 
                          << (word.t1_ms / 1000.0) << "s] " 
                          << word.word 
                          << " (p=" << std::setprecision(2) << word.probability << ")\n";
            }
        } else {
            std::cout << "    (no word timestamps extracted)\n";
        }
        std::cout << "\n";
    }

    std::cout << "============================================================\n";
    std::cout << "✓ Test complete! Found " << segments.size() << " segments\n";
    
    // Statistics
    size_t total_words = 0;
    for (const auto& seg : segments) {
        total_words += seg.words.size();
    }
    std::cout << "  Total words extracted: " << total_words << "\n";
    
    if (total_words > 0) {
        std::cout << "\n✅ SUCCESS: Word-level timestamps working!\n";
    } else {
        std::cout << "\n⚠️ WARNING: No words extracted. Check token parsing logic.\n";
    }

    return 0;
}
