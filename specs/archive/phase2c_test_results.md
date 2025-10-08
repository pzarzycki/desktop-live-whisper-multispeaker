# Phase 2c Neural Embeddings - Test Results

## Date: 2025-10-07

## Implementation Status: ✅ COMPLETE

Successfully implemented ONNX-based neural speaker embeddings with:
- Standalone mel filterbank feature extraction (80-dim Fbank)
- WeSpeaker ResNet34 model integration
- Complete pipeline: audio → mel features → ONNX inference → 256-dim embeddings

## Technical Details

### Mel Feature Extraction

**Implementation:** Custom C++ mel filterbank extractor (`src/diar/mel_features.cpp`)
- **FFT Method:** DFT (O(N²)) - simple but slow, adequate for proof-of-concept
- **Parameters:**
  - n_fft: 400 (25ms at 16kHz)
  - hop_length: 160 (10ms at 16kHz)  
  - n_mels: 80
  - fmin: 0 Hz, fmax: 8000 Hz (Nyquist)
- **Output:** Log-mel features in dB scale, shape [time_frames, 80]

**Performance:** ~60 frames/second (slow due to DFT, could be 1000x faster with proper FFT)

### ONNX Integration

- **Model:** WeSpeaker ResNet34 (voxceleb_resnet34.onnx)
- **Input:** [batch=1, time_frames, n_mels=80]
- **Output:** [batch=1, embedding_dim=256]
- **Inference:** Successfully runs without errors
- **Embedding Stats (Python reference):**
  - L2 norm: ~2.99 (not unit-normalized in model)
  - Mean: -0.0047
  - Std: 0.1867
  - Range: [-0.5, 0.5]

## Test Results

### Test 1: 5-second clip
```
[Phase2] Clustering 17 frames...
Result: 1 speaker detected
Transcription: Works correctly
```

### Test 2: 10-second clip
```
[OnnxEmbedder] Initialization complete (with Fbank extraction)
[cluster_frames] Clustering 37 frames with max_speakers=2, threshold=0.50
[cluster_frames] Clustering complete: 1 speakers
  Speaker 0: 37 frames
```

### Test 3: 20-second clip
```
[Phase2] Clustering 77 frames...
Result: 2 speakers detected
Distribution: Unbalanced (still mostly assigned to one speaker)
```

### Python Embedding Quality Test

Tested segments at t=1s and t=10s (should be different speakers based on ground truth):
- **Cosine similarity: 0.8734**
- **Interpretation:** Model considers them SAME speaker (threshold typically 0.7)

## Analysis

### What Works ✅

1. **Technical implementation:** Mel extraction + ONNX inference pipeline fully functional
2. **Model integration:** WeSpeaker loads and runs correctly
3. **Embedding generation:** Produces 256-dim embeddings with reasonable statistics
4. **No Whisper interference:** Transcription quality unchanged

### What Doesn't Work ❌

1. **Speaker discrimination:** Model fails to distinguish the two speakers in Sean Carroll podcast
   - Cosine similarity 0.87 >> threshold 0.7
   - Treats different speakers as same person
2. **Clustering:** Still produces unbalanced results (similar to hand-crafted features)
3. **Accuracy:** No improvement over hand-crafted 44% baseline

## Root Cause Analysis

### Why WeSpeaker Fails Here

**Hypothesis 1: Training Data Mismatch**
- WeSpeaker trained on VoxCeleb (mainly celebrity interviews, varied accents)
- Sean Carroll podcast: Two male speakers, both American English, similar recording quality
- Model may be optimized for accent/language discrimination, not same-language speaker ID

**Hypothesis 2: Short Context**
- Using 1-second windows (16,000 samples)
- WeSpeaker might need longer context for reliable discrimination
- Test showed: Even with ~200 time frames (2 seconds), similarity is high

**Hypothesis 3: Similar Voices**
- The two speakers might genuinely have similar voice characteristics
- Prosody, pitch, timbre all similar
- Even humans might find them hard to distinguish in short clips

### Performance Issues

**DFT is SLOW:**
- Current implementation: O(N²) per frame
- 400-sample DFT × ~4 frames/sec × 77 frames = very slow
- **Solution:** Use proper FFT library (FFTW, KissFFT, or whisper.cpp's FFT)
  - Would give ~1000x speedup
  - Critical for real-time processing

## Recommendations

### Option A: Try Better Model 🎯 RECOMMENDED

**Titanet Large (NVIDIA NeMo)**:
- Performance: 0.66% EER (vs 2% for WeSpeaker)
- Size: 32MB (vs 7MB)
- Better discrimination on similar voices
- Already researched in `specs/speaker_models_onnx.md`

**Action:**
1. Download Titanet Large ONNX model
2. Test with same audio
3. Compare cosine similarities
4. If < 0.7 for different speakers → deploy

### Option B: Optimize Current Implementation

**Add proper FFT:**
- Replace DFT with FFT (whisper.cpp already has one!)
- Expected: 100-1000x speedup
- Makes real-time processing feasible
- Doesn't improve accuracy, just performance

**Action:**
1. Extract whisper.cpp's FFT function
2. Update `mel_features.cpp` to use it
3. Measure new performance

### Option C: Hybrid Approach

**Combine features:**
- Neural embeddings (global voice characteristics)
- Hand-crafted features (prosody, pitch, energy)
- Use both for clustering
- May improve robustness

## Decision Point

**Question:** Do you want to:

1. **[RECOMMENDED]** Try Titanet Large model (better accuracy, same architecture)
   - Time: 30 min (download + test)
   - Expected: Better speaker discrimination

2. **Optimize performance** (add proper FFT)
   - Time: 2-3 hours
   - Expected: 100-1000x faster, same accuracy

3. **Try alternative approach** (hybrid features, different clustering)
   - Time: 4-6 hours
   - Expected: Incremental improvement

4. **Accept current state** and document limitations
   - Some speaker pairs are just hard to distinguish
   - 44% accuracy may be acceptable for some use cases

## Summary

**Implementation:** ✅ Complete and working  
**Accuracy:** ❌ No improvement (WeSpeaker fails on this audio)  
**Performance:** ⚠️ Slow (DFT bottleneck)  
**Whisper quality:** ✅ Unchanged (no interference)  

**Next step:** Try Titanet Large model for better discrimination, then optimize FFT for performance.
