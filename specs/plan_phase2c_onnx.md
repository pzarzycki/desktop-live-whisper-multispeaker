# Phase 2c: Neural Speaker Embeddings via ONNX Runtime

**Date**: 2025-10-07  
**Status**: Planning → Implementation  
**Goal**: Replace hand-crafted 53-dim features with pretrained neural speaker embeddings

---

## Problem Statement

Phase 2b achieved balanced clustering (47/30 frames) but **accuracy is poor (~44%)**:
- Hand-crafted MFCC+Delta+Pitch+Formants features are not sufficiently discriminative
- Ground truth shows we're misclassifying many segments
- Neural embeddings trained on millions of speaker pairs will perform much better

---

## Solution: ONNX Runtime Integration

### Why ONNX?
- **Cross-platform**: Works on Windows/Linux/macOS
- **C++ API**: Native integration (no Python dependency)
- **Pretrained models**: Many speaker recognition models available
- **Performance**: Optimized inference, CPU/GPU support

### Model Selection

**Option 1: SpeechBrain ECAPA-TDNN** (Recommended)
- 192-dim speaker embeddings
- State-of-the-art accuracy on VoxCeleb
- Available in ONNX format
- Model: `speechbrain/spkrec-ecapa-voxceleb`

**Option 2: PyAnnote Speaker Embedding**
- 512-dim embeddings
- Widely used in research
- Requires ONNX export from PyTorch

**Option 3: Western Speaker Recognition**
- Lightweight ResNet-based model
- 256-dim embeddings
- Good balance of speed/accuracy

**Decision**: Start with **ECAPA-TDNN** (proven, widely used, ONNX available)

---

## Implementation Plan

### Step 1: ONNX Runtime Setup

**Dependencies**:
```cmake
# vcpkg install onnxruntime
find_package(onnxruntime REQUIRED)
target_link_libraries(app_transcribe_file PRIVATE onnxruntime::onnxruntime)
```

**Files to create**:
- `src/diar/onnx_embedder.hpp` - ONNX wrapper interface
- `src/diar/onnx_embedder.cpp` - Implementation

### Step 2: Model Download & Preparation

**Get ECAPA-TDNN ONNX model**:
```python
# Python script to export (run once)
from speechbrain.pretrained import EncoderClassifier
import torch

classifier = EncoderClassifier.from_hparams(
    source="speechbrain/spkrec-ecapa-voxceleb",
    savedir="models/ecapa-tdnn"
)

# Export to ONNX
dummy_input = torch.randn(1, 16000)  # 1 second @ 16kHz
torch.onnx.export(
    classifier.encode_batch,
    dummy_input,
    "models/speaker_embedding.onnx",
    input_names=['audio'],
    output_names=['embedding'],
    dynamic_axes={'audio': {0: 'batch', 1: 'time'}}
)
```

**Alternative**: Download preconverted model from HuggingFace/ONNX Model Zoo

### Step 3: C++ ONNX Wrapper

```cpp
// src/diar/onnx_embedder.hpp
#pragma once
#include <vector>
#include <string>
#include <memory>
#include <onnxruntime_cxx_api.h>

namespace diar {

class OnnxSpeakerEmbedder {
public:
    struct Config {
        std::string model_path = "models/speaker_embedding.onnx";
        int sample_rate = 16000;
        int target_length = 16000;  // 1 second
        bool verbose = false;
    };

    explicit OnnxSpeakerEmbedder(const Config& config);
    ~OnnxSpeakerEmbedder();

    // Extract embedding from audio samples
    std::vector<float> compute_embedding(const int16_t* pcm16, size_t samples);
    
    // Get embedding dimension (192 for ECAPA-TDNN)
    int embedding_dim() const { return m_embedding_dim; }

private:
    Config m_config;
    std::unique_ptr<Ort::Env> m_env;
    std::unique_ptr<Ort::Session> m_session;
    std::unique_ptr<Ort::SessionOptions> m_session_options;
    Ort::AllocatorWithDefaultOptions m_allocator;
    
    std::vector<const char*> m_input_names;
    std::vector<const char*> m_output_names;
    int m_embedding_dim = 192;
    
    // Preprocess audio: resample, normalize, pad/trim
    std::vector<float> preprocess_audio(const int16_t* pcm16, size_t samples);
};

} // namespace diar
```

### Step 4: Integration with SpeakerClusterer

**Modify `speaker_cluster.hpp`**:
```cpp
class SpeakerClusterer {
public:
    enum class EmbeddingMode {
        HandCrafted,  // 53-dim MFCC+Delta+Pitch+Formants (Phase 2b)
        NeuralONNX    // 192-dim ECAPA-TDNN (Phase 2c)
    };
    
    struct Config {
        // ... existing fields ...
        EmbeddingMode embedding_mode = EmbeddingMode::NeuralONNX;
        std::string onnx_model_path = "models/speaker_embedding.onnx";
    };
    
private:
    std::unique_ptr<OnnxSpeakerEmbedder> m_onnx_embedder;
};
```

**Update `compute_speaker_embedding()`**:
```cpp
std::vector<float> SpeakerClusterer::compute_speaker_embedding(
    const int16_t* pcm16, size_t samples, int sample_rate
) {
    if (m_config.embedding_mode == EmbeddingMode::NeuralONNX) {
        if (!m_onnx_embedder) {
            OnnxSpeakerEmbedder::Config onnx_cfg;
            onnx_cfg.model_path = m_config.onnx_model_path;
            onnx_cfg.sample_rate = sample_rate;
            onnx_cfg.verbose = m_config.verbose;
            m_onnx_embedder = std::make_unique<OnnxSpeakerEmbedder>(onnx_cfg);
        }
        return m_onnx_embedder->compute_embedding(pcm16, samples);
    } else {
        // Fallback to hand-crafted features
        return compute_speaker_embedding_v2(pcm16, samples, sample_rate);
    }
}
```

### Step 5: Testing & Validation

**Test 1: Embedding extraction**
- Load audio, extract embeddings, verify shape (192-dim)
- Check values are reasonable (normalized, not NaN/Inf)

**Test 2: Same-speaker similarity**
- Extract embeddings from two segments of same speaker
- Cosine similarity should be high (>0.8)

**Test 3: Different-speaker similarity**
- Extract embeddings from different speakers
- Cosine similarity should be low (<0.5)

**Test 4: Full pipeline**
- Run on Sean Carroll podcast
- Compare clustering accuracy vs hand-crafted features
- Expect significant improvement (>80% accuracy)

---

## Expected Improvements

### Before (Phase 2b - Hand-crafted):
- Features: 53-dim MFCC+Delta+Pitch+Formants
- Clustering: Balanced (47/30) but poor accuracy (~44%)
- Misclassifications: Many segments assigned to wrong speaker

### After (Phase 2c - Neural ONNX):
- Features: 192-dim ECAPA-TDNN embeddings
- Clustering: Expected high accuracy (>80%)
- Better speaker discrimination (trained on millions of examples)

---

## Implementation Steps

1. ✅ **Planning** - This document
2. ⬜ **Setup ONNX Runtime** - Add vcpkg dependency, CMake config
3. ⬜ **Download/Export Model** - Get ECAPA-TDNN ONNX model
4. ⬜ **Implement OnnxSpeakerEmbedder** - C++ wrapper for ONNX inference
5. ⬜ **Integrate with SpeakerClusterer** - Add mode switching
6. ⬜ **Test & Validate** - Verify embeddings and clustering accuracy
7. ⬜ **Compare Results** - Hand-crafted vs Neural (quantitative comparison)
8. ⬜ **Documentation** - Update README, add model download instructions

---

## Fallback Plan

If ONNX integration proves difficult:
- **Plan B**: Use Python subprocess to extract embeddings (SpeechBrain)
- **Plan C**: Stick with hand-crafted features but improve (more formants, jitter/shimmer)
- **Plan D**: Try simpler neural model (lighter ResNet)

---

## Success Criteria

- ✅ ONNX Runtime integrated and working
- ✅ ECAPA-TDNN model loaded successfully
- ✅ Embeddings extracted (192-dim, normalized)
- ✅ Clustering accuracy >80% on test audio
- ✅ Better than hand-crafted features (quantitative comparison)
- ✅ Performance acceptable (<2x slowdown vs hand-crafted)

---

## References

- [ONNX Runtime C++ API](https://onnxruntime.ai/docs/api/c/)
- [SpeechBrain ECAPA-TDNN](https://huggingface.co/speechbrain/spkrec-ecapa-voxceleb)
- [Speaker Recognition Survey](https://arxiv.org/abs/2102.07895)
