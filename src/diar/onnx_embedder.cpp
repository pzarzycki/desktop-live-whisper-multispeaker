#include "diar/onnx_embedder.hpp"
#include "diar/mel_features.hpp"
#include <onnxruntime_cxx_api.h>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <cstdio>

#ifdef _WIN32
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#include <windows.h>
#endif

namespace diar {

OnnxSpeakerEmbedder::OnnxSpeakerEmbedder(const Config& config)
    : m_config(config)
{
    if (m_config.verbose) {
        fprintf(stderr, "[OnnxEmbedder] Initializing with model: %s\n", m_config.model_path.c_str());
    }

    try {
        // Create ONNX Runtime environment
        m_env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "SpeakerEmbedding");
        
        // Create session options
        m_session_options = std::make_unique<Ort::SessionOptions>();
        m_session_options->SetIntraOpNumThreads(4);
        m_session_options->SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        
        // Load the ONNX model
#ifdef _WIN32
        // Windows: convert UTF-8 to wide string
        std::wstring wide_path;
        wide_path.resize(m_config.model_path.size() + 1);
        int len = MultiByteToWideChar(CP_UTF8, 0, m_config.model_path.c_str(), 
                                       static_cast<int>(m_config.model_path.size()),
                                       &wide_path[0], static_cast<int>(wide_path.size()));
        wide_path.resize(len);
        m_session = std::make_unique<Ort::Session>(*m_env, wide_path.c_str(), *m_session_options);
#else
        m_session = std::make_unique<Ort::Session>(*m_env, m_config.model_path.c_str(), *m_session_options);
#endif

        // Create allocator
        m_allocator = std::make_unique<Ort::AllocatorWithDefaultOptions>();
        
        // Get input/output names
        size_t num_inputs = m_session->GetInputCount();
        size_t num_outputs = m_session->GetOutputCount();
        
        if (m_config.verbose) {
            fprintf(stderr, "[OnnxEmbedder] Model loaded: %zu inputs, %zu outputs\n", 
                    num_inputs, num_outputs);
        }
        
        // Get input name
        if (num_inputs > 0) {
            Ort::AllocatedStringPtr input_name_ptr = m_session->GetInputNameAllocated(0, *m_allocator);
            std::string input_name(input_name_ptr.get());
            m_input_name_strings.push_back(input_name);
            m_input_names.push_back(m_input_name_strings.back().c_str());
            
            if (m_config.verbose) {
                fprintf(stderr, "[OnnxEmbedder] Input name: %s\n", input_name.c_str());
            }
        }
        
        // Get output name and dimension
        if (num_outputs > 0) {
            Ort::AllocatedStringPtr output_name_ptr = m_session->GetOutputNameAllocated(0, *m_allocator);
            std::string output_name(output_name_ptr.get());
            m_output_name_strings.push_back(output_name);
            m_output_names.push_back(m_output_name_strings.back().c_str());
            
            // Get output shape to determine embedding dimension
            Ort::TypeInfo type_info = m_session->GetOutputTypeInfo(0);
            auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
            auto shape = tensor_info.GetShape();
            
            if (shape.size() >= 2) {
                m_embedding_dim = static_cast<int>(shape[1]);  // (batch, embedding_dim)
            }
            
            if (m_config.verbose) {
                fprintf(stderr, "[OnnxEmbedder] Output name: %s, embedding_dim: %d\n", 
                        output_name.c_str(), m_embedding_dim);
            }
        }
        
        // Create memory info for input tensors
        m_memory_info = std::make_unique<Ort::MemoryInfo>(
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault)
        );
        
        // Initialize mel feature extractor for Fbank-based models
        MelFeatureExtractor::Config mel_config;
        mel_config.sample_rate = m_config.sample_rate;
        mel_config.n_fft = 400;         // 25ms at 16kHz
        mel_config.hop_length = 160;    // 10ms at 16kHz
        mel_config.n_mels = 80;         // WeSpeaker expects 80-dim Fbank
        mel_config.fmax = m_config.sample_rate / 2.0f;  // Nyquist frequency
        m_mel_extractor = std::make_unique<MelFeatureExtractor>(mel_config);
        
        if (m_config.verbose) {
            fprintf(stderr, "[OnnxEmbedder] Initialization complete (with Fbank extraction)\n");
        }
        
    } catch (const Ort::Exception& e) {
        fprintf(stderr, "[OnnxEmbedder] ONNX Runtime error: %s\n", e.what());
        throw std::runtime_error(std::string("Failed to initialize ONNX embedder: ") + e.what());
    }
}

OnnxSpeakerEmbedder::~OnnxSpeakerEmbedder() = default;

std::vector<float> OnnxSpeakerEmbedder::preprocess_audio(const int16_t* pcm16, size_t samples) {
    // Convert int16 to float32 and normalize to [-1, 1]
    std::vector<float> audio_float(m_config.target_length_samples, 0.0f);
    
    size_t copy_samples = std::min(samples, static_cast<size_t>(m_config.target_length_samples));
    for (size_t i = 0; i < copy_samples; ++i) {
        audio_float[i] = pcm16[i] / 32768.0f;
    }
    
    // If audio is shorter than target, it's already zero-padded
    // If audio is longer, it's truncated (first N samples used)
    
    return audio_float;
}

void OnnxSpeakerEmbedder::normalize_embedding(std::vector<float>& emb) {
    double norm = 0.0;
    for (float val : emb) {
        norm += val * val;
    }
    norm = std::sqrt(norm);
    
    if (norm > 1e-8) {
        for (float& val : emb) {
            val /= static_cast<float>(norm);
        }
    }
}

std::vector<float> OnnxSpeakerEmbedder::compute_embedding(const int16_t* pcm16, size_t samples) {
    if (!pcm16 || samples == 0) {
        return std::vector<float>(m_embedding_dim, 0.0f);
    }
    
    try {
        // Preprocess audio: convert int16 to float32
        std::vector<float> audio_float = preprocess_audio(pcm16, samples);
        
        // Extract mel filterbank features (80-dim Fbank)
        std::vector<float> mel_features = m_mel_extractor->extract_features(
            audio_float.data(), 
            static_cast<int>(audio_float.size())
        );
        
        // Get number of time frames
        int n_frames = m_mel_extractor->get_num_frames(static_cast<int>(audio_float.size()));
        
        if (n_frames <= 0) {
            fprintf(stderr, "[OnnxEmbedder] Warning: No frames extracted from audio\n");
            return std::vector<float>(m_embedding_dim, 0.0f);
        }
        
        // Create input tensor: [batch=1, time_frames, n_mels=80]
        std::vector<int64_t> input_shape = {1, static_cast<int64_t>(n_frames), 80};
        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            *m_memory_info,
            mel_features.data(),
            mel_features.size(),
            input_shape.data(),
            input_shape.size()
        );
        
        // Run inference
        auto output_tensors = m_session->Run(
            Ort::RunOptions{nullptr},
            m_input_names.data(),
            &input_tensor,
            1,
            m_output_names.data(),
            1
        );
        
        // Extract embedding from output tensor
        float* output_data = output_tensors[0].GetTensorMutableData<float>();
        auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();
        
        size_t output_dim = output_shape.size() >= 2 ? output_shape[1] : output_shape[0];
        std::vector<float> embedding(output_data, output_data + output_dim);
        
        // L2-normalize if requested
        if (m_config.normalize_output) {
            normalize_embedding(embedding);
        }
        
        return embedding;
        
    } catch (const Ort::Exception& e) {
        fprintf(stderr, "[OnnxEmbedder] Inference error: %s\n", e.what());
        return std::vector<float>(m_embedding_dim, 0.0f);
    }
}

} // namespace diar
