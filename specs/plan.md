# Phase 2 Implementation Fix - Action Plan

## Date: 2025-10-07

## Problem Summary

Phase 2 implementation (continuous frame analysis at 250ms granularity) **broke Whisper transcription quality**:

- **Expected**: "conservation of momentum" 
- **Got**: "consultless by my" (hallucination)
- **Also**: Audio playback has crackling/artifacts not present in original

## Root Cause Analysis

### What We Changed
1. Replaced original segmentation strategy with new VAD-based 1.5-4s segments
2. Changed from `transcribe_chunk()` to `transcribe_chunk_segments()`
3. Modified audio buffering strategy (continuous stream vs accumulated chunks)
4. Added `assign_speakers_to_segments()` that splits segments (duplicates text)

### What Broke
1. **Whisper transcription quality degraded** - hallucinations, incorrect words
2. **Audio playback has artifacts** - crackling sounds not in source file
3. **Segmentation changed** - went from many small segments to fewer large ones

### Critical Discovery: Why Original Works

Original code uses **small, frequent Whisper segments** (~0.5-1.5s):
```
[S0] what to use the most beautiful
[S0] idea in physics
[S0] conservation of the
[S0] of momentum
[S0] can you elaborate
```

This segmentation strategy gives **excellent transcription quality**:
- ✅ All words correct
- ✅ No hallucinations
- ✅ Natural phrasing
- ✅ Clean audio playback

**WHY THIS WORKS:**
- Small segments = better context focus for Whisper
- Frequent transcription = less audio accumulation = less memory pressure
- Natural pauses = better segmentation boundaries
- Proven in production use

## Assumptions

1. **Whisper segmentation is fragile** - changing segment sizes affects quality
2. **Original segmentation strategy is optimal** - empirically proven good results
3. **Frame extraction is independent** - doesn't need to affect Whisper flow
4. **Parallel processing is viable** - can run frame analyzer alongside original flow

## Target Architecture

### Core Principle: **DO NOT TOUCH WHISPER SEGMENTATION**

Keep original Whisper flow 100% intact, add frame analysis in parallel:

```
Audio Chunk (20ms)
    ↓
    ├─→ [ORIGINAL PATH - UNCHANGED]
    │   ├─→ Accumulate in acc16k buffer
    │   ├─→ Detect pauses/energy
    │   ├─→ Transcribe with Whisper (original strategy)
    │   ├─→ Extract speaker embedding from acc16k
    │   └─→ Output [Sx] text
    │
    └─→ [NEW PATH - PARALLEL]
        ├─→ Feed to ContinuousFrameAnalyzer
        ├─→ Extract embeddings every 250ms
        ├─→ Store in frame buffer
        └─→ (Future) Cluster & assign speaker IDs

```

### What Gets Reverted
- ❌ Remove `audio_stream` continuous buffer
- ❌ Remove `transcribe_chunk_segments()` call
- ❌ Remove `assign_speakers_to_segments()` call
- ❌ Remove VAD-based 1.5-4s segmentation logic
- ❌ Remove last_transcribed_position tracking

### What Gets Kept
- ✅ `ContinuousFrameAnalyzer` class (functional!)
- ✅ `compute_speaker_embedding()` wrapper
- ✅ Frame extraction at 250ms granularity
- ✅ All Phase 2 infrastructure in speaker_cluster.cpp/hpp

### What Gets Added Back
- ✅ Original `acc16k` buffer accumulation
- ✅ Original pause detection logic
- ✅ Original `transcribe_chunk()` call
- ✅ Original speaker embedding extraction
- ✅ Original `SpeakerClusterer::assign()` call
- ✅ **PLUS**: `frame_analyzer.add_audio()` call in parallel

## Implementation Plan

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
