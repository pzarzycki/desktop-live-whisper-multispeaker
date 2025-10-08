# Current Project Status & Plan

**Last Updated:** 2025-10-07  
**Current Phase:** Phase 2c Complete (Neural Embeddings)  
**Next Phase:** Phase 2d (Better Model - Titanet Large)

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

## Phase 2d: Better Speaker Model - IN PROGRESS 🔄

### Objective: Improve Accuracy from 44% to >80%

**Initial Plan:** Try Titanet Large (0.66% EER)  
**Reality Check:** No pre-converted ONNX available (requires PyTorch + NeMo conversion)

**Revised Approach:** Try alternative WeSpeaker models (faster validation)

### Model Search Results (2025-10-07)

**Searched:**
- Titanet Large: Found 8 models on HuggingFace, **no ONNX versions**
- ECAPA-TDNN: Found 30 models, **no pre-converted ONNX**
- WeSpeaker: Found 20 models, **all PyTorch checkpoints**

**Key Finding:** No production-ready ONNX models available for better architectures

### Revised Strategy: Pragmatic Approach

**Option A: Test Alternative WeSpeaker Models** 🎯 **TRYING FIRST**

Available models from WeSpeaker on HuggingFace:
1. **CAMPlus** (`Wespeaker/wespeaker-voxceleb-campplus`)
   - Newer architecture than ResNet34
   - Potentially better discriminative power
   - Same VoxCeleb training (but different architecture)

2. **ECAPA-TDNN 1024** (`Wespeaker/wespeaker-voxceleb-ecapa-tdnn1024`)
   - Larger capacity (1024-dim vs 256-dim ResNet34)
   - ECAPA is state-of-art for speaker verification
   - Same VoxCeleb training

**Pros:**
- Can test quickly (download → convert to ONNX → test)
- Existing infrastructure ready
- 30-60 minutes per model

**Cons:**
- Still VoxCeleb-trained (may have same limitation)
- Not guaranteed to fix accuracy

**Action plan:**
1. Download CAMPlus PyTorch checkpoint
2. Convert to ONNX using torch.onnx.export()
3. Test on Sean Carroll podcast
4. If fails, try ECAPA-TDNN 1024
5. Measure accuracy improvement

---

**Option B: Hybrid Approach** (if Option A fails)

Combine neural embeddings with hand-crafted features:
```python
embedding = concatenate([
    neural_256dim * 0.7,      # WeSpeaker (good for some speakers)
    handcrafted_40dim * 0.3   # MFCCs+pitch (captures prosody)
])
# Total: 296-dim hybrid embedding
```

**Rationale:**
- Neural: Good at global voice timbre
- Hand-crafted: Good at prosodic differences
- Combined might capture complementary information

**Estimated time:** 1-2 hours (modify embedding extraction code)

---

**Option C: Full Conversion** (last resort)

Convert Titanet Large or ECAPA-TDNN from source:

Requirements:
- PyTorch installation
- NeMo toolkit (for Titanet) or SpeechBrain (for ECAPA)
- Model checkpoint download
- ONNX export script
- Input/output format validation

**Estimated time:** 2-4 hours

**Only if:** Options A and B both fail

---

### Current Status: Trying CAMPlus

**Next immediate steps:**
1. Create ONNX conversion script for PyTorch models
2. Download CAMPlus checkpoint
3. Convert to ONNX
4. Test with existing infrastructure
5. Measure accuracy

**Success criteria (same as before):**
- Cosine similarity < 0.7 for different speakers
- Segment-level accuracy > 70%
- Real-time performance maintained
- No Whisper quality regression

**Time estimate:** 2-3 hours (including testing)

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

## Phase 4: UI/UX Improvements - FUTURE

### Qt Quick Desktop App

**Features:**

1. **Real-time Display**
   - Live transcript with speaker labels
   - Color-coded by speaker
   - Auto-scroll

2. **Settings Panel**
   - Model selection (tiny.en / base.en)
   - Diarization on/off
   - Speaker count setting

3. **Export Options**
   - Save transcript (SRT, TXT, JSON)
   - Timestamps included
   - Speaker labels formatted

4. **Audio Device Selection**
   - List available devices
   - Switch between mic/loopback
   - Volume monitoring

---

## Known Issues & Limitations

### Critical Constraints:

⚠️ **DO NOT MODIFY WHISPER SEGMENTATION**
- Empirically optimized, extremely fragile
- Changing it breaks transcription quality (learned in Phase 2a)
- Keep diarization completely parallel

### Current Limitations:

1. **Diarization Accuracy: 44%**
   - Limited by WeSpeaker model
   - Needs better model (Phase 2d)

2. **Max 2 Speakers**
   - Hardcoded for now
   - Will extend in Phase 3

3. **Segment-level Assignment**
   - No word-level speaker changes
   - Will add in Phase 3

4. **Playback Crackling**
   - Cosmetic only, low priority
   - Will fix after accuracy goal met

### Architecture Decisions:

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

### Real-Time Capability:

| Component | Target | Current | Status |
|-----------|--------|---------|--------|
| Overall xRealtime | < 1.0 | 0.998 | ✅ |
| Diarization overhead | < 5% | 0.86% | ✅ |
| Whisper processing | < 30% | 22.5% | ✅ |
| Memory usage | < 500 MB | 320 MB | ✅ |

### Accuracy Requirements:

| Metric | Target | Current | Status |
|--------|--------|---------|--------|
| Whisper WER | < 10% | ~5% | ✅ |
| Diarization accuracy | > 80% | 44% | ❌ |
| Speaker precision | > 90% | Low | ❌ |
| Speaker recall | > 90% | Low | ❌ |

---

## Documentation Status

### Comprehensive Docs Created:

✅ **specs/diarization.md**
- Complete diarization knowledge base
- All experiments documented (Phase 2a/2b/2c)
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

### Docs to Consolidate (After Phase 2d):

**Archive these phase-specific documents:**
- specs/phase2_summary.md
- specs/phase2b_summary.md
- specs/phase2c_final_summary.md
- specs/phase2c_onnx_findings.md
- specs/phase2c_test_results.md
- specs/plan_phase2b_diarization.md
- specs/plan_phase2c_neural.md
- All other plan_*.md files

**Keep as reference:**
- specs/speaker_models_onnx.md (model research)
- specs/continuous_architecture_findings.md (detailed log)

---

## Next Immediate Actions

### Priority Order:

1. **Download Titanet Large model** (30 mins)
   - Find ONNX version or convert from NeMo
   - Place in `models/` directory
   - Validate with Python first

2. **Integrate Titanet Large** (2-3 hours)
   - Update `OnnxSpeakerEmbedder` if needed
   - Handle 192-dim embeddings
   - Test inference works

3. **Benchmark on Test Audio** (1 hour)
   - Run on Sean Carroll podcast
   - Calculate accuracy metrics
   - Compare cosine similarities

4. **Document Results** (1 hour)
   - Update `specs/diarization.md`
   - Add to continuous_architecture_findings.md
   - Update plan.md with decision

5. **If Success: Clean Up Specs Folder** (1 hour)
   - Archive old phase documents
   - Consolidate learnings
   - Update README.md with final metrics

### Estimated Total Time: 6-8 hours

---

## Success Definition

**Phase 2 Complete When:**
- ✅ Real-time performance achieved (<1.0x realtime)
- ✅ Speaker diarization accuracy > 80%
- ✅ No Whisper quality regression
- ✅ Production-ready infrastructure
- ✅ Comprehensive documentation
- ✅ Clean repository (no stray files)

**Current Status:**
- ✅ Real-time: 0.998x (DONE)
- ❌ Accuracy: 44% (NEEDS WORK)
- ✅ Whisper quality: Unchanged (DONE)
- ✅ Infrastructure: Complete (DONE)
- ✅ Documentation: Comprehensive (DONE)
- ✅ Repository: Clean (DONE)

**Remaining:** Try Titanet Large → 80% accuracy → Phase 2 COMPLETE

---

## References

### Key Documents:
- `specs/architecture.md` - System architecture, performance metrics
- `specs/diarization.md` - Complete diarization knowledge
- `specs/transcription.md` - Whisper best practices
- `specs/continuous_architecture_findings.md` - Detailed experiment log

### External Resources:
- NVIDIA NeMo: https://github.com/NVIDIA/NeMo
- WeSpeaker: https://github.com/wenet-e2e/wespeaker
- ONNX Runtime: https://onnxruntime.ai/
- Whisper: https://github.com/openai/whisper

---

**Document Owner:** AI Development Team  
**Last Review:** 2025-10-07  
**Next Review:** After Phase 2d completion## Implementation Plan

### Step 1: Commit Current State
```bash
git add -A
git commit -m "Phase 2 attempt - breaks transcription (save for reference)"
```
**Why**: Preserve the work even though it doesn't work. Shows what NOT to do.

### Step 2: Update Architecture Documentation
Add prominent warning section explaining:
- Why original Whisper segmentation must be preserved
- What makes it work (small segments, frequent transcription)
- What breaks when you change it
- Mark as **CRITICAL - DO NOT MODIFY**

### Step 3: Revert transcribe_file.cpp Whisper Flow
**File**: `src/console/transcribe_file.cpp`

**Revert to original**:
- Restore `acc16k` buffer and accumulation logic
- Restore original pause detection (energy + timing)
- Restore `transcribe_chunk()` call
- Restore original speaker embedding extraction
- Restore `SpeakerClusterer::assign()` call

**Add in parallel**:
```cpp
// In audio processing loop, AFTER adding to acc16k:
frame_analyzer.add_audio(ds.data(), ds.size());
```

**Keep minimal changes**:
- Frame analyzer initialization (with verbose flag)
- Frame statistics output at end
- Verbose output for frame count

### Step 4: Remove Unused Code
**File**: `src/diar/speaker_cluster.cpp`

Keep:
- ✅ `ContinuousFrameAnalyzer` implementation
- ✅ `compute_speaker_embedding()` wrapper
- ✅ `compute_logmel_embedding()` (was already there)

Remove/Comment out:
- ⚠️ `assign_speakers_to_segments()` - not used, has bugs (text duplication)
- Or mark as "// FUTURE: needs word-level text splitting"

**File**: `src/asr/whisper_backend.cpp/hpp`

Keep:
- ⚠️ `transcribe_chunk_segments()` - might be useful later
- Or remove if confirmed unused

### Step 5: Test & Verify
Run tests to confirm:
1. ✅ Transcription quality matches original
2. ✅ No audio artifacts/crackling
3. ✅ Frames being extracted (should see ~80 frames for 20s)
4. ✅ Speaker detection still works
5. ✅ Performance comparable to original

Expected output:
```
[S0] what to use the most beautiful
[S0] idea in physics
[S1] conservation of the
[S1] of momentum
[S0] can you elaborate
...
[Phase2] Frame statistics:
  - Total frames extracted: 77
  - Coverage duration: 20.0s
  - Frames per second: 3.9
```

### Step 6: Document Lessons Learned
Update `specs/architecture.md` with:
- Why this approach works
- Why the previous attempt failed
- Design principles for future development
- How frame extraction integrates without interference

## Success Criteria

- [ ] Transcription quality equals original (no hallucinations)
- [ ] Audio playback is clean (no crackling)
- [ ] Frames extracted successfully (~4 per second)
- [ ] Speaker detection works (alternating S0/S1)
- [ ] Performance acceptable (< 1.5x realtime on target hardware)
- [ ] Code is clean and well-documented
- [ ] Architecture clearly explains what NOT to change

## Future Work (Phase 3+)

Once this is stable:
1. **Phase 3**: Implement online clustering with frame-level speaker IDs
2. **Phase 4**: Map frame speaker IDs to Whisper text output
3. **Phase 5**: Implement retroactive corrections and confidence scores
4. **Phase 6**: Enable word-level speaker changes (if needed)

## Risk Mitigation

**Risk**: Revert introduces regressions
**Mitigation**: We have working original in git, can compare line-by-line

**Risk**: Frame analyzer still interferes somehow
**Mitigation**: Can disable with flag, measure performance impact

**Risk**: Original code doesn't support frame extraction well
**Mitigation**: Frame extraction is read-only, shouldn't affect existing flow

## Timeline Estimate

- Step 1 (Commit): 2 minutes
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
