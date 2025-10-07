# Architecture Documentation

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
