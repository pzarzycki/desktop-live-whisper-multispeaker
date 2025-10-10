# Circular Buffer with Symmetric Context (PROPOSED)

**Status:** 💡 PROPOSED - Not yet implemented  
**Author:** User suggestion (2025-01-XX)  
**Current:** Hold-and-emit with asymmetric context (backward only)  
**Proposed:** Delay processing to have symmetric context (backward + forward)

## Problem with Current Approach

**Current strategy:**
```
Window 1 (0-10s):
├─ Has backward context: None (it's the first window)
├─ Has forward context: None (we process immediately)
└─ Quality at end (7-10s): Reduced (no forward context)

Window 2 (7-17s):
├─ Has backward context: Yes (3s overlap from window 1)
├─ Has forward context: None (we process immediately when buffer full)
└─ Quality at start (10-13s): POOR (no forward context)
```

**Test results:**
- Window 2 starting at 10s: "So we've not had a mess, but okay..."
- whisper-cli (full context): "he made a following very obvious point."

**Root cause:** We process audio AS SOON AS buffer fills (10s). We don't wait for more audio to provide forward context.

## Proposed Solution: Delay Processing for Symmetric Context

### Core Idea

**Instead of processing immediately, WAIT for forward context:**

```
t=0:     Buffer [0-3s]         (wait - need more)
t=3s:    Buffer [0-6s]         (wait - need more)
t=6s:    Buffer [0-9s]         (wait - need more)
t=9s:    Buffer [0-12s]        (wait - need more)
t=12s:   Buffer [0-15s]        PROCESS [3-9s] ✅
         └─ Has 3s backward context (0-3s)
         └─ Has 6s forward context (9-15s)

t=15s:   Buffer [3-18s]        PROCESS [6-12s] ✅
         └─ Has 3s backward context (3-6s)
         └─ Has 6s forward context (12-18s)

t=18s:   Buffer [6-21s]        PROCESS [9-15s] ✅
         └─ Has 3s backward context (6-9s)
         └─ Has 6s forward context (15-21s)
```

**Key parameters:**
- **Capture step:** 3s (advance by 3s each iteration)
- **Process window:** 6s (the audio we transcribe)
- **Backward context:** 3s
- **Forward context:** 6s
- **Total buffer:** 15s (3s back + 6s process + 6s forward)
- **Latency:** 12s (must wait for 12s to have forward context)

### Comparison

| Approach | Backward Context | Forward Context | Latency | Boundary Quality |
|----------|------------------|-----------------|---------|------------------|
| **Current** (hold-and-emit) | 3s | 0s | 10s | ❌ Poor at boundaries |
| **Proposed** (delay for symmetry) | 3s | 6s | 12s | ✅ Good everywhere |
| whisper-cli (batch) | Full | Full | N/A | ✅ Perfect |

**Tradeoff:** +2s latency for much better boundary quality.

## Implementation Strategy

### 1. Circular Buffer

```cpp
class CircularAudioBuffer {
private:
    std::vector<int16_t> buffer;
    size_t write_pos = 0;
    size_t read_pos = 0;
    size_t capacity;
    
public:
    CircularAudioBuffer(size_t duration_s, int sample_rate) {
        capacity = duration_s * sample_rate;
        buffer.resize(capacity);
    }
    
    // Add new audio (from microphone/file)
    void push(const int16_t* data, size_t samples) {
        for (size_t i = 0; i < samples; i++) {
            buffer[write_pos] = data[i];
            write_pos = (write_pos + 1) % capacity;
        }
    }
    
    // Extract audio for processing (non-destructive read)
    std::vector<int16_t> get_window(size_t start_offset_samples, size_t length_samples) {
        std::vector<int16_t> result(length_samples);
        size_t pos = (write_pos - start_offset_samples + capacity) % capacity;
        
        for (size_t i = 0; i < length_samples; i++) {
            result[i] = buffer[pos];
            pos = (pos + 1) % capacity;
        }
        
        return result;
    }
    
    size_t samples_available() const {
        return (write_pos - read_pos + capacity) % capacity;
    }
};
```

### 2. Processing Loop

```cpp
// Parameters
const int SAMPLE_RATE = 16000;
const size_t CAPTURE_STEP_S = 3;        // Advance by 3s
const size_t PROCESS_WINDOW_S = 6;      // Transcribe 6s
const size_t BACKWARD_CONTEXT_S = 3;    // Keep 3s before
const size_t FORWARD_CONTEXT_S = 6;     // Keep 6s after
const size_t TOTAL_BUFFER_S = BACKWARD_CONTEXT_S + PROCESS_WINDOW_S + FORWARD_CONTEXT_S;  // 15s

CircularAudioBuffer audio_buffer(TOTAL_BUFFER_S, SAMPLE_RATE);

size_t samples_per_step = CAPTURE_STEP_S * SAMPLE_RATE;
size_t samples_to_process = PROCESS_WINDOW_S * SAMPLE_RATE;
size_t backward_samples = BACKWARD_CONTEXT_S * SAMPLE_RATE;
size_t forward_samples = FORWARD_CONTEXT_S * SAMPLE_RATE;
size_t total_window_samples = (BACKWARD_CONTEXT_S + PROCESS_WINDOW_S + FORWARD_CONTEXT_S) * SAMPLE_RATE;

int64_t processed_up_to_ms = 0;

while (true) {
    // 1. Capture new audio (3s step)
    audio::AudioQueue::Chunk chunk;
    if (!audioQueue.pop(chunk)) break;
    
    auto resampled = resample_to_16k(chunk.samples, chunk.sample_rate);
    audio_buffer.push(resampled.data(), resampled.size());
    
    // 2. Check if we have enough audio to process
    if (audio_buffer.samples_available() < total_window_samples) {
        continue;  // Wait for more audio (building forward context)
    }
    
    // 3. Extract window: [backward context | process window | forward context]
    auto window = audio_buffer.get_window(
        backward_samples + samples_to_process + forward_samples,  // Start from this far back
        total_window_samples                                       // Total length
    );
    
    // 4. Transcribe the FULL window (15s) for maximum context
    auto whisper_segments = whisper.transcribe_chunk_segments(window.data(), window.size());
    
    // 5. ONLY EMIT segments that fall within the PROCESS WINDOW (middle 6s)
    int64_t process_window_start_ms = processed_up_to_ms;
    int64_t process_window_end_ms = processed_up_to_ms + (PROCESS_WINDOW_S * 1000);
    
    for (const auto& wseg : whisper_segments) {
        // Adjust timestamps (segment is relative to buffer start)
        int64_t seg_start_ms = processed_up_to_ms - (BACKWARD_CONTEXT_S * 1000) + wseg.t0_ms;
        int64_t seg_end_ms = processed_up_to_ms - (BACKWARD_CONTEXT_S * 1000) + wseg.t1_ms;
        
        // Only emit if segment is within the process window
        if (seg_end_ms <= process_window_start_ms) continue;  // Too early (in backward context)
        if (seg_start_ms >= process_window_end_ms) continue;  // Too late (in forward context)
        
        // Emit this segment
        EmittedSegment seg{wseg.text, seg_start_ms, seg_end_ms, compute_speaker(...)};
        all_segments.push_back(seg);
    }
    
    // 6. Advance the process position
    processed_up_to_ms += (CAPTURE_STEP_S * 1000);
}
```

### 3. Segment Filtering

**Key insight:** We transcribe 15s (for maximum context), but only EMIT segments from the middle 6s (the "process window").

```
Transcribe:  [---|---------|---]
              ↑        ↑        ↑
           backward  process  forward
             3s        6s       6s

Emit only:       [------]
                 process window
```

**Why this works:**
- Segments in the process window (6s) have FULL context (3s before + 6s after)
- Segments in backward/forward zones are DISCARDED (will be emitted in other windows)
- No re-transcription problem (each 3s chunk processed exactly once when it's in the process window)

## Advantages

✅ **Symmetric context:** Every segment has both backward and forward context  
✅ **Better boundary quality:** Solves the "he made a following point" vs "So we've not had a mess" problem  
✅ **No re-transcription:** Each audio chunk transcribed once (when it enters process window)  
✅ **Circular buffer:** Efficient memory reuse (constant 15s buffer)  
✅ **Natural segmentation:** Whisper sees full context, segments naturally  

## Disadvantages

⚠️ **Increased latency:** 12s instead of 10s (need to wait for forward context)  
⚠️ **More complex:** Circular buffer + careful window management  
⚠️ **Edge cases:** First/last segments have less context (unavoidable)  

## Comparison with Hold-and-Emit

| Aspect | Hold-and-Emit (Current) | Circular Buffer (Proposed) |
|--------|-------------------------|---------------------------|
| **Latency** | 10s | 12s (+2s) |
| **Boundary quality** | Poor (no forward context) | Good (has forward context) |
| **Implementation** | Medium complexity | Higher complexity |
| **Memory** | 160K samples (10s) | 240K samples (15s) |
| **Re-transcription** | None ✅ | None ✅ |
| **Context** | Asymmetric (backward only) | Symmetric (both directions) |

## When to Use

**Use hold-and-emit (current) when:**
- Latency is critical (<10s required)
- Boundary quality degradation acceptable
- Simpler implementation preferred

**Use circular buffer (proposed) when:**
- Quality is more important than latency
- 12s latency acceptable (e.g., meeting transcription)
- Forward context significantly improves accuracy

## Testing Plan

**1. Implement circular buffer variant in separate branch**

**2. Test with problematic segment (10s mark):**
```bash
# Current approach
.\build\test_transcribe.exe test_data\Sean_Carroll_podcast.wav --limit-seconds 20

# New approach (with forward context)
.\build\test_transcribe_circular.exe test_data\Sean_Carroll_podcast.wav --limit-seconds 20
```

**3. Compare quality at boundaries:**
- Segment at 10s: "he made a following point" vs current output
- Segment at 17s: Check if improved
- Segment at 24s: Check if improved

**4. Measure latency:**
- Hold-and-emit: Should be ~10s
- Circular buffer: Should be ~12s
- Verify acceptable for use case

**5. Long audio test:**
- Test with 60s audio (multiple windows)
- Verify no quality degradation
- Verify no memory leaks (circular buffer reuse working)

## Open Questions

1. **Optimal window sizes:** Is 3s/6s/6s the best ratio? Should we test others?
   - Shorter process window (4s) = more frequent updates, but less per-segment context
   - Longer forward context (8s) = better quality, but more latency

2. **Speaker diarization:** How does this affect frame extraction?
   - Current: Extract frames from each 10s buffer
   - Proposed: Extract frames from 15s buffer, map to process window

3. **VAD integration:** Should we use VAD to adjust window boundaries?
   - whisper.cpp stream example has VAD option
   - Could stop at silence instead of fixed 3s steps

4. **Adaptive latency:** Could we reduce latency when no speech detected?
   - During silence: Process immediately (no need to wait for forward context)
   - During speech: Wait for forward context

5. **Comparison with whisper.cpp stream:** Should we just use their approach?
   - Their default: 10s length, 3s step, 200ms keep
   - But they process immediately (no forward context)
   - Could we modify to add forward context?

## Implementation Priority

**Current status:** Hold-and-emit is WORKING ✅

**Recommendation:** 
1. **Document this proposal** (this file) ✅
2. **Test current approach extensively** to quantify boundary quality issues
3. **If boundary quality is acceptable:** Keep current approach (simpler, lower latency)
4. **If boundary quality is problematic:** Implement circular buffer variant
5. **Make it configurable:** Let user choose latency vs quality tradeoff

**Next steps:**
1. Finish Phase 6 (TranscriptionController) with current approach
2. Add quality metrics (WER at boundaries vs middle of windows)
3. If users complain about boundary quality, implement circular buffer
4. Make it a runtime option: `--mode=immediate` vs `--mode=delayed`

## References

- whisper.cpp stream example: `third_party/whisper.cpp/examples/stream/stream.cpp`
- Current implementation: `src/console/transcribe_file.cpp`
- Hold-and-emit docs: `specs/STREAMING_STRATEGY.md`
