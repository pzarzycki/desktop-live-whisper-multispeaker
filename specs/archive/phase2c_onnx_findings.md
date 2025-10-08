# Phase 2c: ONNX Neural Embeddings - Current Status

## Date: 2025-10-07

## Summary

Successfully integrated ONNX Runtime and WeSpeaker ResNet34 model, but discovered the model requires **80-dimensional Fbank features** as input, not raw audio waveform. This requires adding feature extraction before inference.

## What Works ✅

1. **ONNX Runtime Integration**: 
   - Version 1.20.1 downloaded and integrated
   - CMake linking working
   - All DLLs properly deployed
   
2. **Model Selection**:
   - WeSpeaker ResNet34 downloaded (25.3 MB)
   - Model metadata inspected:
     - Input: `[B, T, 80]` - batch, time steps, 80-dim features
     - Output: `[B, 256]` - batch, 256-dim embeddings
   - Proven performance: 2% EER on VoxCeleb
   
3. **OnnxSpeakerEmbedder Class**:
   - Complete C++ wrapper implemented
   - String lifetime bug fixed
   - Integration with ContinuousFrameAnalyzer complete

4. **Python Environment**:
   - Local `.venv` created with `uv`
   - onnxruntime installed
   - Can inspect ONNX models programmatically

## Current Blocker ❌

**Input Format Mismatch:**

```
[OnnxEmbedder] Inference error: Invalid rank for input: feats 
Got: 2 Expected: 3 Please fix either the inputs/outputs or the model.
```

**Root Cause:**
- WeSpeaker expects: `[batch=1, time_steps, n_features=80]`
  - Features = 80-dimensional Fbank (filterbank energies)
  - Similar to MFCC but without DCT compression
- Current implementation sends: `[batch=1, audio_samples]`
  - Raw int16 PCM converted to float32
  - No acoustic feature extraction

## Options Forward

### Option A: Implement Fbank Extraction ⭐ RECOMMENDED

**Pros:**
- WeSpeaker is proven model (2% EER)
- Whisper.cpp already has 80-bin mel filterbank code
- Features are standard speech processing
- Flexible, works with any Fbank-based model

**Cons:**
- Requires implementing/exposing feature extraction (~200-300 lines)
- More complex than raw audio
- Need to understand whisper.cpp's internal API

**Implementation:**
- Reuse `log_mel_spectrogram()` from whisper.cpp
- Whisper tiny.en uses 80 mel bins (exact match!)
- Already has all the DSP code (FFT, windowing, mel filterbank)

**Effort:** Medium (4-6 hours)

### Option B: Find Raw Audio Model

**Pros:**
- Simpler integration (no feature extraction)
- Current code would work with minor modifications

**Cons:**
- Unknown if good ONNX models exist that accept raw audio
- May sacrifice accuracy
- Research time unknown

**Effort:** Unknown (2-8 hours of searching + testing)

### Option C: Use Alternative Model

Consider models that might accept raw audio:
- **SpeechBrain ECAPA-TDNN**: But requires ONNX export from Python
- **Titanet**: Also likely needs features
- **Wav2Vec2-based**: Might accept raw audio, but large and slow

**Effort:** High (8-12 hours research + integration)

## Technical Details

### Fbank vs Mel Spectrogram

**Fbank (Filterbank energies):**
- Apply mel filterbank to power spectrum
- Log the energies
- No DCT (unlike MFCC)
- Typically 40-80 bins

**Mel Spectrogram (what Whisper uses):**
- Very similar to Fbank
- Whisper uses 80 mel bins
- Can likely reuse with minor modifications

### Whisper.cpp Mel Extraction

Located in `third_party/whisper.cpp/src/whisper.cpp`:

```cpp
static bool log_mel_spectrogram(
    whisper_state & state,
    const float * samples,
    const int n_samples,
    const int sample_rate,
    const int fft_size,
    const int fft_step,
    const int n_mel,
    const int n_threads,
    const whisper_filters & filters,
    const bool debug,
    whisper_mel & mel
);
```

**Key parameters:**
- `n_mel`: 80 (matches WeSpeaker input!)
- `sample_rate`: 16000 (matches our audio)
- `fft_size`: 400 (WHISPER_N_FFT)
- `fft_step`: 160 (WHISPER_HOP_LENGTH)

**What we need:**
1. Extract this function or create similar
2. Convert int16 PCM → float samples
3. Call mel extraction
4. Reshape output to `[1, time_steps, 80]`
5. Feed to ONNX model

## Recommendation

**Implement Option A: Add Fbank extraction using whisper.cpp code**

**Rationale:**
1. WeSpeaker is well-proven (2% EER)
2. Whisper.cpp already has all the DSP primitives
3. 80-mel bins match exactly
4. Standard approach in speech community
5. Once implemented, can try other Fbank-based models easily

**Action Items:**
1. Study `log_mel_spectrogram()` in whisper.cpp
2. Either:
   - a) Expose it as public API, or
   - b) Extract minimal DSP code to compute Fbank features
3. Modify `OnnxSpeakerEmbedder::preprocess_audio()` to:
   - Compute 80-dim Fbank features
   - Reshape to `[1, T, 80]` tensor
4. Test inference
5. Measure accuracy improvement

**Expected Timeline:**
- Study code: 1 hour
- Implement feature extraction: 2-3 hours
- Integration and testing: 1-2 hours
- **Total: 4-6 hours**

## Alternative: Quick Test with Python

Before full C++ implementation, could verify approach works:

```python
# Use librosa or torchaudio to extract Fbank
import librosa
import numpy as np
import onnxruntime as ort

# Load audio
audio, sr = librosa.load('test.wav', sr=16000)

# Extract 80-dim mel spectrogram (Fbank)
mel_spec = librosa.feature.melspectrogram(
    y=audio, sr=sr, n_fft=400, hop_length=160, n_mels=80
)
log_mel = librosa.power_to_db(mel_spec)

# Reshape to [batch, time, features]
features = np.transpose(log_mel).astype(np.float32)
features = np.expand_dims(features, 0)  # Add batch dimension

# Run ONNX inference
sess = ort.InferenceSession('models/speaker_embedding.onnx')
output = sess.run(None, {'feats': features})
embedding = output[0][0]  # Shape: [256]

print(f"Embedding shape: {embedding.shape}")
print(f"Embedding norm: {np.linalg.norm(embedding):.3f}")
```

This would confirm the approach works before implementing in C++.

## Python Environment Setup (Documented)

```powershell
# Create virtual environment with uv
uv venv .venv

# Activate
.\.venv\Scripts\Activate.ps1

# Install dependencies
uv pip install onnxruntime librosa numpy

# Run test scripts
.\.venv\Scripts\python.exe test_model.py
```

**Important:** Always use `.\.venv\Scripts\python.exe` to ensure local environment is used.

## Next Steps

**Decision Point:** Do you want to:

1. **[RECOMMENDED]** Implement Fbank extraction in C++ (4-6 hours, best accuracy)
2. **[QUICK TEST]** Verify approach with Python first (30 min proof of concept)
3. **[ALTERNATIVE]** Search for raw audio speaker model (unknown time, may sacrifice accuracy)

My recommendation is #2 followed by #1: Quick Python proof-of-concept to verify WeSpeaker works with our audio, then implement feature extraction in C++.
