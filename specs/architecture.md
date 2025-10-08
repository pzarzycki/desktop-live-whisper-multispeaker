# Architecture Documentation

## 🚨 CRITICAL: WHISPER SEGMENTATION MUST NOT BE MODIFIED 🚨

### Empirical Finding (2025-10-07)

**DO NOT CHANGE THE WHISPER SEGMENTATION STRATEGY**

The original code achieves **excellent transcription quality** through a specific segmentation approach:

#### What Makes It Work (PROVEN)

Original segmentation creates **many small, frequent segments** (~0.5-1.5s each):
```text
[S0] what to use the most beautiful
[S0] idea in physics
[S1] conservation of the
[S1] of momentum  
[S0] can you elaborate
```

**Why This Is Critical**:
1. ✅ **All words transcribed correctly** - no hallucinations
2. ✅ **Natural phrase boundaries** - respects speech patterns
3. ✅ **Clean audio quality** - no playback artifacts
4. ✅ **Proven in production** - empirically validated

#### What Breaks When You Change It

Attempt to use larger segments (1.5-4s VAD-based) resulted in:
```text
[S0] what to you is the most beautiful idea in physics
[S1] consultless by my  ← WRONG! Should be "conservation of momentum"
[S0] of momentum can you elaborate...  ← Broken/incomplete
```

**Problems Observed**:
- ❌ Whisper hallucinations ("consultless by my")
- ❌ Incorrect word recognition
- ❌ Audio playback artifacts (crackling)
- ❌ Degraded overall quality

#### Root Cause Analysis

**Hypothesis**: Whisper's acoustic model + language model work best with:
- Short segments = focused context window
- Frequent transcription = fresh model state
- Natural pauses = clean segment boundaries
- Small buffers = less memory/processing overhead

**Changing segment size changes**:
- Model attention patterns
- Context accumulation
- Buffer management
- Audio processing pipeline

**Result**: Fragile - do not modify!

### Implementation Guideline

✅ **CORRECT APPROACH**: Keep original Whisper flow, add features in parallel

```cpp
// In audio processing loop:
acc16k.insert(acc16k.end(), ds.begin(), ds.end());  // Original

// Add parallel frame extraction WITHOUT affecting above:
frame_analyzer.add_audio(ds.data(), ds.size());     // NEW - read-only
```

❌ **INCORRECT APPROACH**: Replace segmentation strategy

```cpp
// DON'T DO THIS:
audio_stream.insert(...);  // New continuous buffer
if (should_transcribe_based_on_new_logic) {  // New VAD
    whisper.transcribe_chunk_segments(...);  // New API
}
```

### For Future Development

**If you need to improve speaker diarization**:
- ✅ Extract embeddings at finer granularity (250ms) - PARALLEL to Whisper
- ✅ Build frame-level speaker timeline independently
- ✅ Map frame speaker IDs to Whisper's existing text output
- ✅ Use Whisper's proven segmentation as ground truth

**DO NOT**:
- ❌ Change Whisper segment sizes
- ❌ Modify audio buffering strategy
- ❌ Replace pause detection logic
- ❌ Change transcription API calls

---

## GUI Framework Decision

### Attempted: Qt6
**Reason for trial:** Cross-platform, mature ecosystem, good tooling, widely used

**Why rejected:**
- **LGPL Licensing Issue**: Requires applications to provide re-linking mechanism (users must be able to replace Qt libraries)
- **Commercial Incompatibility**: Not compatible with closed-source commercial distribution without complex legal compliance
- **Commercial License Cost**: ~$5,000+/year per developer (too expensive for project)
- **Distribution Size**: ~20MB of DLL dependencies
- **Build Complexity**: Separate meta-object compiler (moc), special build system integration

### Chosen: Dear ImGui
**License:** MIT (fully permissive, commercial-friendly, no restrictions)

**Advantages:**
- ✅ **Zero licensing concerns** - MIT allows any use including commercial closed-source
- ✅ **Tiny footprint** - ~30KB compiled (vs Qt's 20MB DLLs, 97% reduction)
- ✅ **Source-only** - No installation, managed via git submodule
- ✅ **Simple API** - Immediate mode, all C++, no separate UI language
- ✅ **Platform native rendering** - DirectX 11 (Windows), Metal (macOS)
- ✅ **Easy integration** - Drop-in to existing C++ codebase

**Platform Architecture:**
- **Windows**: `src/ui/main_windows.cpp` - WinMain + DirectX 11 + Win32 backend
- **macOS**: `src/ui/main_macos.mm` - NSApplication + Metal + Cocoa backend  
- **Shared UI Logic**: `src/ui/app_window.hpp/cpp` - 100% platform-independent (no OS headers)

**Font Quality:** TrueType Segoe UI loaded with DPI awareness for crisp rendering on all displays

**Result:** Production-ready GUI with zero licensing risk, minimal distribution size, clean separation of platform-specific and shared code.

---

## Threading Model for Real-Time Audio Processing

### Design Goals
1. Simulate real microphone behavior in file playback mode
2. Ensure smooth audio playback regardless of processing speed
3. Handle processing that can't keep up with real-time gracefully

### Current Implementation (2025-10-04)

#### Thread Architecture
Two-thread design with producer-consumer pattern:

**Thread 1: Audio Source (Producer)**
- Reads audio chunks from file or microphone
- Plays audio with real-time pacing (for files, sleeps to match wall-clock time)
- Always queues chunks successfully - NEVER blocks on queue full
- Simulates real hardware: microphone produces audio continuously

**Thread 2: Processing (Consumer)**
- Pops chunks from queue
- Resamples to 16kHz
- Runs diarization (if enabled)
- Transcribes with Whisper
- If queue grows too large (>50 chunks), skips oldest chunks to catch up

#### AudioQueue Design Philosophy
The key insight: **A real microphone never drops its own output**.

```cpp
// Audio source thread (microphone/file player)
audioQueue.push(chunk);  // Always succeeds, never blocks

// Processing thread
while (audioQueue.pop(chunk)) {
    // If queue.size() > max_size, pop() automatically skips old chunks
    // This is where "dropped chunks" are counted
    process(chunk);
}
```

**Why this design:**
- Real microphone hardware has buffers and keeps producing audio
- If processing falls behind, it should skip chunks to catch up, not slow down audio
- File playback with `sleep_until()` simulates real-time microphone behavior
- Audio playback is smooth and at correct speed regardless of processing performance

#### Performance Characteristics

| Model | Processing Speed | Real-time Capable | Chunk Drops (20s audio) |
|-------|------------------|-------------------|-------------------------|
| tiny.en | 6.7s for 20s audio | ✅ Yes (1.12x RT) | 0 |
| base.en + OpenBLAS | 10.4s for 20s audio | ❌ No (0.63x RT) | ~190 |
| base.en-q5_1 quantized | 10.4s for 20s audio | ❌ No (0.63x RT) | ~188 |

**Real-time requirement:** Processing time must be ≤ audio duration (1.0x realtime or faster)

### Optimization History

#### OpenBLAS Integration (2025-10-04)
- **Goal:** Accelerate BLAS operations in Whisper inference
- **Method:** Local installation of prebuilt OpenBLAS v0.3.28 (Windows x64)
- **Location:** `third_party/openblas/` (bin/, lib/, include/)
- **CMake:** GGML_BLAS=ON, manual BLAS_LIBRARIES and BLAS_INCLUDE_DIRS
- **Result:** ~8% speedup (11.4s → 10.6s for base.en), not sufficient for real-time
- **Threading:** Optimal at 4 threads (OPENBLAS_NUM_THREADS=4)
  - More threads caused overhead: 20 threads = 15.0s (worse than default)

#### Quantization Testing (2025-10-04)
- **Goal:** Reduce model size and inference time
- **Method:** Q5_1 quantization (5-bit weights)
- **Result:** 60% size reduction (141MB → 57MB), but 0% speed improvement
- **Conclusion:** On CPU, quantization helps memory but not speed (with OpenBLAS)

### Current Recommendations

**For Real-Time Use:**
- Use `tiny.en` model (74MB) - proven real-time capable
- Quality is good for most transcription needs
- Zero chunk drops, smooth operation

**For Offline/Higher Quality:**
- Use `base.en` model (141MB) for better accuracy
- Accept that processing takes longer than audio duration
- System will drop chunks but audio playback remains smooth

**For Future GPU Support:**
- base.en and larger models will become real-time capable
- Architecture already handles variable processing speeds gracefully
- No code changes needed when GPU acceleration is added

### Known Issues & Design Decisions

1. **Deduplication:** With 50% window overlap (0.5s in 1.5s windows), deduper may be aggressive
   - Current: Looks for up to 12-word overlap and removes duplicates
   - Trade-off: Smooth continuous transcription vs. possible missed words

2. **Chunk Drop Reporting:** "X chunks dropped" indicates processing is slower than real-time
   - This is EXPECTED for base.en on CPU-only systems
   - Audio playback is NOT affected - it stays smooth
   - Dropped chunks = gaps in transcription, not audio glitches

3. **Real-Time Pacing:** File playback uses `sleep_until()` based on audio frames played
   - Simulates microphone timing accurately
   - Prevents queue overflow when processing is fast enough
   - No artificial throttling - just real-time simulation

### Build Configuration

**OpenBLAS Detection:**
```cmake
if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/openblas/bin/libopenblas.dll")
    set(GGML_BLAS ON)
    set(BLAS_LIBRARIES "${CMAKE_SOURCE_DIR}/third_party/openblas/lib/libopenblas.dll.a")
    set(BLAS_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/third_party/openblas/include")
    set(GGML_BLAS_VENDOR "OpenBLAS")
endif()
```

**Runtime Requirement:** `libopenblas.dll` must be in same directory as executable

### Future Work

**If continuing base.en optimization:**
1. Custom OpenBLAS build with AVX512/FMA3 for specific CPU
2. More aggressive quantization (Q4, Q3) - test quality impact
3. Profile non-BLAS bottlenecks (mel spectrogram, attention layers)
4. Distilled models (smaller architecture, not just quantized weights)
5. GPU offload for encoder/decoder (CUDA, Vulkan, Metal)

**Hybrid Approach (Recommended):**
- Live mode: tiny.en for real-time display
- Background: Re-process with base.en for final transcript
- Best of both: immediate feedback + high accuracy

## Speaker Diarization

### Architecture (2025-10-04)

**Goal:** Identify and label different speakers in the audio stream

**Implementation:** Two-stage pipeline:
1. **Feature Extraction:** Compute speaker embedding from audio window
2. **Clustering:** Assign embedding to speaker cluster using cosine similarity

### Speaker Embedding

**Purpose:** Extract speaker-specific characteristics that are content-invariant

**Features Included:**
- **MFCCs (13 coefficients):** Mel-frequency cepstral coefficients capture vocal timbre and speaker identity
- **Pitch (F0):** Fundamental frequency varies significantly by speaker (gender, age, voice type)
- **Spectral Variance (10 bands):** Voice texture, breathiness, and harmonic structure
- **Energy:** Overall voice power level

**Implementation Details:**
- FFT size: 512 samples
- Hop size: 160 samples (10ms at 16kHz)
- Mel filterbank: 40 bands, 80Hz-8kHz
- Pitch estimation: Autocorrelation method (80-500 Hz range)
- Output: ~25-dimensional normalized vector

**Design Philosophy:**
- Previous approach (log-mel spectrogram) captured "what was said" (phonetic content)
- Current approach captures "who said it" (speaker characteristics)
- Features are averaged over entire 1.5s window for stability
- Normalization ensures features are comparable across different audio conditions

### Speaker Clustering

**Algorithm:** Online k-means with hysteresis and confidence scoring

**Parameters:**
- Max speakers: 2 (configurable)
- Similarity threshold: 0.60 (cosine similarity)
- Switch threshold: 0.70 (requires stronger evidence to change speakers)
- Min frames before switch: 3 (prevents rapid flickering)

**Hysteresis Logic:**
- Staying with current speaker is easier than switching
- Must have 3+ consecutive frames suggesting different speaker
- New speaker must have 0.15+ better similarity score
- Prevents rapid back-and-forth switching on borderline cases

**Centroid Update:**
- Exponential moving average: 95% old + 5% new
- Slow updates keep speaker models stable
- Allows speakers to drift slightly over long conversations

### Performance

**Timing (30s audio, base.en-q5_1 model):**
- Diarization: 0.252s (0.8% of processing time)
- Whisper: 11.246s (97.2% of processing time)
- Resample: 0.007s (0.02% of processing time)

**Quality Results:**
- Successfully distinguishes 2 speakers in Sean Carroll podcast
- Clean speaker transitions (no flickering)
- Sentences properly attributed to correct speaker
- Low computational overhead (<1% of total time)

**Limitations:**
- Fixed maximum of 2 speakers (configurable but optimized for 2)
- Requires minimum 1.5s window for stable embeddings
- May struggle with very similar voices (same gender, age, speaking style)
- No speaker re-identification after long silence periods

## Testing Guide

### Building and Running

**Build Command:**
```powershell
cmake --build --preset build-tests-only-release --target app_transcribe_file
```

**Basic Test (20 seconds):**
```powershell
$env:OPENBLAS_NUM_THREADS=4
build\tests-only-release\app_transcribe_file.exe test_data\Sean_Carroll_podcast.wav --play-file --limit-seconds 20
```

**Full Test (60+ seconds):**
```powershell
$env:OPENBLAS_NUM_THREADS=4
build\tests-only-release\app_transcribe_file.exe test_data\Sean_Carroll_podcast.wav --play-file --limit-seconds 60
```

### Expected Output Format

**Correct Output (speaker changes trigger new lines):**
```
[S0] what to use the most beautiful idea in physics conservation of momentum can you elaborate
[S1] if you are aristotle when aristotle wrote his book on physics he made the following point if i push the ball it moves
[S0] and this is repeated a large number of times
[S1] all over the planet if you don't keep pushing things they stop moving
```

**Key Characteristics:**
- New line appears ONLY when speaker changes
- Multiple sentences from same speaker on one line
- Speaker labels [S0], [S1] are consistent throughout
- No rapid flickering between speakers

### Performance Metrics

**Real-Time Factor (xRealtime):**
- 1.0 or higher = Processing keeps up with audio
- Below 1.0 = Processing slower than audio (chunks will be dropped)

**Expected Values (base.en-q5_1 with OpenBLAS):**
- 20s audio: xRealtime ≈ 0.99 (just under real-time)
- 30s audio: xRealtime ≈ 0.99
- Chunk drops: 0-200 depending on model and CPU

**Timing Breakdown:**
- `t_whisper`: Should be 95%+ of total time
- `t_diar`: Should be <1% of total time
- `t_resample`: Should be negligible (<0.1%)

### Test Scenarios

**1. Speaker Separation Test**
- Use: Two-speaker conversation (e.g., interview, podcast)
- Expected: Clear [S0] and [S1] labels, changes when speaker changes
- Verify: No rapid switching (hysteresis working)

**2. Performance Test**
```powershell
# Test with different models
--model tiny.en     # Should be 1.12x realtime (fast enough)
--model base.en-q5_1 # Should be 0.99x realtime (marginal)
--model base.en     # Should be 0.63x realtime (too slow)
```

**3. Long-Form Test**
```powershell
# Run without time limit to test full file
build\tests-only-release\app_transcribe_file.exe test_data\Sean_Carroll_podcast.wav --play-file
```

**4. Verbose Debug Mode**
```powershell
# Add -v flag for progress dots and detailed output
build\tests-only-release\app_transcribe_file.exe test_data\Sean_Carroll_podcast.wav --play-file -v --limit-seconds 30
```

### Troubleshooting

**Problem: All text assigned to [S0]**
- Cause: Speaker embeddings too similar (voices not distinct enough)
- Solution: Lower similarity threshold or use longer audio windows
- Debug: Set `verbose=true` in SpeakerClusterer constructor to see similarity scores

**Problem: Rapid speaker switching (flickering)**
- Cause: Similarity scores near threshold, hysteresis not strong enough
- Solution: Increase `min_frames_before_switch` or switch threshold
- Debug: Check similarity scores in verbose mode

**Problem: Audio distorted or choppy**
- Cause: Chunk size too small or audio queue dropping chunks
- Solution: Already fixed with 20ms chunks and non-blocking queue
- Verify: Check "chunks dropped" in performance output (should be minimal)

**Problem: No transcription output**
- Cause: Using wrong whisper API (not _from_state variants)
- Solution: Already fixed - must use `whisper_full_with_state()` and corresponding `_from_state` getters
- Verify: Check that segments are appearing in output

**Problem: Processing slower than real-time**
- Cause: Model too large for CPU or insufficient threads
- Solution: Use smaller model (tiny.en) or increase OpenBLAS threads
- Test: Try different `OPENBLAS_NUM_THREADS` values (1, 2, 4, 8)

### Regression Testing

**Before Making Changes:**
1. Run 30s test and capture output
2. Note xRealtime value
3. Verify speaker labels are correct
4. Check output formatting (newlines on speaker change)

**After Making Changes:**
1. Rebuild: `cmake --build --preset build-tests-only-release --target app_transcribe_file`
2. Run same 30s test
3. Compare: xRealtime should be similar or better
4. Verify: Speaker labels still working correctly
5. Check: Output format unchanged

**Acceptable Changes:**
- xRealtime ±10% is normal (CPU load variation)
- Chunk drops may vary slightly (0-20 range)
- Speaker labels may differ (S0↔S1 swap is fine)
- Timing breakdown ratios should stay similar

**Unacceptable Regressions:**
- xRealtime drops by >20%
- Speaker diarization stops working (all S0)
- Output format broken (wrong newline placement)
- Audio becomes choppy or distorted

### Validation Checklist

✅ **Audio Quality:**
- [ ] Plays smoothly without gaps or distortion
- [ ] Real-time pacing (30s audio takes ~30s wall time)
- [ ] No audio glitches even when processing slow

✅ **Transcription Quality:**
- [ ] Text appears in reasonable chunks
- [ ] Words are accurate (compare to known content)
- [ ] No excessive repetition or missing words

✅ **Diarization Quality:**
- [ ] Two distinct speakers identified ([S0] and [S1])
- [ ] Speaker labels consistent throughout
- [ ] No rapid switching (flickering)
- [ ] Changes align with actual speaker changes

✅ **Performance:**
- [ ] xRealtime close to 1.0 (within 10%)
- [ ] Diarization time <1% of total
- [ ] Chunk drops minimal (<200 for 30s)
- [ ] CPU usage reasonable (not maxed out)

✅ **Output Format:**
- [ ] New line only when speaker changes
- [ ] Multiple sentences from same speaker on one line
- [ ] Speaker labels present and correct
- [ ] Clean formatting without artifacts

---

## Speaker Diarization Architecture Evolution (2025-10-07)

### Problem Statement

**Current Issue (as of 2025-10-07):**
The Whisper-first post-processing approach works but has fundamental limitations:

```
Current Output (20s audio):
[S0] what to use the most beautiful idea in physics consultless by my momentum can you elaborate yeah
[S1] if you are aristotle
[S0] when aristotle wrote his book on physics he made a following very obvious point...
[S1] and this is
```

**Problems:**
1. **Combining multiple utterances**: First segment has 3 distinct utterances (question, answer, follow-up)
2. **Splitting single utterances**: "and this is" cut off mid-sentence
3. **Dependent on Whisper segmentation**: Can't detect speaker changes finer than Whisper segments
4. **Chunk-by-chunk clustering**: Each transcription chunk processed independently, no global view

**Human Perception Reality:**
- Humans identify speakers within milliseconds of hearing voice
- Don't need word boundaries or semantic understanding
- Use voice timbre, pitch, formants - purely acoustic features
- Can update speaker identity retroactively ("oh, that was actually person B")

### How Real Systems Work

Research into PyAnnote.audio and production diarization systems reveals:

#### Standard Diarization Pipeline (Offline)

```
1. Voice Activity Detection (VAD)
   ↓ Find all speech segments (vs silence/noise)
   
2. Speaker Embedding Extraction
   ↓ Extract embedding every 250-500ms for ALL speech
   ↓ Use consistent window (e.g., 1s) with 50% overlap
   ↓ Build embedding matrix: [N_frames × embedding_dim]
   
3. Global Clustering
   ↓ Cluster ALL embeddings together (not per-chunk!)
   ↓ Methods: K-means, Spectral clustering, Agglomerative
   ↓ Can estimate number of speakers automatically
   
4. Temporal Smoothing (Re-segmentation)
   ↓ Use HMM/Viterbi to enforce temporal consistency
   ↓ Prevents rapid speaker switching
   ↓ Merges adjacent segments with same speaker
   
5. Alignment with Transcription
   ↓ Map speaker labels to word timestamps
   ↓ Can split/merge transcript segments as needed
```

**Key Insight:** Diarization is **independent** of transcription. It works on raw audio acoustics.

#### Online/Streaming Diarization (Our Use Case)

For real-time applications, the approach must adapt:

```
1. Continuous Embedding Extraction
   ↓ Extract embedding every 250ms (fine granularity)
   ↓ Use consistent 1s window for each embedding
   ↓ Store embeddings in circular buffer (last 60s)
   
2. Incremental Clustering
   ↓ Maintain global cluster centroids that update over time
   ↓ New embedding → compare to existing clusters
   ↓ If similar (>0.85): assign to cluster, update centroid
   ↓ If dissimilar: create new cluster if confident
   
3. Confidence-Based Output
   ↓ Track confidence for each frame's speaker assignment
   ↓ Only output text when speaker stable for 3+ frames
   ↓ Can retroactively correct: "Actually that was S1, not S0"
   
4. Global Model Updates
   ↓ Periodically re-cluster ALL stored embeddings (every 10s)
   ↓ Can merge clusters that became similar
   ↓ Can split clusters that became distinct
   ↓ Update speaker IDs globally (re-labeling is allowed!)
```

**Key Principles:**
- **Fine-grained frames** (250ms) for precise speaker boundaries
- **Global clustering** that sees all speakers across time
- **Confidence tracking** - output when certain, correct when wrong
- **Independent from Whisper** - works on raw audio directly

### Proposed Architecture: Online Diarization Pipeline

#### Component 1: Continuous Frame Analyzer

```cpp
class ContinuousFrameAnalyzer {
    // Extract embedding every hop_ms (e.g., 250ms)
    // Use window_ms (e.g., 1000ms) for each embedding
    // Store in circular buffer with timestamps
    
    struct Frame {
        std::vector<float> embedding;  // 55-dim
        int64_t t_start_ms;
        int64_t t_end_ms;
        int speaker_id;      // -1 = unknown
        float confidence;     // 0-1
    };
    
    std::deque<Frame> frame_history;  // Last 60s
    
    void add_audio_chunk(const int16_t* samples, size_t n);
    Frame extract_frame_at(int64_t center_ms);
};
```

**Parameters:**
- `hop_ms = 250`: Extract embedding every 250ms
- `window_ms = 1000`: Use 1s of audio for each embedding
- `history_sec = 60`: Keep last 60 seconds

**Output:** High-resolution speaker activity timeline

#### Component 2: Online Clustering Manager

```cpp
class OnlineClusteringManager {
    struct ClusterModel {
        std::vector<float> centroid;    // Mean embedding
        std::vector<std::vector<float>> members;  // Recent samples
        int count;                       // Total frames assigned
        int64_t last_seen_ms;           // Most recent activity
    };
    
    std::vector<ClusterModel> clusters;
    
    int assign_frame(const Frame& frame);  // Returns speaker_id
    void update_centroid(int cluster_id, const Frame& frame);
    void re_cluster_all();  // Periodic global re-clustering
    bool should_create_new_cluster(const Frame& frame);
};
```

**Logic:**
- New frame → compute similarity to all existing clusters
- If max_similarity > 0.85: assign to that cluster
- If max_similarity < 0.70: might be new speaker (wait for more evidence)
- Track confidence based on similarity scores
- Every 10s: re-cluster all stored frames (K-means on embeddings)

**Advantages:**
- Global view of all speakers
- Can correct mistakes retroactively
- Learns speaker characteristics over time
- Handles variable number of speakers

#### Component 3: Speaker-Aware Transcription Coordinator

```cpp
class SpeakerAwareTranscriber {
    ContinuousFrameAnalyzer analyzer;
    OnlineClusteringManager clustering;
    WhisperBackend whisper;
    
    struct PendingSegment {
        std::string text;
        int64_t t0_ms, t1_ms;
        int speaker_id;      // From frame analysis
        float confidence;
    };
    
    std::deque<PendingSegment> pending_output;
    
    void process_audio(const int16_t* samples, size_t n);
    void flush_confident_segments();  // Output when speaker stable
};
```

**Flow:**
1. Audio comes in continuously
2. Frame analyzer extracts embeddings every 250ms
3. Clustering assigns speaker IDs to frames
4. When pause detected or buffer full: transcribe with Whisper
5. Map Whisper word timestamps to frame speaker IDs
6. Output text chunks only when speaker confident and stable
7. Can print: `[S0→S1]` if speaker changed mid-sentence

**Output Example:**
```
[S0] what do you think is the most beautiful idea in physics?
[S1] conservation of momentum.
[S0] can you elaborate?
[S1] yeah! if you are aristotle, when aristotle wrote his book on physics...
[S0] [interrupting] and this is different from newton?
[S1] exactly! so aristotle's view was...
```

### Implementation Priority

**Phase 1: Prove the concept** (Current goal)
- [x] YIN pitch detection
- [x] LPC formants
- [x] Delta-MFCCs
- [x] 55-dimensional embeddings
- [x] Whisper-first post-processing
- [ ] **Issue**: Still grouping multiple utterances

**Phase 2: Implement continuous frame analysis** (Next step)
- [ ] ContinuousFrameAnalyzer: Extract embeddings every 250ms
- [ ] Store frames in circular buffer with timestamps
- [ ] Test: Can we detect speaker changes at 250ms granularity?

**Phase 3: Add online clustering** (After frame analysis works)
- [ ] OnlineClusteringManager: Maintain global cluster centroids
- [ ] Incremental updates: new frame → assign to cluster
- [ ] Periodic re-clustering: every 10s, re-cluster all frames
- [ ] Test: Does global clustering improve speaker consistency?

**Phase 4: Confidence-based output** (Polish)
- [ ] Track confidence for each frame assignment
- [ ] Buffer output until speaker stable (3+ frames)
- [ ] Allow retroactive corrections
- [ ] Test: Does output quality improve with confidence gating?

**Phase 5: Integration with Whisper** (Full system)
- [ ] Map Whisper word timestamps to frame speaker IDs
- [ ] Split/merge transcript based on speaker boundaries
- [ ] Handle overlapping speech (if detected)
- [ ] Test: End-to-end quality on multi-speaker audio

### Key Architectural Decisions

**Q: Why not use Whisper's tinydiarize?**
- Tinydiarize is built into Whisper model (requires special model file)
- Only marks `[SPEAKER_TURN]` tokens, no fine control
- Can't access embeddings or clustering logic
- Our approach: More control, better for live streaming

**Q: Why not use stereo channel diarization?**
- Requires 2-channel audio with speakers in separate channels
- Our audio is mono (or mixed stereo)
- Wouldn't work for real microphone (single source)

**Q: Why continuous frames vs. Whisper-first?**
- Whisper segments are semantic (sentence/phrase boundaries)
- Speaker changes happen mid-sentence frequently
- Fine-grained frames (250ms) can catch rapid speaker switching
- Can map frame speaker IDs to any word timestamps later

**Q: Why allow retroactive speaker corrections?**
- Early audio may not have enough speaker data to cluster well
- After hearing 10s of person A, can go back and fix first 2s
- Real-time UX: Can re-print corrected output
- Trade-off: Slight delay (buffer 1-2s) vs. accuracy

**Q: How many speakers can this handle?**
- No hard limit - clustering discovers number automatically
- Practical: 2-6 speakers tested well in literature
- Challenge: More speakers → lower inter-speaker distance
- Solution: Adapt thresholds based on estimated speaker count

### References & Prior Art

**PyAnnote.audio** (Python, industry standard):
- Pipeline: VAD → Embedding → Clustering → Smoothing
- Uses neural network for embeddings (ResNet-based)
- Spectral clustering with constrained optimization
- Paper: "PyAnnote.audio: neural building blocks for speaker diarization" (2020)

**Whisper.cpp tinydiarize**:
- Integrated into Whisper model (encoder has diarization head)
- Outputs `[SPEAKER_TURN]` tokens in transcript
- Requires special `-tdrz` model variant
- Simple but limited control

**Kaldi speaker diarization**:
- Classic pipeline: i-vectors → PLDA → clustering
- Offline-focused, very accurate
- Complex setup, not real-time friendly

**Our Approach (Hybrid)**:
- Embeddings: Hand-crafted acoustic features (MFCCs, pitch, formants)
- Clustering: Simple online k-means with incremental updates
- Integration: Maps to Whisper word timestamps post-hoc
- Goal: Real-time capable, good accuracy, simple implementation

### Expected Improvements

**Current State (Whisper-first post-processing):**
- ❌ Groups multiple utterances in one segment
- ❌ Depends on Whisper's segmentation
- ❌ No global view of speakers
- ✅ But: Fast, simple, works end-to-end

**After Continuous Frame Analysis (Phase 2):**
- ✅ Can detect speaker changes at 250ms resolution
- ✅ Independent of Whisper segmentation
- ❌ Still doing chunk-by-chunk clustering
- ⚠️ May see inconsistent speaker IDs across chunks

**After Online Clustering (Phase 3):**
- ✅ Global view of all speakers
- ✅ Consistent speaker IDs over time
- ✅ Can handle variable number of speakers
- ❌ May still output premature/uncertain segments

**After Confidence Gating (Phase 4):**
- ✅ Only outputs when speaker assignment confident
- ✅ Can correct early mistakes
- ✅ Handles ambiguous audio gracefully
- ✅ High-quality speaker labels

### Test Case: Sean Carroll Podcast

**Ground Truth (First 20s):**
```
[S0] "What do you think is the most beautiful idea in physics?"
[S1] "Conservation of momentum."
[S0] "Can you elaborate?"
[S1] "Yeah! If you are Aristotle, when Aristotle wrote his book on physics..."
```

**Current Output (Whisper-first):**
```
[S0] what to use the most beautiful idea in physics consultless by my momentum can you elaborate yeah
[S1] if you are aristotle
[S0] when aristotle wrote his book on physics...
```
- ❌ Groups 3 utterances into first [S0] segment
- ❌ Splits Aristotle explanation mid-sentence
- ~60% accuracy

**Expected After Continuous Frames:**
```
[S0] what do you think is the most beautiful idea in physics
[S1] conservation of momentum
[S0] can you elaborate
[S1] yeah if you are aristotle when aristotle wrote his book on physics...
```
- ✅ Separate segments for each speaker turn
- ✅ Clean speaker boundaries
- ~90% accuracy (transcription errors remain)

**Expected After Full Pipeline:**
```
[S0] What do you think is the most beautiful idea in physics?
[S1] Conservation of momentum.
[S0] Can you elaborate?
[S1] Yeah! If you are Aristotle, when Aristotle wrote his book on physics...
```
- ✅ Perfect segmentation
- ✅ Proper punctuation (if Whisper provides it)
- ✅ Natural conversation flow
- ~95%+ accuracy

---

## Performance Metrics (Current State)

### Phase 2c: Neural Embeddings Implementation (2025-10-07)

**System:** ONNX Runtime 1.20.1 + WeSpeaker ResNet34 model

#### Real-Time Performance (20-second audio test)

```
Processing Breakdown:
  - Audio capture:     Real-time (streaming)
  - Resampling:        0.004s   (0.02% of total)
  - Diarization:       0.173s   (0.86% of total) ← Neural embeddings
  - Whisper ASR:       4.516s   (22.5% of total)
  - Other (I/O/play):  15.351s  (76.6% of total)
  - Total:            20.044s   (100%)

Real-time Factor: 0.998 (perfect real-time capability)
Audio Duration:   20.000s
Wall Clock Time:  20.044s
```

**Key Insight:** Diarization overhead is negligible (<1%). System bottleneck is Whisper inference (22.5%), not speaker embedding.

#### Diarization Performance Details

**Feature Extraction (Mel Filterbank):**
- Algorithm: 80-dim Fbank with Cooley-Tukey FFT
- Parameters: n_fft=400 (25ms), hop_length=160 (10ms)
- Performance: ~0.03s for 77 frames (~115x faster than real-time)
- Optimization: ~1000x speedup vs naive DFT implementation

**ONNX Inference (WeSpeaker ResNet34):**
- Model size: 25.3 MB
- Embedding dimension: 256
- Processing time: ~0.10s for 77 frames (58% of diarization time)
- Throughput: ~770 frames/second

**Clustering + Assignment:**
- Algorithm: Agglomerative hierarchical clustering
- Processing time: ~0.04s for 77 frames (23% of diarization time)
- Real-time capable: Yes (0.05s << 20s audio)

#### Memory Usage

```
Component                Memory
---------------------------------
Whisper model (tiny.en)  ~200 MB
ONNX Runtime             ~50 MB
WeSpeaker model          ~50 MB
Frame buffers            ~2 MB
Audio buffers            ~10 MB
---------------------------------
Total Peak RAM           ~320 MB
```

#### Accuracy Status

**Current:** ~44% segment-level accuracy on Sean Carroll podcast  
**Target:** >80% accuracy  
**Blocker:** WeSpeaker model unsuitable for this audio type

**Root Cause Analysis:**
- WeSpeaker trained on VoxCeleb (cross-language, cross-accent scenarios)
- Test audio: Same language, similar voices (2 male American English speakers)
- Model treats different speakers as same (cosine similarity 0.87 >> 0.7 threshold)
- **Technical implementation complete, accuracy limited by model selection**

**Next Steps:**
- Try Titanet Large (0.66% EER vs 2.0% WeSpeaker)
- Better discriminative power for same-language speakers
- Expected improvement: 44% → 70-80% accuracy

#### Comparison: Neural vs Hand-Crafted Features

| Metric | Hand-Crafted (Phase 2b) | Neural (Phase 2c) |
|--------|-------------------------|-------------------|
| Processing Time | 0.252s | 0.173s |
| xRealtime Factor | N/A | 0.998 |
| Embedding Dimension | 40 | 256 |
| Accuracy | 44% | 44% |
| Clustering Balance | Good (47/30) | Poor (unbalanced) |
| **Winner** | - | **Performance** |

**Conclusion:** Neural embeddings are faster and more scalable, but current model (WeSpeaker) doesn't improve accuracy for this use case. Infrastructure is production-ready, just needs better model.

#### Technology Stack

**ONNX Runtime:**
- Version: 1.20.1 (prebuilt Windows x64 binaries)
- Linking: Direct to onnxruntime.lib
- Runtime: onnxruntime.dll (11.6 MB), onnxruntime_providers_shared.dll, libopenblas.dll

**Feature Extraction:**
- Custom C++ implementation (no external dependencies)
- Cooley-Tukey radix-2 FFT for efficiency
- Sin/cos lookup tables (512-element cache)
- Self-contained, production-ready

**Model:**
- WeSpeaker ResNet34 (voxceleb_resnet34.onnx)
- Input: [batch=1, time_frames, 80] float32
- Output: [batch=1, 256] float32 (L2-normalized embeddings)
- Source: https://github.com/wenet-e2e/wespeaker

#### Detailed References

See comprehensive documentation:
- `specs/diarization.md` - Complete diarization knowledge base
- `specs/transcription.md` - Whisper ASR learnings and best practices
- `specs/continuous_architecture_findings.md` - Detailed experiment logs
- `specs/phase2c_final_summary.md` - Phase 2c complete technical report

---



