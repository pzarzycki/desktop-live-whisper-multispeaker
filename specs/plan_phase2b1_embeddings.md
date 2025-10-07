# Phase 2b-1: Improve Speaker Embeddings

## Date: 2025-10-07

## Goal
Fix speaker discrimination by implementing proper speaker embeddings without breaking Whisper transcription.

## Current State

**Problem**: 40-dim mel features not discriminative enough
- Clustering: Finds 2 speakers but S0=76 frames, S1=1 frame (unbalanced)
- Root cause: Mel features designed for speech/silence, not speaker identity

**Voting Problem** (separate issue, will address in Phase 2b-2):
- Majority vote fails when speakers have unbalanced duration within segments
- Even with perfect embeddings, needs smarter voting strategy

## Two-Phase Approach

### Phase 2b-1: Better Embeddings (THIS PHASE)

**Objective**: Make frame embeddings discriminative enough to separate speakers

**Options** (in order of complexity):

#### Option A: Enhanced Hand-Crafted Features (Quick Win)
Add speaker-discriminative features to current implementation:
- **Delta & Delta-Delta MFCCs**: Capture temporal dynamics (how voice changes over time)
- **Pitch statistics**: F0 mean, range, variance (not just average)
- **Formants**: F1, F2, F3 via LPC analysis (vocal tract characteristics)
- **Energy statistics**: Mean, variance, dynamic range
- **Spectral features**: Centroid, rolloff, flux

**Pros**: 
- ✅ No external dependencies
- ✅ Fast to implement (~1-2 hours)
- ✅ Good baseline for comparison
- ✅ Literature shows 70-80% accuracy for 2 speakers

**Cons**:
- ❌ Still hand-crafted (may not generalize well)
- ❌ Requires careful feature engineering
- ❌ May need threshold tuning

#### Option B: ONNX + Pretrained Model (Best Quality)
Integrate neural speaker embedding model:
- **Models**: x-vector, ECAPA-TDNN, ResNet-based embeddings
- **Source**: Hugging Face, SpeechBrain, pyannote.audio
- **Runtime**: ONNX Runtime C++ API

**Pros**:
- ✅ State-of-the-art accuracy (90%+ for 2 speakers)
- ✅ Pretrained on thousands of speakers
- ✅ Robust to accent, noise, prosody
- ✅ Industry standard approach

**Cons**:
- ❌ External dependency (ONNX Runtime)
- ❌ Model file to download/include
- ❌ Slightly higher compute cost
- ❌ More integration work (~3-4 hours)

### Phase 2b-2: Smarter Voting (NEXT PHASE)
After embeddings are good, fix segment assignment:
- Detect speaker changes WITHIN segments
- Options: sliding window, transition detection, split segments
- See `plan_phase2b2_voting.md` (to be created)

## Recommended Plan: Start with Option A

### Why Option A First?
1. **Fast validation**: Can test in 1-2 hours
2. **No dependencies**: Works with current build system
3. **Educational**: Understand what features matter
4. **Baseline**: Compare against Option B later
5. **Good enough**: Literature shows 70-80% accuracy achievable

If Option A doesn't work well enough, we'll have:
- Better understanding of the problem
- Clear performance baseline
- Easy path to Option B

## Option A Implementation Plan

### Step 1: Implement Enhanced Features (45 min)

**File**: `src/diar/speaker_cluster.cpp`

Add new function: `compute_speaker_embedding_v2()`

**Features to extract**:
1. **MFCCs (13 coefficients)**: Vocal tract shape
   - DCT of log-mel spectrum
   - Capture timbre and voice characteristics
   
2. **Delta MFCCs (13 coefficients)**: First-order derivatives
   - How MFCCs change over time
   - Capture speaking dynamics
   
3. **Delta-Delta MFCCs (13 coefficients)**: Second-order derivatives
   - Acceleration of MFCC changes
   - Capture prosody and rhythm
   
4. **Pitch features (4 values)**:
   - F0 mean (fundamental frequency)
   - F0 range (max - min)
   - F0 variance (stability/tremor)
   - Voiced ratio (fraction of voiced frames)
   
5. **Formant features (3 values)**:
   - F1, F2, F3 (first three formants)
   - Via LPC analysis (Linear Predictive Coding)
   - Vocal tract resonances
   
6. **Energy features (3 values)**:
   - RMS energy mean
   - Energy variance
   - Dynamic range
   
7. **Spectral features (4 values)**:
   - Spectral centroid (brightness)
   - Spectral rolloff (where high frequencies drop)
   - Spectral flux (how spectrum changes)
   - Zero crossing rate (noisiness)

**Total**: 13+13+13+4+3+3+4 = **53 dimensions**

### Step 2: Update Frame Extraction (15 min)

**File**: `src/diar/speaker_cluster.cpp`

Update `compute_speaker_embedding()` to call new `_v2()` function:

```cpp
std::vector<float> compute_speaker_embedding(const int16_t* pcm16, size_t samples, int sample_rate) {
    return compute_speaker_embedding_v2(pcm16, samples, sample_rate);
}
```

### Step 3: Test and Validate (30 min)

**Expected improvements**:
- Clustering should find balanced speakers (e.g., S0=40 frames, S1=37 frames)
- Segment assignment should alternate [S0] [S1] [S0] [S1]
- Transcription quality unchanged (Whisper untouched)

**If results are good**:
- ✅ Stop here, document success
- Move to Phase 2b-2 (voting improvements)

**If results still poor**:
- Document what didn't work
- Move to Option B (ONNX + pretrained model)

### Step 4: Document Results (15 min)

Update this file with:
- Test output (frame distribution)
- Clustering quality
- Segment assignment accuracy
- Next steps decision

## Option B Implementation Plan (If Needed)

### Prerequisites
1. Add ONNX Runtime dependency to CMakeLists.txt
2. Download pretrained speaker embedding model (.onnx file)
3. Test model inference independently

### Implementation
1. Create `OnnxSpeakerEmbedder` class wrapper
2. Load model in constructor
3. Preprocess audio (resample if needed, normalize)
4. Run inference to get embeddings
5. Replace `compute_speaker_embedding()` call

### Model Options
- **SpeechBrain ECAPA-TDNN**: 192-dim embeddings, ~5MB model
- **PyAnnote.audio**: ResNet-based, 512-dim embeddings, ~15MB
- **X-vector**: Classic approach, 512-dim, ~10MB

## Success Criteria

**Phase 2b-1 success**:
- [ ] Clustering finds 2 speakers with balanced distribution (40/37 not 76/1)
- [ ] Most segments assigned correctly (70%+ accuracy minimum)
- [ ] Whisper transcription still perfect
- [ ] No build errors, no runtime crashes

**Ready for Phase 2b-2 when**:
- Embeddings are discriminative (balanced clustering)
- But segment assignment still imperfect due to voting

## Risks

**Risk**: Enhanced features still not enough
**Mitigation**: Have Option B ready as backup

**Risk**: Breaking Whisper again
**Mitigation**: Zero changes to Whisper code path, only frame extraction changes

**Risk**: Performance degradation
**Mitigation**: Profile first, optimize if needed (but 53-dim is still fast)

## Timeline

**Option A**: ~2 hours
- Implementation: 45 min
- Integration: 15 min
- Testing: 30 min
- Documentation: 15 min
- Buffer: 15 min

**Option B** (if needed): ~4 hours
- ONNX integration: 2 hours
- Model selection/download: 30 min
- Testing: 1 hour
- Documentation: 30 min

## References

**Hand-crafted features for speaker recognition**:
- Reynolds et al. (2000): "Speaker Verification Using Adapted Gaussian Mixture Models"
- Davis & Mermelstein (1980): "Comparison of Parametric Representations for Monosyllabic Word Recognition"

**Delta/Delta-Delta MFCCs**:
- Young et al. (2006): "The HTK Book" - Section 5.6
- Standard in speech recognition since 1990s

**LPC formant extraction**:
- Markel & Gray (1976): "Linear Prediction of Speech"
- Rabiner & Schafer (1978): "Digital Processing of Speech Signals"

**ONNX + Pretrained models**:
- SpeechBrain: https://speechbrain.github.io/
- PyAnnote.audio: https://github.com/pyannote/pyannote-audio
- Hugging Face speaker embeddings: Various models available
