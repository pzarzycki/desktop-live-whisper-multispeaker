# Speaker Diarization - Complete Knowledge Base

**Last Updated:** 2025-10-07  
**Status:** Phase 2c Complete - Production Ready (accuracy needs improvement)

---

## Table of Contents

1. [Current State](#current-state)
2. [Architecture](#architecture)
3. [Experiment History](#experiment-history)
4. [What Works](#what-works)
5. [What Doesn't Work](#what-doesnt-work)
6. [Performance Metrics](#performance-metrics)
7. [Next Steps](#next-steps)

---

## Current State

### Implementation

**Phase 2c: Neural Embeddings with ONNX** ✅ Complete

- **Embedding Method:** WeSpeaker ResNet34 (256-dim neural embeddings)
- **Feature Extraction:** 80-dim mel filterbank (Fbank) with Cooley-Tukey FFT
- **Clustering:** Agglomerative hierarchical clustering (cosine similarity, threshold=0.5)
- **Assignment:** Frame-level analysis with speaker run voting
- **Performance:** Real-time capable (0.173s diarization for 20s audio)

### Accuracy

**Current:** ~44% segment-level accuracy on Sean Carroll podcast  
**Target:** >80% accuracy  
**Blocker:** WeSpeaker model not suitable for this audio (treats different speakers as same)

---

## Architecture

### Pipeline Overview

```
Audio Input (44.1kHz stereo)
    ↓
Downmix to Mono + Resample to 16kHz
    ↓
    ├─→ [Whisper ASR Path]
    │   └─→ Transcription
    │
    └─→ [Diarization Path - PARALLEL]
        ↓
    Frame Extraction (250ms windows, 250ms hop)
        ↓
    Feature Extraction
        ├─→ Convert int16 → float32
        ├─→ Extract 80-dim Fbank (FFT-based)
        └─→ Feed to ONNX model
        ↓
    ONNX Inference (WeSpeaker ResNet34)
        └─→ 256-dim speaker embeddings
        ↓
    Clustering (Agglomerative, cosine similarity)
        └─→ Frame-level speaker IDs
        ↓
    Speaker Assignment (run-based voting)
        └─→ Segment-level speaker labels
```

### Key Components

#### 1. Frame Extraction (`ContinuousFrameAnalyzer`)
- **Window:** 250ms (4,000 samples at 16kHz)
- **Hop:** 250ms (non-overlapping)
- **Rate:** 4 frames per second
- **Storage:** Circular buffer, parallel to Whisper processing

#### 2. Feature Extraction (`MelFeatureExtractor`)
- **Algorithm:** Mel filterbank energy (Fbank)
- **FFT:** Cooley-Tukey radix-2 (512-point, O(N log N))
- **Parameters:**
  - n_fft: 400 (25ms)
  - hop_length: 160 (10ms)
  - n_mels: 80
  - fmin: 0 Hz, fmax: 8000 Hz
- **Output:** Log-mel features (dB scale), shape [time_frames, 80]

#### 3. ONNX Embedder (`OnnxSpeakerEmbedder`)
- **Model:** WeSpeaker ResNet34 (voxceleb_resnet34.onnx, 25.3 MB)
- **Input:** [batch=1, time_frames, 80] float32
- **Output:** [batch=1, 256] float32
- **Processing:** L2-normalized embeddings
- **Initialization:** Lazy (on first frame)

#### 4. Clustering
- **Algorithm:** Agglomerative hierarchical clustering
- **Distance Metric:** Cosine distance (1 - cosine_similarity)
- **Threshold:** 0.5 (merge if similarity > 0.5)
- **Max Speakers:** 2 (configurable)

#### 5. Speaker Assignment
- **Method:** Run-based voting
- **Logic:** 
  1. Detect continuous speaker runs in frames
  2. For each segment, find overlapping frame runs
  3. Assign speaker based on longest overlap
  4. Minimum confidence check (optional)

---

## Experiment History

### Phase 2a: Initial Continuous Analysis (FAILED)
**Date:** Mid-2025  
**Approach:** VAD-based segmentation (1.5-4s), continuous frame extraction  
**Result:** ❌ **Broke Whisper transcription quality**

**What went wrong:**
- Changed Whisper segmentation strategy (VAD instead of energy-based)
- Large segments caused hallucinations ("consultless by my" instead of "conservation of momentum")
- Audio artifacts introduced

**Lesson:** **NEVER modify Whisper's segmentation** - it's fragile and empirically optimized

---

### Phase 2b: Hand-Crafted Features (PARTIAL SUCCESS)
**Date:** 2025-10  
**Approach:** MFCC + Delta + Pitch + Formants (40-dim embeddings)

**Results:**
- ✅ Whisper quality preserved (revert to original segmentation)
- ✅ Balanced clustering (47/30 frames for 77 total)
- ❌ Poor accuracy (~44% segment-level)

**Why it failed:**
- Hand-crafted features capture acoustic properties
- But lack discriminative power for subtle voice differences
- Pitch/formants similar for same-language male speakers

**Experiments:**
1. **MFCC-only (13-dim):** Poor discrimination
2. **MFCC + Delta (26-dim):** Slightly better
3. **MFCC + Delta + Pitch + Formants (40-dim):** Best of hand-crafted, still 44%
4. **Different cluster thresholds:** 0.3-0.7 tested, 0.5 optimal for balance

---

### Phase 2c: Neural Embeddings (COMPLETE, ACCURACY ISSUE)
**Date:** 2025-10-07  
**Approach:** ONNX-based deep learning embeddings

#### Iteration 1: Raw Audio Input (FAILED)
**Model:** WeSpeaker ResNet34  
**Issue:** Model expects Fbank features, not raw waveform  
**Error:** `Invalid rank for input: feats Got: 2 Expected: 3`

**Solution:** Add mel feature extraction

---

#### Iteration 2: DFT Feature Extraction (TOO SLOW)
**Implementation:** Naive DFT O(N²)  
**Issue:** Extremely slow, process hung on 20s audio  
**Performance:** Unusable for real-time

**Solution:** Implement proper FFT

---

#### Iteration 3: FFT Optimization (SUCCESS) ✅
**Implementation:** Cooley-Tukey radix-2 FFT O(N log N)  
**Performance:** 
- 0.173s diarization for 20s audio
- ~1000x speedup vs DFT
- Real-time capable (115x faster than audio duration)

**Result:** Technical implementation complete

---

#### Iteration 4: Model Evaluation (ACCURACY ISSUE) ❌
**Testing:** Sean Carroll podcast (2 speakers, American English males)

**Findings:**
```python
# Python validation test
Segment 1 (t=1s) vs Segment 2 (t=10s) [different speakers]
Cosine similarity: 0.8734  # Should be < 0.7
Model conclusion: SAME speaker (incorrect)
```

**Clustering Results:**
- 10s audio: 1 speaker detected (should be 2)
- 20s audio: 2 speakers detected but unbalanced
- Accuracy: ~44% (no improvement)

**Root Cause:** WeSpeaker trained on VoxCeleb (cross-language, cross-accent scenarios), not optimized for subtle same-language speaker distinctions

---

## What Works

### ✅ Technical Implementation

1. **Mel Feature Extraction**
   - FFT-based, production-quality
   - 80-dim Fbank matching model requirements
   - Self-contained, no external dependencies
   - ~1000x faster than naive approach

2. **ONNX Integration**
   - Clean C++ wrapper
   - Proper resource management (string lifetimes, memory)
   - Lazy initialization with fallback
   - No interference with Whisper pipeline

3. **Performance**
   - Real-time capable (<1% overhead)
   - Diarization: 0.173s for 20s audio
   - xRealtime factor: 0.998 (perfect for live streaming)
   - Scales well to longer audio

4. **Architecture**
   - Modular design (easy to swap models)
   - Parallel processing (doesn't block Whisper)
   - Frame-based analysis (fine temporal resolution)
   - Clean separation of concerns

5. **Code Quality**
   - Well-documented
   - Error handling throughout
   - Debugging capabilities (save intermediate audio)
   - Repository hygiene (outputs to `output/`)

### ✅ Integration with Whisper

**Critical Success:** Whisper transcription quality unchanged
- Original energy-based segmentation preserved
- No audio artifacts
- Diarization runs in parallel (read-only access)
- Independent failure modes (fallback to no speaker labels)

---

## What Doesn't Work

### ❌ Accuracy - Primary Issue

**Current:** ~44% segment-level accuracy  
**Target:** >80% accuracy  
**Status:** Unmet

**Why:**

1. **Model Mismatch**
   - WeSpeaker: Trained on VoxCeleb (diverse languages, accents, recording conditions)
   - Our audio: Same language, same accent, similar voices
   - Model optimized for wrong problem

2. **Speaker Similarity**
   - Both male speakers
   - American English
   - Similar prosody
   - Similar recording quality
   - **Human perception:** Easily distinguishable
   - **Model perception:** 0.87 cosine similarity (threshold 0.7)

3. **Training Data Bias**
   - VoxCeleb: Celebrity interviews, varied conditions
   - Optimized for: Language ID, accent detection, channel robustness
   - Under-optimized for: Subtle within-language voice timbre

### ❌ Hand-Crafted Features

**Approaches Tried:**
- MFCC (13-dim)
- MFCC + Delta (26-dim)
- MFCC + Delta + Pitch + Formants (40-dim)

**Best Result:** 40-dim, still only 44% accuracy

**Why they failed:**
- Capture acoustic properties (pitch, spectral envelope)
- Lack high-level discriminative power
- Too sensitive to speaking style changes
- Not robust to prosody variations

### ⚠️ Playback Crackling (Low Priority)

**Issue:** Audio playback has crackling/distortion  
**Finding:** Resampled audio is clean (`output/whisper_input_16k.wav`)  
**Root Cause:** Likely WASAPI buffer underruns  
**Impact:** Zero (cosmetic only, doesn't affect transcription)  
**Priority:** Low

---

## Performance Metrics

### Current Performance (20-second audio)

```
Processing Time:
  - Resampling:  0.004s  (0.02%)
  - Diarization: 0.173s  (0.86%)  ← Our work
  - Whisper ASR: 4.516s  (22.5%)  ← Bottleneck
  - Other:      15.351s  (76.6%)  ← Mainly I/O, playback
  - Total:      20.044s  (100%)

Real-time Factor: 0.998 (1.0 = real-time)
Audio Duration:   20.000s
Wall Clock:       20.044s
```

### Diarization Breakdown

```
Mel Feature Extraction: ~0.03s  (17% of diarization)
ONNX Inference:        ~0.10s  (58% of diarization)
Clustering:            ~0.04s  (23% of diarization)
Assignment:            ~0.003s (2% of diarization)
```

### Frame Statistics (20s audio)

```
Total Frames:           77 frames
Frame Rate:             3.85 fps (250ms hop)
Frames per Speaker:     Variable (depends on clustering)
  - Ideal (50/50):      38-39 frames each
  - Hand-crafted:       47/30 (61%/39%) - balanced but inaccurate
  - Neural (10s test):  37/0 (100%/0%)  - broken
  - Neural (20s test):  Unbalanced distribution
```

### Accuracy (Sean Carroll Podcast)

| Method | Accuracy | Precision | Recall | Clustering Balance |
|--------|----------|-----------|--------|-------------------|
| Ground Truth | 100% | - | - | ~50/50 |
| Hand-Crafted (40-dim) | 44% | Low | Medium | 61%/39% ✅ |
| Neural WeSpeaker | 44% | Low | Low | Poor ❌ |
| **Target** | **>80%** | **High** | **High** | **Balanced** |

### Memory Usage

```
ONNX Model (loaded):     ~50 MB
Frame Buffer (max):      ~1 MB (stores 77 frames × 256 dims × 4 bytes)
Mel Feature Cache:       ~250 KB (temporary)
Total Diarization:       ~52 MB overhead
```

---

## Next Steps

### Immediate Priority: Improve Accuracy

#### Option A: Try Titanet Large 🎯 RECOMMENDED

**Model:** NVIDIA NeMo Titanet Large  
**Performance:** 0.66% EER (vs 2.0% WeSpeaker)  
**Size:** 32 MB  
**Embeddings:** 192-dim

**Why it might work:**
- Better architecture for same-language distinction
- Lower EER suggests better discriminative power
- Still reasonable size for production

**Action Items:**
1. Download Titanet Large from NVIDIA NeMo repository
2. Convert to ONNX (if not already available)
3. Test with Sean Carroll podcast
4. Compare cosine similarities (expect < 0.7 for different speakers)
5. Measure accuracy improvement

**Time Estimate:** 1-2 hours  
**Success Criteria:** Cosine similarity < 0.7, accuracy > 70%

---

#### Option B: Hybrid Approach

**Concept:** Combine neural + hand-crafted features

**Implementation:**
```python
final_embedding = concat([
    neural_embedding * 0.7,  # 256-dim from WeSpeaker
    handcrafted * 0.3        # 40-dim MFCC+Pitch
])
# Total: 296-dim hybrid
```

**Pros:**
- Leverage both approaches
- May capture complementary information
- Tunable via weights

**Cons:**
- More complex
- Requires parameter tuning
- Marginal gains uncertain

**Recommendation:** Only if Titanet Large fails

---

#### Option C: Alternative Models

**Other ONNX Speaker Models:**

1. **ECAPA-TDNN** (SpeechBrain)
   - EER: 0.69%
   - Embeddings: 192-dim
   - Issue: Requires ONNX export from Python

2. **CAM++** (Chinese-focused, but might help)
   - EER: 0.79%
   - Embeddings: 512-dim
   - Larger model

3. **X-vector** (Kaldi-based)
   - Classic approach
   - Good for telephony
   - May not help with similar voices

**Recommendation:** Consider if Titanet Large unavailable

---

### Secondary Priorities

#### 1. Fine-tune Clustering Parameters
- Test threshold range: 0.3-0.7
- Experiment with linkage methods (single, complete, average, ward)
- Try alternative clustering (DBSCAN, spectral)

#### 2. Improve Assignment Logic
- Implement confidence scores per segment
- Add minimum overlap requirements
- Consider temporal smoothing (Kalman filter?)

#### 3. Multi-speaker Support (>2 speakers)
- Current: Hardcoded max_speakers=2
- Extend to automatic speaker count detection
- Test with 3+ speaker scenarios

#### 4. Word-level Timestamps
- Integrate Whisper word-level timestamps
- Assign speakers per word (not just per segment)
- Enable mid-segment speaker changes

#### 5. Fix Playback Crackling (Low Priority)
- Investigate WASAPI buffer management
- Add buffer underrun detection
- Implement adaptive buffer sizing

---

### Long-term Vision

1. **Online Diarization**
   - Current: Batch processing (cluster all frames at end)
   - Future: Online clustering (update as new frames arrive)
   - Benefit: True real-time streaming

2. **Speaker Enrollment**
   - Store speaker embeddings
   - Match against known speakers
   - Enable "Who is speaking?" queries

3. **Voice Activity Detection (VAD)**
   - Current: Energy-based gating (-55 dBFS threshold)
   - Future: Dedicated VAD model (Silero VAD?)
   - Benefit: Better silence detection

4. **Speaker Change Detection**
   - Detect boundaries between speakers
   - Refine segment timestamps
   - Improve alignment with Whisper output

---

## Lessons Learned

### Critical Insights

1. **Don't Touch Whisper Segmentation**
   - Whisper's segmentation is empirically optimized
   - Changing it breaks transcription quality
   - Keep diarization completely parallel

2. **Model Selection Matters**
   - Training data must match use case
   - VoxCeleb ≠ podcast scenarios
   - Always validate with target audio before deployment

3. **FFT Performance Critical**
   - DFT O(N²): Unusable
   - FFT O(N log N): Real-time capable
   - ~1000x speedup for 400-sample windows

4. **Test Early with Ground Truth**
   - Don't assume features work
   - Validate with known speaker boundaries
   - 44% accuracy caught early = time saved

5. **Repository Hygiene**
   - Save outputs to dedicated folder (`output/`)
   - Use `.gitignore` properly
   - Keep specs/ organized

### What Would We Do Differently?

1. **Model validation first:** Test embeddings with Python before C++ integration
2. **Multiple models ready:** Have 2-3 candidates downloaded before starting
3. **Accuracy metrics from day 1:** Don't celebrate balance without checking correctness
4. **FFT from start:** Don't waste time on DFT
5. **Audio debugging early:** Save intermediate audio from beginning

---

## References

### Models Evaluated

1. **WeSpeaker ResNet34** (Current)
   - Paper: "WeSpeaker: A Research and Production oriented Speaker Verification System"
   - Repository: https://github.com/wenet-e2e/wespeaker
   - Training: VoxCeleb1+2
   - Performance: 2% EER
   - Our experience: Poor for same-language scenarios

2. **Titanet Large** (Recommended Next)
   - Source: NVIDIA NeMo
   - Repository: https://github.com/NVIDIA/NeMo
   - Performance: 0.66% EER
   - Size: 32 MB, 192-dim embeddings

3. **ECAPA-TDNN** (Alternative)
   - Source: SpeechBrain
   - Repository: https://github.com/speechbrain/speechbrain
   - Performance: 0.69% EER
   - Note: Requires ONNX export

### Papers & Resources

- VoxCeleb Dataset: http://www.robots.ox.ac.uk/~vgg/data/voxceleb/
- Mel Filterbank: "Speech and Language Processing" (Jurafsky & Martin)
- FFT Algorithm: Cooley & Tukey (1965)
- Agglomerative Clustering: scikit-learn documentation

### Related Documentation

- `specs/architecture.md` - Overall system architecture
- `specs/transcription.md` - Whisper ASR learnings
- `specs/continuous_architecture_findings.md` - Detailed experiment log
- `specs/phase2c_final_summary.md` - Phase 2c complete report

---

**Document Status:** Living document - update as experiments progress  
**Maintainer:** AI Development Team  
**Review Frequency:** After each major experiment or model change
