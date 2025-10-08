# NEXT AGENT: Start Here! 👋

**Date:** 2025-10-08  
**Branch:** main (tracked to origin/main)  
**Last Commit:** f52e90b - Add comprehensive speaker identification analysis  
**Current Phase:** Phase 4 - Application API (Skeleton Complete)

---

## 🎯 YOUR MISSION

**Wire up the Application API to the transcription engine.**

The API skeleton is complete and tested. Now you need to connect it to:
1. WASAPI audio capture
2. Whisper transcription
3. Speaker diarization (frame voting)

**Estimated Time:** 12-18 hours

---

## 📍 WHERE IS THE API?

### Core Files (What You'll Work With)

**1. API Interface** (COMPLETE ✅)
```
src/app/transcription_controller.hpp (260 lines)
```
- Public API definition
- All data structures (TranscriptionChunk, SpeakerReclassification, etc.)
- Method signatures
- **DO NOT MODIFY** - This is the contract with GUI developers

**2. API Implementation** (SKELETON - NEEDS WIRING ⏳)
```
src/app/transcription_controller.cpp (680 lines)
```
- PIMPL implementation class: `TranscriptionControllerImpl`
- **KEY METHOD TO IMPLEMENT:** `processing_loop()` (line ~440)
- Thread management already working
- Event emission already working
- **YOUR JOB:** Fill in the processing loop with real transcription

**3. Test Application** (WORKS ✅)
```
apps/test_controller_api.cpp (320 lines)
```
- Tests device enum, start/stop, pause/resume
- Colored output for chunks and speakers
- **Use this to test your implementation!**

### Reference Files (Copy Logic From Here)

**4. Frame Voting Reference** (COMPLETE ✅)
```
apps/test_frame_voting.cpp (260 lines)
```
- **COPY THIS LOGIC** into `processing_loop()`
- Shows how to:
  - Get frames from ContinuousFrameAnalyzer
  - Get segments from Whisper
  - Vote on speaker per segment
  - Set speaker confidence

**5. Whisper Integration Reference** (COMPLETE ✅)
```
apps/test_word_timestamps.cpp (200 lines)
src/console/transcribe_file.cpp (300 lines)
```
- Shows how to call `transcribe_chunk_with_words()`
- How to get WhisperSegmentWithWords
- How to extract text and timestamps

**6. Audio Capture Reference** (COMPLETE ✅)
```
src/audio/windows_wasapi.cpp (existing)
src/console/transcribe_file.cpp (shows usage)
```
- WASAPI device enumeration
- Real-time audio capture
- Buffer management

---

## 🔧 WHAT TO IMPLEMENT

### Step 1: Wire Up Audio Capture (3-4 hours)

**File:** `src/app/transcription_controller.cpp`

**In `list_audio_devices()` method:**
```cpp
std::vector<AudioDevice> TranscriptionControllerImpl::list_audio_devices() {
    // TODO: Replace dummy data with real WASAPI enumeration
    
    // See: src/audio/windows_wasapi.cpp for device enumeration
    // Or: src/console/transcribe_file.cpp lines 50-80 for example
    
    std::vector<AudioDevice> devices;
    
    // Call WASAPI to enumerate devices
    // For each device:
    //   AudioDevice dev;
    //   dev.id = device_id_from_wasapi;
    //   dev.name = device_name_from_wasapi;
    //   dev.is_default = is_default_device;
    //   devices.push_back(dev);
    
    return devices;
}
```

**In `processing_loop()` method (line ~440):**
```cpp
void TranscriptionControllerImpl::processing_loop() {
    // TODO: Replace dummy loop with real audio capture
    
    // 1. Open selected audio device
    //    See: src/audio/windows_wasapi.cpp
    //    Create AudioCapture object with selected_device_id_
    
    // 2. Start capture thread
    //    Buffer audio in real-time
    
    // 3. Process audio in chunks (e.g., 500ms chunks)
    //    while (running_) {
    //        if (paused_) continue;
    //        
    //        audio_chunk = capture.get_next_chunk();
    //        process_audio(audio_chunk);  // See Step 2
    //    }
}
```

### Step 2: Wire Up Whisper Transcription (3-4 hours)

**File:** `src/app/transcription_controller.cpp`

**Add member variables to `TranscriptionControllerImpl`:**
```cpp
class TranscriptionControllerImpl {
private:
    // Add these:
    std::unique_ptr<WhisperBackend> whisper_;
    std::unique_ptr<ContinuousFrameAnalyzer> frame_analyzer_;
    std::unique_ptr<ONNXEmbedder> embedder_;
};
```

**In `start()` method:**
```cpp
bool TranscriptionControllerImpl::start(const TranscriptionConfig& config) {
    // ... existing code ...
    
    // TODO: Load models
    whisper_ = std::make_unique<WhisperBackend>();
    if (!whisper_->load_model(config.whisper_model)) {
        emit_error({/* model load failed */});
        return false;
    }
    
    embedder_ = std::make_unique<ONNXEmbedder>();
    if (!embedder_->load_model(config.speaker_model)) {
        emit_error({/* model load failed */});
        return false;
    }
    
    frame_analyzer_ = std::make_unique<ContinuousFrameAnalyzer>(
        embedder_.get(), 
        250,   // hop_ms
        1000   // window_ms
    );
    
    // ... rest of existing code ...
}
```

**Create helper method:**
```cpp
void TranscriptionControllerImpl::process_audio_chunk(
    const int16_t* audio, 
    size_t samples,
    int64_t timestamp_ms) {
    
    // 1. Feed to frame analyzer (for diarization)
    frame_analyzer_->add_audio(audio, samples);
    
    // 2. Feed to Whisper (for transcription)
    auto segments = whisper_->transcribe_chunk_with_words(audio, samples);
    
    // 3. For each segment, do speaker assignment
    for (const auto& seg : segments) {
        process_segment(seg, timestamp_ms);
    }
}
```

### Step 3: Implement Frame Voting (2-3 hours)

**File:** `src/app/transcription_controller.cpp`

**Copy logic from `apps/test_frame_voting.cpp` (lines 120-180):**

```cpp
void TranscriptionControllerImpl::process_segment(
    const WhisperSegmentWithWords& seg,
    int64_t base_timestamp_ms) {
    
    // Get all frames overlapping this segment
    auto all_frames = frame_analyzer_->get_frames();
    std::vector<Frame> seg_frames;
    
    for (const auto& frame : all_frames) {
        int64_t frame_t0 = base_timestamp_ms + frame.timestamp_ms;
        int64_t frame_t1 = frame_t0 + 250;  // 250ms per frame
        int64_t seg_t0 = base_timestamp_ms + seg.t0_ms;
        int64_t seg_t1 = base_timestamp_ms + seg.t1_ms;
        
        // Check overlap
        if (frame_t1 > seg_t0 && frame_t0 < seg_t1) {
            seg_frames.push_back(frame);
        }
    }
    
    if (seg_frames.empty()) {
        // No frames - emit with UNKNOWN_SPEAKER
        emit_chunk_for_segment(seg, UNKNOWN_SPEAKER, 0.0f);
        return;
    }
    
    // Initialize speakers if needed
    if (speaker_count_ == 0) {
        init_speaker_from_frames(0, seg_frames);
        emit_chunk_for_segment(seg, 0, 1.0f);
        return;
    }
    
    if (speaker_count_ == 1 && seg_frames.size() >= 3) {
        // Check if this is a new speaker
        float sim_to_s0 = compute_similarity(seg_frames, speaker_embeddings_[0]);
        if (sim_to_s0 < 0.85f) {
            init_speaker_from_frames(1, seg_frames);
            emit_chunk_for_segment(seg, 1, 1.0f);
            return;
        }
    }
    
    // Frame voting (copy from test_frame_voting.cpp lines 160-180)
    std::vector<int> votes(speaker_count_, 0);
    
    for (const auto& frame : seg_frames) {
        int best_speaker = 0;
        float best_sim = -1.0f;
        
        for (int s = 0; s < speaker_count_; ++s) {
            float sim = cosine_similarity(frame.embedding, speaker_embeddings_[s]);
            if (sim > best_sim) {
                best_sim = sim;
                best_speaker = s;
            }
        }
        
        votes[best_speaker]++;
    }
    
    // Find winner
    int winner = std::distance(votes.begin(), 
                               std::max_element(votes.begin(), votes.end()));
    float confidence = (float)votes[winner] / seg_frames.size();
    
    emit_chunk_for_segment(seg, winner, confidence);
}
```

### Step 4: Emit Chunks (1-2 hours)

**File:** `src/app/transcription_controller.cpp`

```cpp
void TranscriptionControllerImpl::emit_chunk_for_segment(
    const WhisperSegmentWithWords& seg,
    int speaker_id,
    float confidence) {
    
    TranscriptionChunk chunk;
    chunk.id = next_chunk_id_++;
    chunk.text = seg.text;
    chunk.speaker_id = speaker_id;
    chunk.timestamp_ms = seg.t0_ms;
    chunk.duration_ms = seg.t1_ms - seg.t0_ms;
    chunk.speaker_confidence = confidence;
    chunk.is_finalized = false;  // Can be reclassified later
    
    // Copy word-level details
    for (const auto& word : seg.words) {
        TranscriptionChunk::Word w;
        w.text = word.word;
        w.t0_ms = word.t0_ms;
        w.t1_ms = word.t1_ms;
        w.probability = word.probability;
        chunk.words.push_back(w);
    }
    
    emit_chunk(chunk);  // Already implemented!
}
```

### Step 5: Add Reclassification Logic (2-3 hours)

**File:** `src/app/transcription_controller.cpp`

**Call from `process_segment()`:**
```cpp
void TranscriptionControllerImpl::process_segment(...) {
    // ... existing voting logic ...
    
    emit_chunk_for_segment(seg, winner, confidence);
    
    // Check for reclassification opportunities
    check_for_reclassification();
}
```

**Implement:**
```cpp
void TranscriptionControllerImpl::check_for_reclassification() {
    std::lock_guard<std::mutex> lock(chunks_mutex_);
    
    if (chunks_.size() < 3) return;  // Need at least 3 chunks
    
    // Get recent unfinalized chunks (last 5 seconds)
    auto now_ms = get_elapsed_ms();
    std::vector<TranscriptionChunk*> recent;
    
    for (auto it = chunks_.rbegin(); it != chunks_.rend(); ++it) {
        if (it->is_finalized) continue;
        if (now_ms - it->timestamp_ms > 5000) break;
        recent.push_back(&(*it));
    }
    
    std::reverse(recent.begin(), recent.end());
    
    // Detect isolated chunks: S0 S0 [S1] S0 S0 → reclassify [S1] to S0
    for (size_t i = 1; i + 1 < recent.size(); ++i) {
        if (recent[i]->speaker_id != recent[i-1]->speaker_id &&
            recent[i]->speaker_id != recent[i+1]->speaker_id &&
            recent[i-1]->speaker_id == recent[i+1]->speaker_id) {
            
            // Isolated chunk detected!
            int old_speaker = recent[i]->speaker_id;
            int new_speaker = recent[i-1]->speaker_id;
            
            recent[i]->speaker_id = new_speaker;
            
            SpeakerReclassification recl;
            recl.chunk_ids = {recent[i]->id};
            recl.old_speaker_id = old_speaker;
            recl.new_speaker_id = new_speaker;
            recl.reason = "isolated_chunk";
            
            emit_reclassification(recl);  // Already implemented!
            break;  // Only one reclassification per check
        }
    }
}
```

### Step 6: Real-Time Timing (1-2 hours)

**File:** `src/app/transcription_controller.cpp`

**In `start()` method:**
```cpp
bool TranscriptionControllerImpl::start(...) {
    // ... existing code ...
    
    session_start_time_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    
    // ... rest of code ...
}
```

**In `get_elapsed_ms()` method:**
```cpp
int64_t TranscriptionControllerImpl::get_elapsed_ms() const {
    if (session_start_time_ms_ == 0) return 0;
    
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    
    return now - session_start_time_ms_;
}
```

---

## 🧪 TESTING YOUR IMPLEMENTATION

### Test 1: Basic Functionality

```powershell
# Build
cmake --preset tests-only-release
cmake --build --preset build-tests-only-release --target test_controller_api

# Run test (will use your microphone!)
.\build\tests-only-release\test_controller_api.exe
```

**Expected output:**
```
TEST 1: Audio Device Enumeration
Found N audio device(s):
  0. Microphone (Realtek...) [DEFAULT]
  1. Line In (...)
✓ Device selected

TEST 3: Start Transcription
✓ Transcription started!

[S0] Hello, this is a test.
[S1] Yes, I can hear you.
[S0] Great!

>>> RECLASSIFIED 1 chunk(s): S0 → S1 (isolated_chunk)

✓ All tests completed
```

### Test 2: Frame Voting Validation

```powershell
# Compare with reference implementation
.\build\tests-only-release\test_frame_voting.exe output\whisper_input_16k.wav 0.85
```

**Your implementation should produce similar speaker assignments!**

### Test 3: Real-World Audio

Record a conversation and test:
```powershell
# Record 30 seconds
# Save as output\test_conversation.wav

# Test with controller API
.\build\tests-only-release\test_controller_api.exe
# (speak into microphone for 30 seconds)
```

---

## 📚 REFERENCE DOCUMENTATION

### Design Documents (READ THESE FIRST!)

1. **`specs/application_api_design.md`** (390 lines)
   - Complete API specification
   - Usage examples
   - Thread safety considerations
   - Testing plan

2. **`specs/phase4_application_api_summary.md`** (419 lines)
   - What's implemented (skeleton)
   - What needs implementation (this document!)
   - Timeline and estimates

3. **`specs/speaker_identification_analysis.md`** (350 lines)
   - Why embeddings are strong (NOT weak!)
   - Expected accuracy by scenario
   - Path to better accuracy

### Phase 3 Technical Reports

4. **`specs/phase3_report.md`** (390 lines)
   - Frame voting approach explained
   - All 6 approaches tested
   - Why frame voting is best

5. **`specs/continuous_architecture_findings.md`**
   - Architecture decisions
   - Design patterns used

---

## ⚠️ IMPORTANT NOTES

### Thread Safety

- `processing_loop()` runs in separate thread
- Use `state_mutex_` when accessing `config_`, `selected_device_id_`
- Use `chunks_mutex_` when accessing `chunks_`
- Use `callbacks_mutex_` when accessing callback lists
- `emit_chunk()`, `emit_reclassification()`, etc. already thread-safe

### Memory Management

- Models loaded in `start()`, destroyed in `stop()`
- Don't hold pointers to audio buffers across calls
- Use `std::unique_ptr` for owned resources
- PIMPL pattern keeps implementation details hidden

### Performance Requirements

- Real-time factor < 1.0x (faster than realtime)
- Chunk emission latency < 500ms
- CPU usage < 50% on modern multi-core
- Memory usage < 500 MB including models

### Error Handling

```cpp
// Use emit_error() for problems
TranscriptionError error;
error.severity = TranscriptionError::Severity::ERROR;
error.message = "Failed to load Whisper model";
error.details = detailed_error_message;
error.timestamp_ms = get_elapsed_ms();
emit_error(error);
```

---

## 🎯 SUCCESS CRITERIA

Your implementation is COMPLETE when:

✅ `test_controller_api.exe` runs without errors  
✅ Real audio transcribed from microphone  
✅ Speaker labels [S0]/[S1] displayed correctly  
✅ At least one reclassification event triggered  
✅ No memory leaks (use valgrind or ASAN)  
✅ Real-time factor < 1.0x  
✅ Chunk emission latency < 500ms

Then you can commit with:
```bash
git add -A
git commit -m "Phase 4 COMPLETE: Wired up Application API to transcription engine

- Integrated WASAPI audio capture
- Connected Whisper transcription
- Implemented frame voting for speaker assignment
- Added reclassification logic (isolated chunks)
- Real-time timing calculations
- All tests pass with real audio!"

git push origin main
```

---

## 🆘 TROUBLESHOOTING

### Build Errors

```powershell
# Clean rebuild
cmake --preset tests-only-release
cmake --build --preset build-tests-only-release --target test_controller_api --clean-first
```

### Linker Errors

Make sure `app_controller` links to all dependencies:
```cmake
target_link_libraries(app_controller PUBLIC 
    audio_windows 
    asr_whisper 
    diarization
)
```

### Runtime Errors

- Check models are in `models/` directory
- Check ONNX Runtime DLLs copied to output
- Check OpenBLAS DLL if using BLAS

### Low Accuracy

- Check threshold (0.35 for CAMPlus, 0.50 for WeSpeaker)
- Check frame overlap detection
- Check speaker initialization
- Compare with `test_frame_voting.exe` output

---

## 📞 NEED HELP?

**Check these files:**
- Implementation reference: `apps/test_frame_voting.cpp`
- Audio capture: `src/audio/windows_wasapi.cpp`
- Whisper usage: `apps/test_word_timestamps.cpp`
- Full example: `src/console/transcribe_file.cpp`

**Common patterns:**
- Load models: See `transcribe_file.cpp` lines 100-150
- Audio capture: See `windows_wasapi.cpp` lines 50-200
- Frame voting: See `test_frame_voting.cpp` lines 120-180

---

**Good luck! The architecture is solid, just needs wiring. You got this! 💪**

**Estimated completion: 12-18 hours of focused work.**
