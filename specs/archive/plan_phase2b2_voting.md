# Phase 2b-2: Improved Voting Strategy

## Date: 2025-10-07

## Current State After Phase 2b-1

**Embeddings**: ✅ Working well (S0=47 frames, S1=30 frames - balanced!)

**Current Voting**: Simple majority vote per segment
```cpp
// For each segment [t_start, t_end]:
auto frames = get_frames_in_range(t_start, t_end);
// Count votes: S0=8, S1=1 → assign S0
```

**Problem**: Speaker changes WITHIN segments are lost by majority vote

### Example Issue

Ground truth:
```
Segment [1500-3000ms]: "can you elaborate? yeah if you..."
  First part (0-750ms): S0 asking question
  Second part (750-1500ms): S1 answering
```

Current behavior:
```
Frames: S0 S0 S0 S0 S1 S1 S1 S1 S1 (majority S1)
Result: [S1] can you elaborate? yeah if you...
```

Expected:
```
Either:
  [S0] can you elaborate?
  [S1] yeah if you...
OR:
  [S0/S1] can you elaborate? yeah if you... (mark transition)
```

## Voting Strategy Options

### Option 1: Weighted by Position (Quick)
Give more weight to frames at segment boundaries:
- First 20% of frames: 2x weight for speaker assignment
- Middle 60%: 1x weight
- Last 20%: 2x weight

**Pros**: Simple, fast
**Cons**: Doesn't detect mid-segment changes

### Option 2: Run-Based Assignment (Better)
Detect speaker runs within segment:
```
Frames: S0 S0 S0 S1 S1 S1 S1 S1 S1
Runs:   [S0: 3] [S1: 6]
Logic: Assign to longest run (S1)
```

**Pros**: Better than pure majority
**Cons**: Still assigns entire segment to one speaker

### Option 3: Speaker Change Detection (Best)
Detect transitions within segments:
```
Frames: S0 S0 S0 S1 S1 S1 S1 S1 S1
Change at frame 3 (transition from S0→S1)
Split: First 33% = S0, Last 67% = S1
```

**Pros**: Can split segments at speaker boundaries
**Cons**: Requires text splitting (complex for Whisper output)

### Option 4: Confidence-Weighted (Practical)
Use frame confidence and temporal weighting:
```
For each segment:
  - Get all frames
  - Find longest consistent speaker run
  - If run covers >70% of segment: use that speaker
  - If mixed (no dominant run): use boundary frames (first/last)
  - If still ambiguous: mark as [S0→S1] transition
```

**Pros**: Practical, doesn't require text splitting
**Cons**: Some ambiguous segments remain

## Recommended Approach: Option 4 + Transition Detection

### Implementation Strategy

1. **Detect speaker runs** within frame sequence
2. **Assign based on dominant run** (>70% coverage)
3. **For mixed segments**: Weight boundary frames more
4. **Mark transitions**: If no clear dominant speaker, mark as `[S0→S1]`
5. **Optional**: Allow text splitting at word boundaries (future)

### Algorithm Pseudocode

```cpp
struct SpeakerRun {
    int speaker_id;
    int start_frame;
    int end_frame;
    float confidence;  // average frame confidence
};

std::vector<SpeakerRun> detect_runs(const std::vector<Frame>& frames) {
    // Group consecutive frames with same speaker
    std::vector<SpeakerRun> runs;
    int current_speaker = frames[0].speaker_id;
    int run_start = 0;
    
    for (int i = 1; i <= frames.size(); ++i) {
        if (i == frames.size() || frames[i].speaker_id != current_speaker) {
            runs.push_back({current_speaker, run_start, i-1, compute_avg_confidence(frames, run_start, i-1)});
            if (i < frames.size()) {
                current_speaker = frames[i].speaker_id;
                run_start = i;
            }
        }
    }
    return runs;
}

int assign_speaker_smart(const std::vector<Frame>& frames) {
    if (frames.empty()) return -1;
    
    auto runs = detect_runs(frames);
    
    // Find longest run
    SpeakerRun longest = runs[0];
    for (const auto& run : runs) {
        if (run.end_frame - run.start_frame > longest.end_frame - longest.start_frame) {
            longest = run;
        }
    }
    
    // If dominant run covers >70% of frames: use it
    float coverage = (longest.end_frame - longest.start_frame + 1) / float(frames.size());
    if (coverage > 0.70f) {
        return longest.speaker_id;
    }
    
    // Mixed segment: use boundary frames
    int first_speaker = frames[0].speaker_id;
    int last_speaker = frames.back().speaker_id;
    
    if (first_speaker == last_speaker) {
        return first_speaker;  // Same speaker at both ends
    }
    
    // Transition detected: use last speaker (person currently speaking)
    return last_speaker;
}
```

### Enhanced with Transition Marking

```cpp
struct SegmentAssignment {
    int primary_speaker;
    int secondary_speaker;  // -1 if none
    bool has_transition;
    float transition_point;  // 0.0-1.0 position in segment
};

SegmentAssignment assign_with_transition(const std::vector<Frame>& frames) {
    auto runs = detect_runs(frames);
    
    SegmentAssignment result;
    result.primary_speaker = runs[0].speaker_id;
    result.secondary_speaker = -1;
    result.has_transition = false;
    result.transition_point = 0.0f;
    
    if (runs.size() > 1) {
        // Find major transition (largest run change)
        int best_idx = 0;
        int max_combined = 0;
        
        for (int i = 0; i < runs.size() - 1; ++i) {
            int combined = (runs[i].end_frame - runs[i].start_frame) + 
                          (runs[i+1].end_frame - runs[i+1].start_frame);
            if (combined > max_combined) {
                max_combined = combined;
                best_idx = i;
            }
        }
        
        if (runs[best_idx].speaker_id != runs[best_idx+1].speaker_id) {
            result.has_transition = true;
            result.primary_speaker = runs[best_idx].speaker_id;
            result.secondary_speaker = runs[best_idx+1].speaker_id;
            result.transition_point = float(runs[best_idx].end_frame) / frames.size();
        }
    }
    
    return result;
}
```

## Implementation Plan

### Step 1: Add Run Detection (30 min)

**File**: `src/console/transcribe_file.cpp`

Add helper function before segment assignment loop:

```cpp
struct SpeakerRun {
    int speaker_id;
    int start_idx;
    int end_idx;
};

std::vector<SpeakerRun> detect_speaker_runs(
    const std::vector<diar::ContinuousFrameAnalyzer::Frame>& frames)
{
    std::vector<SpeakerRun> runs;
    if (frames.empty()) return runs;
    
    int current_speaker = frames[0].speaker_id;
    int run_start = 0;
    
    for (size_t i = 1; i <= frames.size(); ++i) {
        if (i == frames.size() || frames[i].speaker_id != current_speaker) {
            runs.push_back({current_speaker, run_start, static_cast<int>(i - 1)});
            if (i < frames.size()) {
                current_speaker = frames[i].speaker_id;
                run_start = static_cast<int>(i);
            }
        }
    }
    return runs;
}
```

### Step 2: Update Voting Logic (30 min)

Replace simple majority vote with run-based assignment:

```cpp
// OLD: Simple majority
std::map<int, int> vote_counts;
for (const auto& frame : frames) {
    if (frame.speaker_id >= 0) {
        vote_counts[frame.speaker_id]++;
    }
}

// NEW: Run-based with boundary weighting
auto runs = detect_speaker_runs(frames);

if (runs.empty()) {
    seg.speaker_id = -1;
    continue;
}

// Find longest run
SpeakerRun longest_run = runs[0];
int longest_length = runs[0].end_idx - runs[0].start_idx + 1;

for (const auto& run : runs) {
    int length = run.end_idx - run.start_idx + 1;
    if (length > longest_length) {
        longest_length = length;
        longest_run = run;
    }
}

// Check if dominant
float coverage = static_cast<float>(longest_length) / frames.size();
if (coverage > 0.70f) {
    seg.speaker_id = longest_run.speaker_id;
} else {
    // Mixed: use last speaker (person currently speaking when segment ends)
    seg.speaker_id = frames.back().speaker_id;
}
```

### Step 3: Add Transition Marking (Optional, 20 min)

Update output to show transitions:

```cpp
bool has_transition = (runs.size() > 1 && runs[0].speaker_id != runs.back().speaker_id);

if (has_transition && verbose) {
    fprintf(stderr, "\n[S%d→S%d] %s", 
            runs[0].speaker_id, 
            runs.back().speaker_id,
            seg.text.c_str());
} else if (seg.speaker_id >= 0) {
    fprintf(stderr, "\n[S%d] %s", seg.speaker_id, seg.text.c_str());
}
```

### Step 4: Test and Validate (30 min)

Expected improvements:
- Segments with clear dominant speaker: same as before
- Segments with transitions: assigned to last speaker (more natural)
- Verbose mode: shows transition markers `[S0→S1]`

## Success Criteria

- [ ] Run detection works correctly (tested with debug output)
- [ ] Dominant run assignment (>70% coverage) produces stable results
- [ ] Mixed segments assigned to last speaker (not first/random)
- [ ] Optional transition markers working in verbose mode
- [ ] Whisper transcription still perfect
- [ ] No performance degradation

## Expected Improvements

**Before Phase 2b-2**:
```
[S0] can you elaborate
[S1] yeah if you are aristotle  ← Wrong! Question was S0
```

**After Phase 2b-2**:
```
[S0] can you elaborate           ← Correct (boundary weighting)
[S1] yeah if you are aristotle   ← Correct
```

Or with transitions:
```
[S0→S1] can you elaborate yeah if you...  ← Mark mixed segments
```

## Timeline

- Step 1 (Run detection): 30 min
- Step 2 (Update voting): 30 min
- Step 3 (Transitions): 20 min
- Step 4 (Testing): 30 min
- **Total**: ~2 hours

## Notes

- This approach doesn't split text (Whisper segments remain intact)
- Future enhancement: Use Whisper word timestamps to split at exact boundaries
- For now: Better speaker assignment + optional transition marking
- Maintains Whisper quality guarantee (no changes to transcription)
