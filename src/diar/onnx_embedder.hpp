#pragma once

#include <vector>
#include <string>
#include <memory>

// Forward declarations to avoid including onnxruntime headers here
namespace Ort {
    struct Env;
    struct Session;
    struct SessionOptions;
    struct AllocatorWithDefaultOptions;
    struct MemoryInfo;
}

namespace diar {

/**
 * ONNX-based neural speaker embedding extractor.
 * Uses pretrained models like ECAPA-TDNN for high-quality speaker embeddings.
 */
class OnnxSpeakerEmbedder {
public:
    struct Config {
        std::string model_path = "models/speaker_embedding.onnx";
        int sample_rate = 16000;
        int target_length_samples = 16000;  // 1 second of audio
        bool normalize_output = true;       // L2-normalize embeddings
        bool verbose = false;
    };

    explicit OnnxSpeakerEmbedder(const Config& config);
    ~OnnxSpeakerEmbedder();

    // Disable copy/move (ONNX session is non-copyable)
    OnnxSpeakerEmbedder(const OnnxSpeakerEmbedder&) = delete;
    OnnxSpeakerEmbedder& operator=(const OnnxSpeakerEmbedder&) = delete;

    /**
     * Extract speaker embedding from audio samples.
     * @param pcm16 Raw 16-bit PCM audio
     * @param samples Number of samples (will be padded/trimmed to target_length)
     * @return Normalized embedding vector (typically 192-dim for ECAPA-TDNN)
     */
    std::vector<float> compute_embedding(const int16_t* pcm16, size_t samples);
    
    /**
     * Get the dimensionality of output embeddings.
     */
    int embedding_dim() const { return m_embedding_dim; }

private:
    Config m_config;
    
    // ONNX Runtime objects (using unique_ptr to hide implementation details)
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
    std::unique_ptr<Ort::SessionOptions> m_session_options;
    std::unique_ptr<Ort::AllocatorWithDefaultOptions> m_allocator;
    std::unique_ptr<Ort::MemoryInfo> m_memory_info;
    
    std::vector<const char*> m_input_names;
    std::vector<const char*> m_output_names;
    int m_embedding_dim = 192;  // Default for ECAPA-TDNN
    
    /**
     * Preprocess audio: convert int16 to float32, normalize, pad/trim.
     */
    std::vector<float> preprocess_audio(const int16_t* pcm16, size_t samples);
    
    /**
     * L2-normalize embedding vector.
     */
    void normalize_embedding(std::vector<float>& emb);
};

} // namespace diar
