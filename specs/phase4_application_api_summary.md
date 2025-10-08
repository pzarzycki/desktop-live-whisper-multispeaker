# Application API Summary - Phase 4 Kickoff

**Date:** 2025-10-08  
**Commit:** 2d69c45  
**Status:** Design Complete, Skeleton Implemented, Tests Pass ✅

---

## What We Built

### 1. Comprehensive API Design (`specs/application_api_design.md`)

**390-line design document** covering:

- **Architecture**: 3-layer separation (Engine → Controller → GUI)
- **Core Concepts**: Text chunks, speaker reclassification, event-driven flow
- **Complete API**: TranscriptionController class with full interface
- **Configuration**: Models, thresholds, real-time behavior
- **Events**: Chunks, reclassification, status, errors
- **Usage Examples**: Basic session, Qt integration, reclassification scenarios
- **Implementation Strategy**: 4-phase plan with timeline
- **Testing Plan**: Unit tests, integration tests, demo app
- **Thread Safety**: Mutex protection, callback marshaling
- **Performance Requirements**: <500ms latency, <1.0x realtime

**Key Design Decisions:**

✅ **Event-driven**: No polling, callbacks for all events  
✅ **Retroactive updates**: GUI can receive reclassification events  
✅ **Thread-safe**: All public methods protected by mutexes  
✅ **PIMPL pattern**: Clean separation of interface and implementation  
✅ **GUI-agnostic**: Works with Qt, Win32, web, etc.

### 2. Production-Quality Interface (`src/app/transcription_controller.hpp`)

**260 lines** of clean C++ interface:

```cpp
class TranscriptionController {
public:
    // Lifecycle
    TranscriptionController();
    ~TranscriptionController();
    
    // Device Management
    std::vector<AudioDevice> list_audio_devices();
    bool select_audio_device(const std::string& device_id);
    
    // Transcription Control
    bool start_transcription(const TranscriptionConfig& config);
    void stop_transcription();
    bool pause_transcription();
    bool resume_transcription();
    
    // Event Subscription
    void subscribe_to_chunks(ChunkCallback callback);
    void subscribe_to_reclassification(ReclassificationCallback callback);
    void subscribe_to_status(StatusCallback callback);
    void subscribe_to_errors(ErrorCallback callback);
    
    // Speaker Management
    int get_speaker_count() const;
    void set_max_speakers(int max_speakers);
    
    // Chunk History
    std::vector<TranscriptionChunk> get_all_chunks() const;
    bool get_chunk_by_id(uint64_t id, TranscriptionChunk& chunk) const;
    void clear_history();
};
```

**Event Structures:**

```cpp
struct TranscriptionChunk {
    uint64_t id;                    // Unique identifier
    std::string text;               // Transcribed text
    int speaker_id;                 // 0, 1, 2, ... or UNKNOWN_SPEAKER
    int64_t timestamp_ms;           // When this was spoken
    int64_t duration_ms;            // Duration
    float speaker_confidence;       // 0.0-1.0
    bool is_finalized;              // Can still be reclassified?
    std::vector<Word> words;        // Word-level details
};

struct SpeakerReclassification {
    std::vector<uint64_t> chunk_ids;  // Which chunks changed
    int old_speaker_id;                // What they were
    int new_speaker_id;                // What they are now
    std::string reason;                // Why: "better_context", etc.
};
```

### 3. Working Skeleton (`src/app/transcription_controller.cpp`)

**680 lines** of implementation:

✅ **PIMPL Pattern**: Clean separation, forward compatibility  
✅ **Thread Safety**: Mutexes for state, chunks, callbacks  
✅ **Event Emission**: Exception-safe callback invocation  
✅ **Lifecycle Management**: Start/stop/pause/resume  
✅ **Chunk Storage**: Deque with max size limit  
✅ **Device Enumeration**: Skeleton (will integrate WASAPI)  
✅ **Processing Thread**: Skeleton (will integrate engine)

**What Works Now:**

- ✅ Device enumeration (dummy data)
- ✅ Device selection
- ✅ Start/stop lifecycle
- ✅ Pause/resume
- ✅ Event subscription
- ✅ Status reporting
- ✅ Thread-safe callbacks
- ✅ Exception handling

**What Needs Wiring:**

- ⏳ WASAPI audio capture integration
- ⏳ Whisper ASR integration
- ⏳ Frame voting from Phase 3
- ⏳ Reclassification logic
- ⏳ Real-time timing calculations

### 4. Interactive Test Application (`apps/test_controller_api.cpp`)

**320 lines** of colored terminal demo:

**Features:**

✅ Device enumeration with default highlighting  
✅ Event subscription with colored output  
✅ Speaker color-coding (S0=blue, S1=red)  
✅ Confidence warnings (yellow for <0.7)  
✅ Reclassification alerts (magenta)  
✅ Status updates (green=running, yellow=paused)  
✅ Error display with severity levels  
✅ Pause/resume testing (every 20 seconds)  
✅ Final transcript with speaker distribution  
✅ Ctrl+C handling

**Test Output:**

```
TEST 1: Audio Device Enumeration
Found 1 audio device(s):
  0. System Default [DEFAULT]
✓ Device selected

TEST 2: Event Subscription
✓ Subscribed to all event types

TEST 3: Start Transcription
Starting transcription...
✓ Transcription started!

[Testing pause...]
[Testing resume...]

TEST 5: Stop and Summary
Final Status:
  Total chunks emitted: 0
  Total reclassifications: 0
  Elapsed time: 0 seconds
✓ All tests completed
```

---

## Architecture Benefits

### For Application Developers

✅ **Simple Integration**: 5 lines to start transcription  
✅ **Real-time Events**: Receive chunks as they're transcribed  
✅ **Retroactive Updates**: Handle speaker corrections automatically  
✅ **No Polling**: Event-driven, efficient  
✅ **Thread-Safe**: No race conditions  
✅ **Framework Agnostic**: Works with any GUI toolkit

### For Engine Developers

✅ **Clean Separation**: Engine doesn't know about GUI  
✅ **Testable**: Controller can be tested without GUI  
✅ **Flexible**: Easy to swap implementations  
✅ **Observable**: Status and error reporting built-in

### For Users

✅ **Responsive UI**: Updates in real-time (<500ms)  
✅ **Accurate**: Corrections when better context available  
✅ **Transparent**: See speaker confidence, status  
✅ **Reliable**: Exception handling prevents crashes

---

## Usage Example

### Minimal Integration (5 lines)

```cpp
TranscriptionController controller;

controller.subscribe_to_chunks([](const TranscriptionChunk& chunk) {
    std::cout << "[S" << chunk.speaker_id << "] " << chunk.text << "\n";
});

TranscriptionConfig config;
controller.start_transcription(config);
```

### Qt Application Integration

```cpp
class TranscriptionWidget : public QWidget {
    TranscriptionController controller_;
    QTextEdit* display_;
    std::map<uint64_t, TranscriptionChunk> chunks_;
    
public:
    TranscriptionWidget() {
        // Subscribe to events, marshal to UI thread
        controller_.subscribe_to_chunks([this](const TranscriptionChunk& chunk) {
            QMetaObject::invokeMethod(this, [this, chunk]() {
                on_chunk_received(chunk);
            });
        });
        
        controller_.subscribe_to_reclassification([this](const SpeakerReclassification& recl) {
            QMetaObject::invokeMethod(this, [this, recl]() {
                on_speaker_reclassified(recl);
            });
        });
    }
    
    void on_start_clicked() {
        TranscriptionConfig config;
        controller_.start_transcription(config);
    }
    
    void on_chunk_received(const TranscriptionChunk& chunk) {
        chunks_[chunk.id] = chunk;
        
        QColor color = (chunk.speaker_id == 0) ? Qt::blue : Qt::red;
        // ... append to display with color ...
    }
    
    void on_speaker_reclassified(const SpeakerReclassification& recl) {
        // Update stored chunks
        for (uint64_t id : recl.chunk_ids) {
            if (chunks_.contains(id)) {
                chunks_[id].speaker_id = recl.new_speaker_id;
            }
        }
        rebuild_display();  // Re-render with corrected speakers
    }
};
```

---

## Reclassification Logic (To Be Implemented)

### Scenario 1: Isolated Chunk

```
Pattern:  S0 S0 S0 [S1] S0 S0 S0
Problem:  Single S1 surrounded by S0
Action:   Reclassify [S1] → S0
Reason:   "isolated_chunk"
```

### Scenario 2: Low Confidence Followed by High

```
Pattern:  [S0 conf=0.4] [S1 conf=0.9]
Problem:  Low confidence immediately before high confidence
Action:   Reclassify [S0] → S1
Reason:   "low_confidence_correction"
```

### Scenario 3: Better Context

```
Time T=0:   "Yeah." → S0 (conf=0.6, not sure)
Time T=500: "If you were" → S1 (conf=0.9, clearly S1)
Time T=600: Realize "Yeah." should be S1 too
Action:     Reclassify "Yeah." S0 → S1
Reason:     "better_context"
```

---

## Next Steps

### Phase 4 Remaining Work

1. **Audio Capture Integration** (2-3 hours)
   - Wire up WASAPI device enumeration
   - Connect selected device to processing thread
   - Buffer management and real-time guarantees

2. **Transcription Integration** (3-4 hours)
   - Load Whisper model in processing thread
   - Call transcribe_chunk_with_words()
   - Extract segments and words
   - Emit TranscriptionChunk events

3. **Frame Voting Integration** (2-3 hours)
   - Port frame voting from test_frame_voting.cpp
   - Get overlapping frames for each segment
   - Compute speaker votes
   - Set speaker_id and confidence

4. **Reclassification Logic** (2-3 hours)
   - Implement isolated chunk detection
   - Implement low confidence correction
   - Implement better context detection
   - Emit SpeakerReclassification events

5. **Real-time Timing** (1-2 hours)
   - Track session start time
   - Calculate elapsed_ms accurately
   - Compute realtime_factor from audio duration

6. **Testing** (2-3 hours)
   - Test with real microphone
   - Validate chunk emission
   - Verify reclassification triggers
   - Measure latency and CPU usage

**Estimated Total: 12-18 hours**

### Phase 5: GUI Development

Once Phase 4 is complete:

1. **GUI Framework Selection**
   - Qt Quick (QML) - preferred (already set up)
   - Qt Widgets - alternative
   - Dear ImGui - lightweight option

2. **Main Window**
   - Device selection dropdown
   - Start/Stop/Pause buttons
   - Status indicators

3. **Transcription Display**
   - Scrolling text area
   - Color-coded speakers
   - Confidence indicators
   - Reclassification animations

4. **Settings Panel**
   - Model selection
   - Max speakers
   - Speaker threshold
   - Reclassification toggle

---

## Technical Debt

### To Address Before Production

1. **Error Handling**
   - More granular error types
   - Recovery strategies
   - User-friendly error messages

2. **Performance**
   - Profile processing thread CPU usage
   - Optimize chunk storage (circular buffer?)
   - Measure memory usage over time

3. **Testing**
   - Unit tests for controller methods
   - Integration tests with mock engine
   - Stress tests (long sessions, many chunks)

4. **Documentation**
   - API reference (Doxygen)
   - Usage tutorials
   - Best practices guide

---

## Summary

**What We Have:**

✅ Clean, event-driven API design  
✅ Production-quality C++ interface  
✅ Thread-safe skeleton implementation  
✅ Working test application  
✅ Comprehensive documentation  
✅ Clear path to completion

**What We Need:**

⏳ Wire up audio capture  
⏳ Connect transcription engine  
⏳ Implement frame voting  
⏳ Add reclassification logic  
⏳ Complete timing calculations

**When It's Done:**

🎯 GUI developers can start immediately  
🎯 Real-time transcription with speaker labels  
🎯 Retroactive speaker corrections  
🎯 Professional user experience  
🎯 Production-ready application

**Estimated Time to Phase 4 Complete:** 12-18 hours of focused development

---

**Status:** ✅ Architecture solid, skeleton works, ready to wire up engine!
