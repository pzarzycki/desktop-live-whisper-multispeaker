# Phase 2b: Improve Speaker Diarization

## Date: 2025-10-07

## Current State

**Transcription Quality**: ✅ EXCELLENT
```
[S0] what to you is the most beautiful
[S0] idea in physics
[S0] conservation of
[S0] momentum
[S0] can you elaborate
```

**Speaker Detection**: ❌ NEEDS IMPROVEMENT - All segments labeled [S0]

**Frame Extraction**: ✅ WORKING - 77 frames @ 3.9 fps

## Ground Truth (Expected Output)

```
[S0] what to you is the most beautiful idea in physics?
[S1] conservation of momentum.
[S0] can you elaborate?
[S1] yeah! if you are aristotle, when aristotle wrote his book on physics...
```

**Expected Pattern**: Alternating speakers (question/answer format)

## Problem Analysis

### Why All Segments Are [S0]

Current flow:
1. Each 1.5s segment → extract embedding from `acc16k`
2. Call `SpeakerClusterer::assign(embedding)`
3. Clusterer compares to existing centroids
4. All segments are similar enough → assigned to same speaker

**Root Cause**: Per-segment embeddings from short windows don't capture speaker differences well enough. The segments are too short and similarity threshold may be too strict.

### Current SpeakerClusterer Behavior

```cpp
SpeakerClusterer spk(2, 0.60f);  // max_speakers=2, threshold=0.60
```

- **Threshold 0.60**: Cosine similarity threshold for creating new speaker
- **Hysteresis**: Requires multiple confirmations before switching speakers
- **Per-segment**: Analyzes each 1.5s window independently

**Issue**: Short segments may not have enough speaker-discriminative information.

## Solution Strategy

### Option 1: Use Frame-Level Speaker IDs (RECOMMENDED)

**Approach**: Leverage the 77 frames we're already extracting!

```
1. Continuous frames (250ms hop) → better temporal resolution
2. Cluster ALL frames globally → identify speakers across entire audio
3. For each Whisper segment → query frames in that time range
4. Assign speaker based on majority vote from frames
```

**Advantages**:
- ✅ Uses existing frame extraction infrastructure
- ✅ Higher temporal resolution (250ms vs 1.5s)
- ✅ Global clustering = better speaker separation
- ✅ More data points = more accurate assignments
- ✅ Doesn't modify Whisper flow at all

**Implementation**:
- Cluster frames after all audio processed
- Store speaker IDs in frames
- Query frames by time range for each segment
- Majority vote determines segment speaker

### Option 2: Improve Per-Segment Clustering (SIMPLER)

**Approach**: Fix current `SpeakerClusterer` parameters

```cpp
// Current:
SpeakerClusterer spk(2, 0.60f);  // threshold too high?

// Try:
SpeakerClusterer spk(2, 0.45f);  // lower threshold = easier to create 2nd speaker
```

**Advantages**:
- ✅ Minimal code changes
- ✅ Quick to test
- ✅ No new infrastructure needed

**Disadvantages**:
- ❌ Still per-segment (less data)
- ❌ May be unstable (too sensitive)
- ❌ Doesn't use frame extraction

### Option 3: Hybrid Approach

**Approach**: Use both methods

1. Extract frames continuously (already working)
2. Cluster frames globally
3. Use frame clusters to guide segment-level decisions
4. Fall back to per-segment if frames unavailable

## Recommended Plan: Option 1 (Frame-Based)

### Step 1: Implement Simple Frame Clustering

**File**: `src/diar/speaker_cluster.cpp`

Add function to cluster frames after processing:
```cpp
void ContinuousFrameAnalyzer::cluster_frames(int max_speakers, float threshold) {
    // Simple k-means or agglomerative clustering
    // For now: just compare first frame to subsequent frames
    if (m_frames.empty()) return;
    
    // First frame = Speaker 0
    m_frames[0].speaker_id = 0;
    std::vector<std::vector<float>> centroids;
    centroids.push_back(m_frames[0].embedding);
    
    for (size_t i = 1; i < m_frames.size(); ++i) {
        // Find best matching centroid
        int best_speaker = 0;
        float best_sim = cosine_similarity(m_frames[i].embedding, centroids[0]);
        
        for (int s = 1; s < centroids.size(); ++s) {
            float sim = cosine_similarity(m_frames[i].embedding, centroids[s]);
            if (sim > best_sim) {
                best_sim = sim;
                best_speaker = s;
            }
        }
        
        // Create new speaker if similarity too low and haven't hit max
        if (best_sim < threshold && centroids.size() < max_speakers) {
            best_speaker = centroids.size();
            centroids.push_back(m_frames[i].embedding);
        }
        
        m_frames[i].speaker_id = best_speaker;
        
        // Update centroid (running average)
        for (size_t d = 0; d < centroids[best_speaker].size(); ++d) {
            centroids[best_speaker][d] = 
                0.9f * centroids[best_speaker][d] + 0.1f * m_frames[i].embedding[d];
        }
    }
}
```

### Step 2: Query Frames for Segment Speaker

**File**: `src/console/transcribe_file.cpp`

After processing all audio, cluster frames:
```cpp
// After audio processing loop, before final output
fprintf(stderr, "\n[Phase2] Clustering frames...\n");
frame_analyzer.cluster_frames(2, 0.50f);  // max_speakers=2, threshold=0.50
```

Then modify segment output to use frame-based speaker IDs:
```cpp
// When outputting each segment:
int64_t seg_start_ms = /* calculate from segment timing */;
int64_t seg_end_ms = /* calculate from segment timing */;

// Query frames in this time range
auto frames = frame_analyzer.get_frames_in_range(seg_start_ms, seg_end_ms);

// Majority vote
std::map<int, int> vote_counts;
for (const auto& frame : frames) {
    if (frame.speaker_id >= 0) {
        vote_counts[frame.speaker_id]++;
    }
}

int segment_speaker = -1;
int max_votes = 0;
for (const auto& [spk, count] : vote_counts) {
    if (count > max_votes) {
        max_votes = count;
        segment_speaker = spk;
    }
}
```

### Step 3: Test and Validate

Expected improvement:
```
[S0] what to you is the most beautiful idea in physics?
[S1] conservation of momentum.
[S0] can you elaborate?
[S1] yeah! if you are aristotle...
```

## Implementation Steps

### 1. Add Helper Functions

**File**: `src/diar/speaker_cluster.cpp`

```cpp
// Add cosine similarity helper (if not already exists)
static float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b);

// Add clustering method to ContinuousFrameAnalyzer
void cluster_frames(int max_speakers, float threshold);

// Already exists: get_frames_in_range()
```

### 2. Update Main Processing Loop

**File**: `src/console/transcribe_file.cpp`

After audio processing completes:
```cpp
// Cluster frames globally
if (frame_analyzer.frame_count() > 0) {
    frame_analyzer.cluster_frames(2, 0.50f);
}
```

### 3. Calculate Segment Timing

Need to track absolute timestamps for segments:
```cpp
// In processing loop, track time offset
int64_t audio_offset_ms = 0;

// When outputting segment:
int64_t seg_start_ms = audio_offset_ms;
int64_t seg_end_ms = audio_offset_ms + (acc16k.size() * 1000) / target_hz;

// Query frames and vote
auto speaker = get_segment_speaker_from_frames(frame_analyzer, seg_start_ms, seg_end_ms);
```

### 4. Update Output Logic

Replace current speaker assignment with frame-based lookup.

## Success Criteria

- [ ] Frames clustered into 2 speakers
- [ ] Alternating speaker pattern in output
- [ ] First segments = S0 (questions)
- [ ] Second segments = S1 (answers)
- [ ] Transcription quality still excellent
- [ ] No crashes or errors

## Risks and Mitigations

**Risk**: Frame clustering doesn't work well
**Mitigation**: Can fall back to current per-segment method, or tune threshold

**Risk**: Timing alignment issues between frames and segments
**Mitigation**: Use ms-based timestamps consistently, validate with logs

**Risk**: Breaking Whisper transcription again
**Mitigation**: NO changes to Whisper flow, only post-processing changes

## Timeline

- Step 1 (Add clustering): 30 minutes
- Step 2 (Integrate with output): 30 minutes  
- Step 3 (Test and debug): 30 minutes
- Step 4 (Validate and document): 15 minutes

**Total**: ~2 hours

## Alternative: Quick Test with Lower Threshold

If we want to test quickly, just change one line:

```cpp
// Current:
SpeakerClusterer spk(2, 0.60f);

// Try:
SpeakerClusterer spk(2, 0.45f);  // Lower threshold
```

This might immediately improve speaker detection without any other changes. Worth trying first!
