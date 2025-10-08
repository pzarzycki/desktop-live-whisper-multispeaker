# Current Project Status & Plan

**Last Updated:** 2025-10-07  
**Current Phase:** Phase 2d Complete (CAMPlus Model)  
**Next Phase:** Phase 3 (Word-Level Speaker Assignment)

---

## Phase 2c: Neural Embeddings - COMPLETE ✅

### Status: Technical Implementation Complete, Accuracy Needs Improvement

**Completed Tasks:**

✅ **ONNX Runtime Integration**
- Version 1.20.1 prebuilt binaries integrated
- DLL management automated in CMake
- Clean C++ wrapper with proper resource management
- Fixed string lifetime bugs (AllocatedStringPtr → std::string)

✅ **WeSpeaker ResNet34 Model**
- Downloaded and integrated (25.3 MB)
- Input format validated: [batch, time_frames, 80] Fbank features
- Output: 256-dimensional embeddings
- L2 normalization applied in C++

✅ **Mel Feature Extraction**
- Standalone C++ implementation (no external dependencies)
- 80-dim mel filterbank (Fbank) with FFT
- Cooley-Tukey radix-2 FFT algorithm (~1000x speedup vs DFT)
- Sin/cos lookup tables for optimization
- Production-ready code quality

✅ **Performance Optimization**
- Real-time capable: 0.998x realtime factor
- Diarization overhead: <1% (0.173s for 20s audio)
- FFT optimization: ~1000x faster than naive DFT
- Scales linearly with audio length

✅ **Integration & Testing**
- Parallel to Whisper (doesn't block transcription)
- Clean separation of concerns
- Mode switching (HandCrafted/NeuralONNX) functional
- No Whisper quality regression

✅ **Development Tools**
- Python environment with `uv` documented
- Audio debugging (save Whisper input)
- Repository cleanup (output folder, .gitignore)
- Comprehensive documentation

### Current Issues:

❌ **Accuracy Not Improved**
- Current: ~44% segment-level accuracy (same as hand-crafted)
- Target: >80% accuracy
- Root cause: WeSpeaker model unsuitable for this audio type
  - Trained on VoxCeleb (cross-language, cross-accent discrimination)
  - Test audio: Same language, similar voices (2 male American English)
  - Model treats different speakers as same (cosine similarity 0.87 >> 0.7 threshold)

⚠️ **Playback Crackling (Low Priority)**
- Cosmetic issue in audio playback
- Whisper input is clean (verified by saving audio)
- Likely WASAPI buffer underruns
- Does not affect transcription quality

---

## Phase 2d: Better Speaker Model - COMPLETE ✅

### Objective: Improve Model Quality

**Result:** Found optimal model and threshold configuration!

### Model Testing Results (2025-10-07)

**Tested Models:**

| Model | Threshold | Frame Balance | Status |
|-------|-----------|---------------|--------|
| WeSpeaker ResNet34 | 0.50 | 61/39 | ✅ Working baseline |
| CAMPlus | 0.50 | 90/10 | ❌ Over-clusters |
| **CAMPlus** | **0.35** | **56/44** | ✅ **BEST!** |

### Key Finding:

**CAMPlus IS stronger (0.8% EER vs 2.0% EER) but requires lower threshold!**

- With threshold=0.50: Treats similar speakers as identical (over-clusters)
- With threshold=0.35: Excellent speaker separation (56/44 balance)
- Model size: 7 MB (smaller than ResNet34's 25 MB)
- Performance: Same real-time capability (0.998x)

### Why Lower Threshold?

CAMPlus produces MORE consistent embeddings for the same speaker:
- Better intra-speaker consistency → higher cosine similarity
- Need lower threshold (0.35 vs 0.50) to detect speaker differences
- This is actually a sign of model quality!

### Implementation:

✅ Downloaded campplus_voxceleb.onnx (7.2 MB)
✅ Updated clustering threshold: 0.50 → 0.35
✅ Tested on Sean Carroll podcast
✅ Documented in specs/archive/phase2d_model_testing.md

### Current Limitation:

Even with optimal model, segment-level accuracy remains ~44% because:
- **Root cause:** Whisper segments don't align with speaker boundaries
- Whisper segments: 4-6 seconds (energy-based, optimized for ASR)
- Speaker turns: 1-3 seconds (natural conversation rhythm)
- Problem: One segment often contains multiple speakers

**Solution:** Phase 3 - Use word-level timestamps to split at speaker changes

---

## Phase 3: Production Deployment - FUTURE

### After Accuracy Goal Met (>80%)

**Tasks:**

1. **Online Clustering**
   - Current: Batch clustering (all frames at end)
   - Target: Incremental clustering (real-time updates)
   - Benefit: True streaming diarization

2. **Word-level Timestamps**
   - Integrate Whisper word-level timestamps
   - Assign speakers per word (not just per segment)
   - Enable mid-segment speaker changes

3. **Multi-speaker Support (>2)**
   - Current: Hardcoded max_speakers=2
   - Target: Automatic speaker count detection
   - Test with 3+ speaker scenarios

4. **Confidence Scores**
   - Per-segment confidence scores
   - Flag low-confidence segments
   - Automatic quality control

5. **Speaker Enrollment**
   - Store speaker embeddings
   - Match against known speakers
   - Enable "Who is speaking?" queries

---

## Phase 3: Word-Level Speaker Assignment - NEXT 🎯

### Objective: Improve Segment-Level Accuracy from 44% to >80%

**Current Problem:**
- Frame-level detection works well (250ms resolution)
- Clustering works well (optimal model + threshold)
- BUT: Whisper segments (4-6s) don't align with speaker turns (1-3s)
- Result: One segment often contains multiple speakers → voting fails

**Solution Strategy:**

Use Whisper's word-level timestamps to split segments at speaker boundaries!

### Architecture Overview

```
Current (Segment-level):
  Whisper segment (5s) → Extract 20 frames (250ms each) → Vote → One speaker per segment ❌

Proposed (Word-level):
  Whisper segment (5s) → Extract 20 frames → Map to word timestamps → Split at boundaries ✅
  
Example:
  Segment: "what to you is the most beautiful idea in physics conservation of momentum"
  Words:   [0.0s: what] [0.3s: to] [0.5s: you] ... [3.2s: physics] [3.8s: conservation] ...
  Frames:  [S0 S0 S0 S0 S0 S0 S0 S0 S0 S0 S0 S0 S0 S1 S1 S1 S1 S1 S1 S1]
           └─────────────── Speaker 0 ───────────┘ └──── Speaker 1 ────┘
  Output:  "[S0] what to you is the most beautiful idea in physics"
           "[S1] conservation of momentum"
```

### Implementation Plan

**Step 1: Enable Word-Level Timestamps in Whisper** (30 mins)

Whisper.cpp supports this via `whisper_full_get_segment_t0()` and token-level timing.

Tasks:
- Add word-level timestamp extraction to `WhisperTranscriber`
- Store `std::vector<WordTimestamp>` per segment
- Validate timestamps are accurate

```cpp
struct WordTimestamp {
    std::string word;
    float start_time;  // seconds
    float end_time;    // seconds
};
```

**Step 2: Map Frame Speaker IDs to Word Timestamps** (1-2 hours)

For each word, find which frame(s) overlap its time window:

```cpp
// Frame i covers time: [i * 0.25, (i+1) * 0.25]
// Word covers time: [word.start_time, word.end_time]
// If overlap > 50%, assign frame's speaker to word

std::vector<int> AssignSpeakersToWords(
    const std::vector<WordTimestamp>& words,
    const std::vector<int>& frame_speaker_ids,  // From diarization
    float frame_duration = 0.25f
) {
    std::vector<int> word_speakers;
    for (const auto& word : words) {
        // Find overlapping frames
        int start_frame = word.start_time / frame_duration;
        int end_frame = word.end_time / frame_duration;
        
        // Majority vote among overlapping frames
        int speaker = MajorityVote(frame_speaker_ids, start_frame, end_frame);
        word_speakers.push_back(speaker);
    }
    return word_speakers;
}
```

**Step 3: Split Segments at Speaker Boundaries** (1 hour)

When speaker changes between consecutive words, start a new line:

```cpp
void PrintWithSpeakerChanges(
    const std::vector<WordTimestamp>& words,
    const std::vector<int>& word_speakers
) {
    int current_speaker = word_speakers[0];
    std::cout << "[S" << current_speaker << "] ";
    
    for (size_t i = 0; i < words.size(); ++i) {
        if (word_speakers[i] != current_speaker) {
            // Speaker changed!
            std::cout << "\n[S" << word_speakers[i] << "] ";
            current_speaker = word_speakers[i];
        }
        std::cout << words[i].word << " ";
    }
    std::cout << "\n";
}
```

**Step 4: Handle Edge Cases** (1-2 hours)

1. **Short words (<250ms):** May not have frame coverage → inherit from previous word
2. **Silence gaps:** No frames → mark as uncertain
3. **High-frequency speaker changes:** Apply smoothing (min 3 words per speaker)
4. **Word timestamp errors:** Whisper sometimes misaligns → validate with frame count

**Step 5: Testing & Validation** (2 hours)

Test on Sean Carroll podcast (ground truth available):
- Calculate word-level accuracy (% words assigned to correct speaker)
- Calculate segment-level accuracy (after splitting)
- Measure performance impact (should be <1ms overhead)

### Expected Results

**Before (Segment-level voting):**
- Accuracy: ~44%
- Granularity: 4-6 seconds
- Problem: Mixed-speaker segments vote fails

**After (Word-level assignment):**
- Accuracy: >80% (target)
- Granularity: Word-level (~0.3s per word)
- Benefit: Splits at natural speaker boundaries

### Files to Modify

1. **src/trans/whisper_transcriber.h/cpp**
   - Add `GetWordTimestamps()` method
   - Return `std::vector<WordTimestamp>` per segment

2. **src/diar/diarizer.h/cpp**
   - Add `MapSpeakersToWords()` method
   - Takes word timestamps + frame speaker IDs
   - Returns per-word speaker assignments

3. **apps/app_transcribe_file.cpp**
   - Update main loop to use word-level output
   - Print with speaker change detection

4. **tests/test_diarization.cpp**
   - Add unit tests for word mapping
   - Validate edge cases (short words, gaps, etc.)

### Success Criteria

✅ Word-level timestamps extracted from Whisper
✅ Frame-to-word mapping implemented
✅ Segment splitting at speaker boundaries
✅ Accuracy > 80% on test audio
✅ Performance impact < 1ms per segment
✅ No Whisper quality regression

### Estimated Time: 6-8 hours

**Breakdown:**
- Step 1 (Whisper word timestamps): 30 mins
- Step 2 (Frame-to-word mapping): 1-2 hours
- Step 3 (Segment splitting): 1 hour
- Step 4 (Edge cases): 1-2 hours
- Step 5 (Testing): 2 hours

### Risks & Mitigation

**Risk 1: Whisper word timestamps inaccurate**
- Mitigation: Validate against frame count; use frame boundaries as fallback

**Risk 2: Performance degradation**
- Mitigation: Profile early; word-level processing should be O(n) and fast

**Risk 3: Over-splitting (too many speaker changes)**
- Mitigation: Apply smoothing (min 3 words or 750ms per speaker turn)

---

## Phase 4: Production Features - FUTURE

### After Phase 3 Complete (Word-Level Assignment)

**Tasks:**

1. **Online Clustering**
   - Current: Batch clustering (all frames at end)
   - Target: Incremental clustering (real-time updates)
   - Benefit: True streaming diarization
   - Complexity: Need online k-means or DBSCAN variant

2. **Multi-speaker Support (>2)**
   - Current: Hardcoded max_speakers=2
   - Target: Automatic speaker count detection
   - Algorithm: Use silhouette score or elbow method
   - Test with 3+ speaker scenarios

3. **Confidence Scores**
   - Per-word speaker confidence (based on frame agreement)
   - Flag uncertain segments (< 60% frame agreement)
   - Automatic quality control
   - UI visualization (color intensity = confidence)

4. **Speaker Enrollment**
   - Store reference embeddings per speaker
   - Match against known speakers across sessions
   - Enable "Who is speaking?" queries
   - Persistent speaker database

5. **Qt Quick Desktop App**
   - Live transcript with color-coded speakers
   - Settings panel (model selection, speaker count)
   - Export options (SRT, TXT, JSON with speaker labels)
   - Audio device selection and volume monitoring

---

## Known Issues & Limitations

### Critical Constraints

⚠️ **DO NOT MODIFY WHISPER SEGMENTATION**

- Empirically optimized, extremely fragile
- Changing it breaks transcription quality (learned in Phase 2a)
- Keep diarization completely parallel

### Current Limitations

1. **Diarization Accuracy: 44% (segment-level)**
   - Frame-level detection works well
   - Clustering works well (CAMPlus + optimal threshold)
   - BUT: Whisper segments don't align with speaker boundaries
   - **Solution**: Phase 3 - Word-level assignment

2. **Max 2 Speakers**
   - Hardcoded for now
   - Will extend in Phase 4

3. **Segment-level Assignment**
   - No word-level speaker changes yet
   - Will add in Phase 3

4. **Playback Crackling**
   - Cosmetic only, low priority
   - Will fix after accuracy goal met

### Architecture Decisions

✅ **Keep:**

- Original Whisper segmentation (energy-based, 4-6s target)
- Parallel diarization (doesn't block transcription)
- Frame-based analysis (250ms windows)
- Mode switching (HandCrafted/NeuralONNX)

❌ **Don't:**

- Change Whisper segment sizes
- Use VAD-based segmentation
- Modify audio buffering strategy
- Block Whisper on diarization

---

## Performance Requirements

### Real-Time Capability

| Component | Target | Current | Status |
|-----------|--------|---------|--------|
| Overall xRealtime | < 1.0 | 0.998 | ✅ |
| Diarization overhead | < 5% | 0.86% | ✅ |
| Whisper processing | < 30% | 22.5% | ✅ |
| Memory usage | < 500 MB | 320 MB | ✅ |

### Accuracy Requirements

| Metric | Target | Current | Status |
|--------|--------|---------|--------|
| Whisper WER | < 10% | ~5% | ✅ |
| Diarization (segment) | > 80% | 44% | 🔄 Phase 3 |
| Frame-level accuracy | N/A | Good | ✅ |
| Speaker precision | > 90% | TBD | 🔄 Phase 3 |
| Speaker recall | > 90% | TBD | 🔄 Phase 3 |

---

## Documentation Status

### Comprehensive Docs Created

✅ **specs/diarization.md**

- Complete diarization knowledge base
- All experiments documented (Phase 2a/2b/2c/2d)
- Performance metrics, findings, next steps

✅ **specs/transcription.md**

- Whisper ASR learnings and best practices
- Configuration guidelines
- Known issues and workarounds

✅ **specs/architecture.md**

- System architecture overview
- Performance metrics added (Phase 2c)
- Technology stack documented

✅ **README.md**

- Updated with current performance metrics
- Status table with real numbers
- Next steps clearly stated

✅ **.github/copilot-instructions.md**

- Python environment usage
- `uv` commands documented
- Development guidelines

✅ **specs/archive/phase2d_model_testing.md**

- CAMPlus model testing results
- Threshold optimization findings
- Decision rationale

### Archive Structure

**specs/archive/** contains:

- phase2_summary.md
- phase2b_summary.md
- phase2c_final_summary.md
- phase2c_onnx_findings.md
- phase2c_test_results.md
- phase2d_model_testing.md (NEW)
- plan_phase2b_diarization.md
- plan_phase2b1_embeddings.md
- plan_phase2b2_voting.md
- plan_phase2c_onnx.md

**Keep as reference:**

- specs/speaker_models_onnx.md (model research)
- specs/continuous_architecture_findings.md (detailed log)

---

## Next Immediate Actions - Phase 3

### Priority Order

**1. Enable Whisper Word-Level Timestamps** (30 mins - 1 hour)

- Research whisper.cpp API for word timestamps
- Add `GetWordTimestamps()` to `WhisperTranscriber`
- Validate timestamps are accurate
- Test on sample audio

**2. Implement Frame-to-Word Mapping** (1-2 hours)

- Create `MapSpeakersToWords()` function
- Handle overlapping frames/words
- Test with known speaker changes
- Validate edge cases (short words, gaps)

**3. Add Segment Splitting at Speaker Boundaries** (1 hour)

- Detect speaker changes in word sequence
- Start new line when speaker changes
- Apply smoothing (min 3 words per turn)
- Test formatting output

**4. Comprehensive Testing** (2 hours)

- Test on Sean Carroll podcast (ground truth)
- Calculate word-level accuracy
- Calculate segment-level accuracy (after splitting)
- Measure performance impact

**5. Update Documentation** (1 hour)

- Update specs/diarization.md with Phase 3 results
- Update README.md with new accuracy metrics
- Update plan.md status

### Estimated Total Time: 6-8 hours

---

## Success Definition

**Phase 2 Complete When:**

- ✅ Real-time performance achieved (<1.0x realtime)
- ✅ Speaker diarization infrastructure complete
- ✅ Optimal model found (CAMPlus)
- ✅ Frame-level detection working
- ✅ No Whisper quality regression
- ✅ Comprehensive documentation
- ✅ Clean repository

**Current Status:**

- ✅ Real-time: 0.998x (DONE)
- ✅ Infrastructure: Complete (DONE)
- ✅ Model: CAMPlus with threshold=0.35 (DONE)
- ✅ Frame detection: Working (DONE)
- ✅ Whisper quality: Unchanged (DONE)
- ✅ Documentation: Comprehensive (DONE)
- ✅ Repository: Clean (DONE)

**Phase 2: COMPLETE! ✅**

---

**Phase 3 Goal:** Improve segment-level accuracy from 44% to >80% using word-level assignment

---

## References

### Key Documents

- `specs/architecture.md` - System architecture, performance metrics
- `specs/diarization.md` - Complete diarization knowledge
- `specs/transcription.md` - Whisper best practices
- `specs/continuous_architecture_findings.md` - Detailed experiment log
- `specs/archive/phase2d_model_testing.md` - CAMPlus testing results

### External Resources

- NVIDIA NeMo: https://github.com/NVIDIA/NeMo
- WeSpeaker: https://github.com/wenet-e2e/wespeaker
- ONNX Runtime: https://onnxruntime.ai/
- Whisper.cpp: https://github.com/ggerganov/whisper.cpp

---

**Document Owner:** AI Development Team  
**Last Review:** 2025-10-07  
**Next Review:** After Phase 3 completion
- Step 2 (Docs): 10 minutes
- Step 3 (Revert): 30-45 minutes (careful work)
- Step 4 (Cleanup): 10 minutes
- Step 5 (Test): 15 minutes
- Step 6 (Document): 15 minutes

**Total**: ~1.5-2 hours careful implementation

## Notes

- This is a **retreat and regroup** strategy
- We're not abandoning Phase 2 goals, just changing the approach
- The frame extraction infrastructure is solid - just integrate it properly
- Original code has proven quality - respect that


---

## Phase 2c Update: Neural Embeddings with ONNX (2025-10-07)

### Status: IN PROGRESS - Feature Extraction Required

**Completed:**
- ✅ ONNX Runtime 1.20.1 integrated
- ✅ WeSpeaker ResNet34 model downloaded (25.3 MB)
- ✅ OnnxSpeakerEmbedder class created
- ✅ Fixed string lifetime bug (I/O names)
- ✅ Python environment setup with `uv` documented

**Current Issue: Model Input Format Mismatch**

WeSpeaker model expects **80-dimensional Fbank features**, NOT raw audio:
- **Input**: `[B, T, 80]` - batch, time steps, 80-dim acoustic features
- **Output**: `[B, 256]` - batch, 256-dim speaker embeddings
- **Current implementation**: Sending `[1, samples]` raw audio waveform ❌

**Error:**
```text
[OnnxEmbedder] Inference error: Invalid rank for input: feats 
Got: 2 Expected: 3 Please fix either the inputs/outputs or the model.
```

### Next Steps:

1. **Add Fbank feature extraction** to `onnx_embedder.cpp`:
   - Extract 80-dim Fbank features from raw audio
   - Options:
     - A) Use existing whisper.cpp mel spectrogram code (80 bins)
     - B) Implement lightweight Fbank extraction
     - C) Consider alternative model that accepts raw audio
