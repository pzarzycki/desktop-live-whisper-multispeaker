/**
 * Diagnostic tool to analyze speaker embedding quality
 * 
 * Extracts all frame embeddings and dumps them for analysis:
 * - Saves embeddings to numpy-compatible format
 * - Computes pairwise cosine similarities
 * - Shows statistics (mean, std, min, max)
 * - Identifies clusters
 * 
 * Usage: test_embedding_quality <audio.wav> [output.txt]
 */

#include <iostream>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <iomanip>
#include "audio/file_capture.hpp"
#include "diar/speaker_cluster.hpp"

// Compute cosine similarity between two embeddings
float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return 0.0f;
    
    float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    
    if (norm_a == 0.0f || norm_b == 0.0f) return 0.0f;
    return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

// Compute all pairwise similarities
std::vector<float> compute_all_similarities(const std::deque<diar::ContinuousFrameAnalyzer::Frame>& frames) {
    std::vector<float> similarities;
    
    for (size_t i = 0; i < frames.size(); ++i) {
        for (size_t j = i + 1; j < frames.size(); ++j) {
            float sim = cosine_similarity(frames[i].embedding, frames[j].embedding);
            similarities.push_back(sim);
        }
    }
    
    return similarities;
}

// Save embeddings in numpy-compatible text format
void save_embeddings(const std::deque<diar::ContinuousFrameAnalyzer::Frame>& frames, 
                     const std::string& filename) {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Failed to open " << filename << " for writing\n";
        return;
    }
    
    // Header: dimensions
    out << "# Shape: (" << frames.size() << ", " << frames[0].embedding.size() << ")\n";
    out << "# Format: time_ms, embedding[0], embedding[1], ..., embedding[n-1]\n";
    
    // Data
    for (const auto& frame : frames) {
        out << std::fixed << std::setprecision(1) << frame.t_start_ms << ",";
        for (size_t i = 0; i < frame.embedding.size(); ++i) {
            if (i > 0) out << ",";
            out << std::fixed << std::setprecision(6) << frame.embedding[i];
        }
        out << "\n";
    }
    
    std::cout << "✓ Saved " << frames.size() << " embeddings to " << filename << "\n";
}

// Statistics helper
struct Stats {
    float mean = 0.0f;
    float std_dev = 0.0f;
    float min_val = 0.0f;
    float max_val = 0.0f;
    
    Stats(const std::vector<float>& values) {
        if (values.empty()) return;
        
        // Min/max
        min_val = *std::min_element(values.begin(), values.end());
        max_val = *std::max_element(values.begin(), values.end());
        
        // Mean
        float sum = 0.0f;
        for (float v : values) sum += v;
        mean = sum / values.size();
        
        // Std dev
        float var = 0.0f;
        for (float v : values) {
            float diff = v - mean;
            var += diff * diff;
        }
        std_dev = std::sqrt(var / values.size());
    }
    
    void print(const std::string& label) const {
        std::cout << label << ":\n";
        std::cout << "  Mean:   " << std::fixed << std::setprecision(4) << mean << "\n";
        std::cout << "  StdDev: " << std::fixed << std::setprecision(4) << std_dev << "\n";
        std::cout << "  Range:  [" << std::fixed << std::setprecision(4) << min_val 
                  << ", " << max_val << "]\n";
    }
};

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <audio.wav> [output.txt]\n";
        std::cerr << "Note: Input should be 16kHz mono (or will be resampled)\n";
        return 1;
    }
    
    const std::string audio_path = argv[1];
    const std::string output_path = (argc >= 3) ? argv[2] : "embeddings.txt";
    
    // Load audio
    audio::FileCapture fileCap;
    if (!fileCap.start_from_wav(audio_path)) {
        std::cerr << "Failed to load audio file: " << audio_path << "\n";
        return 1;
    }
    
    std::cout << "✓ Audio loaded: " << fileCap.duration_seconds() << " seconds @ " 
              << fileCap.sample_rate() << " Hz\n";
    
    // Read all audio
    std::vector<int16_t> audio_samples;
    while (true) {
        auto chunk = fileCap.read_chunk();
        if (chunk.empty()) break;
        audio_samples.insert(audio_samples.end(), chunk.begin(), chunk.end());
    }
    
    // Initialize frame analyzer with CAMPlus
    diar::ContinuousFrameAnalyzer::Config frame_config;
    frame_config.embedding_mode = diar::EmbeddingMode::NeuralONNX;
    frame_config.onnx_model_path = "models/campplus_voxceleb.onnx";
    frame_config.hop_ms = 250;       // 4 frames per second
    frame_config.window_ms = 1000;   // 1s window
    frame_config.verbose = false;
    
    diar::ContinuousFrameAnalyzer frame_analyzer(fileCap.sample_rate(), frame_config);
    
    std::cout << "✓ Extracting embeddings (every 250ms)...\n";
    frame_analyzer.add_audio(audio_samples.data(), audio_samples.size());
    
    const auto& frames = frame_analyzer.get_all_frames();
    std::cout << "✓ Extracted " << frames.size() << " frames\n";
    std::cout << "  Embedding dimension: " << frames[0].embedding.size() << "\n";
    std::cout << "  Time span: " << frames.front().t_start_ms << "ms - " 
              << frames.back().t_end_ms << "ms\n\n";
    
    // Save embeddings
    save_embeddings(frames, output_path);
    
    // Compute all pairwise similarities
    std::cout << "\n============================================================\n";
    std::cout << "SIMILARITY ANALYSIS\n";
    std::cout << "============================================================\n\n";
    
    std::cout << "Computing " << (frames.size() * (frames.size() - 1) / 2) 
              << " pairwise similarities...\n";
    auto similarities = compute_all_similarities(frames);
    
    Stats sim_stats(similarities);
    sim_stats.print("All pairwise similarities");
    
    // Find most similar and most dissimilar pairs
    auto min_it = std::min_element(similarities.begin(), similarities.end());
    auto max_it = std::max_element(similarities.begin(), similarities.end());
    
    std::cout << "\n" << std::string(60, '-') << "\n";
    std::cout << "Most similar:    " << std::fixed << std::setprecision(6) << *max_it 
              << " (should be ~1.0 for same speaker)\n";
    std::cout << "Most dissimilar: " << std::fixed << std::setprecision(6) << *min_it 
              << " (should be <0.8 for different speakers)\n";
    
    // Analyze distribution
    std::cout << "\n" << std::string(60, '-') << "\n";
    std::cout << "DISTRIBUTION ANALYSIS:\n\n";
    
    // Count similarities in ranges
    int very_high = 0, high = 0, medium = 0, low = 0;
    for (float sim : similarities) {
        if (sim > 0.95f) very_high++;
        else if (sim > 0.85f) high++;
        else if (sim > 0.70f) medium++;
        else low++;
    }
    
    std::cout << "Similarity ranges:\n";
    std::cout << "  >0.95 (very similar): " << very_high << " (" 
              << (100.0f * very_high / similarities.size()) << "%)\n";
    std::cout << "  0.85-0.95 (similar):  " << high << " (" 
              << (100.0f * high / similarities.size()) << "%)\n";
    std::cout << "  0.70-0.85 (medium):   " << medium << " (" 
              << (100.0f * medium / similarities.size()) << "%)\n";
    std::cout << "  <0.70 (dissimilar):   " << low << " (" 
              << (100.0f * low / similarities.size()) << "%)\n\n";
    
    // Expected for 2 speakers
    std::cout << std::string(60, '-') << "\n";
    std::cout << "EXPECTED FOR 2 DISTINCT SPEAKERS:\n";
    std::cout << "  - Intra-speaker similarity: >0.90 (same person)\n";
    std::cout << "  - Inter-speaker similarity: <0.80 (different people)\n";
    std::cout << "  - Clear bimodal distribution\n\n";
    
    // Check if bimodal
    int above_threshold = 0;
    int below_threshold = 0;
    float threshold = 0.85f;
    
    for (float sim : similarities) {
        if (sim > threshold) above_threshold++;
        else below_threshold++;
    }
    
    std::cout << "At threshold " << threshold << ":\n";
    std::cout << "  Above: " << above_threshold << " (" 
              << (100.0f * above_threshold / similarities.size()) << "%)\n";
    std::cout << "  Below: " << below_threshold << " (" 
              << (100.0f * below_threshold / similarities.size()) << "%)\n\n";
    
    if (below_threshold < similarities.size() * 0.1f) {
        std::cout << "⚠️  WARNING: Nearly all similarities are high!\n";
        std::cout << "    This suggests embeddings are NOT distinguishing speakers.\n";
        std::cout << "    Possible issues:\n";
        std::cout << "      - Model may not be loaded correctly\n";
        std::cout << "      - Audio may contain only one speaker\n";
        std::cout << "      - Embeddings may not be normalized\n\n";
    } else if (below_threshold > similarities.size() * 0.3f) {
        std::cout << "✓ Good separation detected!\n";
        std::cout << "  Likely 2+ distinct speakers present.\n\n";
    }
    
    // Show first few frame-to-frame similarities
    std::cout << std::string(60, '-') << "\n";
    std::cout << "SEQUENTIAL FRAME SIMILARITIES (first 10):\n";
    std::cout << "(Adjacent frames from same speaker should be >0.95)\n\n";
    
    for (size_t i = 0; i < std::min(size_t(10), frames.size() - 1); ++i) {
        float sim = cosine_similarity(frames[i].embedding, frames[i + 1].embedding);
        std::cout << "  Frame " << i << " → " << (i + 1) << " @ " 
                  << frames[i].t_start_ms << "ms: " 
                  << std::fixed << std::setprecision(4) << sim;
        
        if (sim < 0.85f) {
            std::cout << " ← Speaker change?";
        }
        std::cout << "\n";
    }
    
    std::cout << "\n✅ Analysis complete! Check " << output_path << " for embeddings.\n";
    std::cout << "\nTo visualize in Python:\n";
    std::cout << "  import numpy as np\n";
    std::cout << "  import matplotlib.pyplot as plt\n";
    std::cout << "  from sklearn.decomposition import PCA\n";
    std::cout << "  data = np.loadtxt('" << output_path << "', delimiter=',', skiprows=2)\n";
    std::cout << "  embeddings = data[:, 1:]  # Skip time column\n";
    std::cout << "  pca = PCA(n_components=2)\n";
    std::cout << "  reduced = pca.fit_transform(embeddings)\n";
    std::cout << "  plt.scatter(reduced[:, 0], reduced[:, 1])\n";
    std::cout << "  plt.xlabel('PC1'); plt.ylabel('PC2')\n";
    std::cout << "  plt.title('Speaker Embeddings (PCA projection)')\n";
    std::cout << "  plt.show()\n";
    
    return 0;
}
