# Speaker Embedding Models - ONNX Format

**Date**: 2025-10-07  
**Goal**: Find pretrained speaker embedding models already in ONNX format

---

## Available Models (ONNX Format)

### 1. **SpeechBrain ECAPA-TDNN** (Recommended for accuracy)
- **Source**: https://huggingface.co/speechbrain/spkrec-ecapa-voxceleb
- **ONNX Availability**: Requires manual export (Python script ready)
- **Embedding Dim**: 192
- **Trained on**: VoxCeleb (7,000+ speakers)
- **Performance**: EER ~0.69% on VoxCeleb1
- **Size**: ~70MB
- **Note**: Not directly available in ONNX - need to export

### 2. **Pyannote Speaker Embedding** 
- **Source**: https://huggingface.co/pyannote/embedding
- **ONNX Availability**: Community exports available
- **Embedding Dim**: 512
- **Performance**: State-of-the-art for diarization
- **Size**: ~15MB
- **Note**: May need conversion

### 3. **Titanet (NVIDIA NeMo)** ✅ **BEST CHOICE - START HERE**
- **Source**: https://catalog.ngc.nvidia.com/orgs/nvidia/teams/nemo/models/titanet_large
- **ONNX Availability**: ✅ **Direct ONNX export available**
- **Embedding Dim**: 192
- **Trained on**: VoxCeleb1+2
- **Performance**: EER ~0.66% (excellent!)
- **Size**: ~32MB
- **Speed**: Very fast (optimized for real-time)
- **License**: NVIDIA NeMo (Apache 2.0)

### 4. **WeSpeaker ResNet34** ✅ **LIGHTWEIGHT ALTERNATIVE**
- **Source**: https://github.com/wenet-e2e/wespeaker
- **ONNX Availability**: ✅ **Pre-converted ONNX models available**
- **Models**:
  - ResNet34 (small): 7MB, 256-dim embeddings
  - ResNet152 (large): 25MB, 256-dim embeddings
- **Trained on**: VoxCeleb, CN-Celeb
- **Performance**: EER ~1.5-2.0% (good, not best)
- **Speed**: Extremely fast
- **License**: Apache 2.0

### 5. **Resemblyzer (Simple, Educational)**
- **Source**: https://github.com/resemble-ai/Resemblyzer
- **ONNX Availability**: Community ports exist
- **Embedding Dim**: 256
- **Performance**: Moderate (~5% EER)
- **Size**: ~5MB
- **Note**: Good for testing, not production

---

## Decision Matrix

| Model | ONNX Ready? | Size | Accuracy | Speed | Recommendation |
|-------|-------------|------|----------|-------|----------------|
| **WeSpeaker ResNet34** | ✅ Yes | 7MB | Good (2% EER) | ⚡ Fast | **START HERE** |
| Titanet Large | ✅ Yes | 32MB | Best (0.66%) | Fast | Try next |
| ECAPA-TDNN | ❌ Need export | 70MB | Best (0.69%) | Medium | If others fail |
| Pyannote | ❌ Need export | 15MB | Excellent | Medium | Advanced |
| Resemblyzer | ⚠️ Community | 5MB | OK (5%) | Fast | Testing only |

---

## **SELECTED: WeSpeaker ResNet34**

**Why?**
1. ✅ **Direct ONNX download** - no Python/export needed
2. ✅ **Small size** (7MB) - fast downloads, fast inference
3. ✅ **Good performance** (2% EER = 98% accuracy)
4. ✅ **Apache 2.0 license** - commercial friendly
5. ✅ **Well-documented** - easy integration
6. ✅ **Proven in production** - used by WeNet ASR

**Download URLs**:
- Model: https://wespeaker-1256283475.cos.ap-shanghai.myqcloud.com/models/voxceleb/voxceleb_resnet34.onnx
- Alternative: https://github.com/wenet-e2e/wespeaker/releases

**Input Format**:
- Audio: 16kHz mono float32
- Length: Variable (handles padding internally)
- Output: 256-dim L2-normalized embedding

**Expected Results on Sean Carroll Podcast**:
- Current (hand-crafted): ~44% accuracy
- Expected (WeSpeaker): **>85% accuracy**
- Improvement: ~2x better

---

## Backup Plan: Titanet Large

If WeSpeaker doesn't meet needs:
- Download: https://catalog.ngc.nvidia.com/orgs/nvidia/teams/nemo/models/titanet_large
- Slightly larger (32MB) but state-of-the-art accuracy (0.66% EER)
- May require NVIDIA NGC account (free)

---

## Implementation Steps

1. ✅ Download WeSpeaker ResNet34 ONNX model
2. ✅ Place in `models/speaker_embedding.onnx`
3. ✅ Update `OnnxSpeakerEmbedder` config if needed (target_length, etc.)
4. ✅ Build and test on Sean Carroll podcast
5. ✅ Compare accuracy: hand-crafted vs neural
6. ⬜ If <80% accuracy, try Titanet Large
7. ⬜ Document final choice in architecture.md
