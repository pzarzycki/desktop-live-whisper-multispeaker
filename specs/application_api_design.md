# Application API Design - Transcription Controller

**Date:** 2025-10-08  
**Purpose:** Define the interface between transcription engine and GUI application layer  
**Status:** Design Document

---

## Overview

The Application API provides a clean, event-driven interface for controlling real-time transcription with speaker diarization. It handles:
- Device selection and audio capture control
- Real-time transcription streaming
- Speaker identification and reclassification
- Incremental text updates with speaker changes

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    GUI Application                      │
│  - Display transcription with speaker colors            │
│  - Handle user controls (start/stop/settings)           │
│  - Update UI when speaker reclassified                  │
└────────────────────┬────────────────────────────────────┘
                     │
                     │ Uses
                     ▼
┌─────────────────────────────────────────────────────────┐
│          TranscriptionController (This API)             │
│  - Manages transcription lifecycle                      │
│  - Emits events: text chunks, speaker changes           │
│  - Handles speaker reclassification                     │
└────────────────────┬────────────────────────────────────┘
                     │
                     │ Controls
                     ▼
┌─────────────────────────────────────────────────────────┐
│              Transcription Engine                       │
│  - Audio capture (WASAPI)                               │
│  - Whisper ASR (word-level timestamps)                  │
│  - Speaker diarization (frame voting)                   │
└─────────────────────────────────────────────────────────┘
```

---

## Core Concepts

### 1. Text Chunk
A piece of transcribed text with metadata:
- **Unique ID:** Allows retroactive updates
- **Text:** Word(s) or phrase from Whisper
- **Speaker ID:** 0, 1, 2, ... (or UNKNOWN)
- **Timestamp:** When this was spoken
- **Confidence:** How certain the speaker assignment is
- **Finalized:** Whether this chunk can still be reclassified

### 2. Speaker Reclassification
When context reveals earlier chunks were misassigned:
- Controller emits `SpeakerReclassified` event
- Contains chunk IDs that changed speaker
- GUI updates display retroactively

### 3. Event-Driven Flow
```
User clicks Start
    → Controller starts audio capture
    → Engine processes audio in real-time
    → Engine emits TranscriptionChunk events
    → GUI displays chunks as they arrive
    → Engine may emit SpeakerReclassified
    → GUI updates previous chunks
```

---

## API Interface

### TranscriptionController Class

```cpp
class TranscriptionController {
public:
    // Lifecycle
    TranscriptionController();
    ~TranscriptionController();
    
    // Device Management
    struct AudioDevice {
        std::string id;
        std::string name;
        bool is_default;
    };
    std::vector<AudioDevice> list_audio_devices();
    bool select_audio_device(const std::string& device_id);
    
    // Transcription Control
    bool start_transcription(const TranscriptionConfig& config);
    void stop_transcription();
    void pause_transcription();
    void resume_transcription();
    
    bool is_running() const;
    TranscriptionStatus get_status() const;
    
    // Event Subscription
    void subscribe_to_chunks(ChunkCallback callback);
    void subscribe_to_reclassification(ReclassificationCallback callback);
    void subscribe_to_status(StatusCallback callback);
    void subscribe_to_errors(ErrorCallback callback);
    
    // Speaker Management
    int get_speaker_count() const;
    void set_max_speakers(int max_speakers);
    
    // Retroactive Access (for GUI to rebuild display)
    std::vector<TranscriptionChunk> get_all_chunks() const;
    void clear_history();
};
```

### Configuration

```cpp
struct TranscriptionConfig {
    // Model Selection
    std::string whisper_model = "tiny.en";  // tiny, base, small, medium, large
    std::string speaker_model = "campplus_voxceleb.onnx";
    
    // Processing Parameters
    int max_speakers = 2;
    float speaker_threshold = 0.35f;      // For CAMPlus
    int vad_silence_duration_ms = 1000;   // How long silence before finalizing
    
    // Real-time Behavior
    bool enable_partial_results = true;    // Send incomplete segments
    int chunk_duration_ms = 250;           // How often to emit chunks
    
    // Speaker Reclassification
    bool enable_reclassification = true;
    int reclassification_window_ms = 5000; // How far back to reconsider
};
```

### Events

```cpp
// Text chunk with speaker identification
struct TranscriptionChunk {
    uint64_t id;                    // Unique identifier for this chunk
    std::string text;               // The transcribed text
    int speaker_id;                 // 0, 1, 2, ... or UNKNOWN_SPEAKER (-1)
    int64_t timestamp_ms;           // When this was spoken (relative to start)
    int64_t duration_ms;            // How long this chunk spans
    float speaker_confidence;       // 0.0-1.0 confidence in speaker assignment
    bool is_finalized;              // If true, won't be reclassified
    
    // Word-level details (optional)
    struct Word {
        std::string text;
        int64_t t0_ms;
        int64_t t1_ms;
        float probability;
    };
    std::vector<Word> words;
};

// Speaker was reclassified for earlier chunks
struct SpeakerReclassification {
    std::vector<uint64_t> chunk_ids;  // Which chunks changed
    int old_speaker_id;                // What they were
    int new_speaker_id;                // What they are now
    std::string reason;                // Why: "better_context", "majority_vote", etc.
};

// Status updates
struct TranscriptionStatus {
    enum State {
        IDLE,
        STARTING,
        RUNNING,
        PAUSED,
        STOPPING,
        ERROR
    };
    
    State state;
    int64_t elapsed_ms;
    int chunks_emitted;
    int reclassifications_count;
    std::string current_device;
    
    // Performance metrics
    float realtime_factor;   // <1.0 means faster than realtime
    int audio_buffer_ms;     // How much audio buffered
};

// Error events
struct TranscriptionError {
    enum Severity {
        WARNING,   // Non-fatal, can continue
        ERROR,     // Fatal, transcription stopped
        CRITICAL   // System-level issue
    };
    
    Severity severity;
    std::string message;
    std::string details;
    int64_t timestamp_ms;
};

// Callbacks
using ChunkCallback = std::function<void(const TranscriptionChunk&)>;
using ReclassificationCallback = std::function<void(const SpeakerReclassification&)>;
using StatusCallback = std::function<void(const TranscriptionStatus&)>;
using ErrorCallback = std::function<void(const TranscriptionError&)>;

// Special constant
constexpr int UNKNOWN_SPEAKER = -1;
```

---

## Usage Examples

### Basic Transcription Session

```cpp
#include "app/transcription_controller.hpp"

// Create controller
TranscriptionController controller;

// List available devices
auto devices = controller.list_audio_devices();
for (const auto& dev : devices) {
    std::cout << (dev.is_default ? "[DEFAULT] " : "          ")
              << dev.name << " (" << dev.id << ")\n";
}

// Select device (or use default)
if (!devices.empty()) {
    controller.select_audio_device(devices[0].id);
}

// Subscribe to events
controller.subscribe_to_chunks([](const TranscriptionChunk& chunk) {
    std::cout << "[S" << chunk.speaker_id << "] " 
              << chunk.text 
              << " (confidence: " << chunk.speaker_confidence << ")\n";
});

controller.subscribe_to_reclassification([](const SpeakerReclassification& recl) {
    std::cout << "Reclassified " << recl.chunk_ids.size() << " chunks: "
              << "S" << recl.old_speaker_id << " → S" << recl.new_speaker_id
              << " (" << recl.reason << ")\n";
});

controller.subscribe_to_errors([](const TranscriptionError& error) {
    std::cerr << "ERROR: " << error.message << "\n";
});

// Configure and start
TranscriptionConfig config;
config.whisper_model = "base.en";
config.max_speakers = 2;
config.enable_reclassification = true;

if (controller.start_transcription(config)) {
    std::cout << "Transcription started!\n";
    
    // Let it run...
    std::this_thread::sleep_for(std::chrono::seconds(30));
    
    controller.stop_transcription();
    std::cout << "Transcription stopped.\n";
}
```

### GUI Integration (Qt Example)

```cpp
class TranscriptionWidget : public QWidget {
    Q_OBJECT
    
public:
    TranscriptionWidget(QWidget* parent = nullptr) 
        : QWidget(parent) {
        
        // Setup UI
        text_display_ = new QTextEdit(this);
        text_display_->setReadOnly(true);
        
        // Subscribe to controller events
        controller_.subscribe_to_chunks([this](const TranscriptionChunk& chunk) {
            // Queue for UI thread
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
    
private slots:
    void on_start_button_clicked() {
        TranscriptionConfig config;
        config.max_speakers = 2;
        
        if (controller_.start_transcription(config)) {
            start_button_->setEnabled(false);
            stop_button_->setEnabled(true);
        }
    }
    
    void on_stop_button_clicked() {
        controller_.stop_transcription();
        start_button_->setEnabled(true);
        stop_button_->setEnabled(false);
    }
    
    void on_chunk_received(const TranscriptionChunk& chunk) {
        // Store chunk for retroactive updates
        chunks_[chunk.id] = chunk;
        
        // Color by speaker
        QColor color = (chunk.speaker_id == 0) ? Qt::blue : Qt::red;
        
        // Append to display
        QTextCursor cursor = text_display_->textCursor();
        cursor.movePosition(QTextCursor::End);
        
        QTextCharFormat format;
        format.setForeground(color);
        cursor.setCharFormat(format);
        
        cursor.insertText("[S" + QString::number(chunk.speaker_id) + "] ");
        cursor.insertText(chunk.text + " ");
        
        text_display_->setTextCursor(cursor);
    }
    
    void on_speaker_reclassified(const SpeakerReclassification& recl) {
        // Update stored chunks
        for (uint64_t id : recl.chunk_ids) {
            if (chunks_.contains(id)) {
                chunks_[id].speaker_id = recl.new_speaker_id;
            }
        }
        
        // Rebuild entire display (or update specific chunks if performance matters)
        rebuild_display();
    }
    
    void rebuild_display() {
        text_display_->clear();
        
        // Re-render all chunks in order
        std::vector<TranscriptionChunk> sorted;
        for (const auto& [id, chunk] : chunks_) {
            sorted.push_back(chunk);
        }
        std::sort(sorted.begin(), sorted.end(), 
                  [](const auto& a, const auto& b) { return a.timestamp_ms < b.timestamp_ms; });
        
        for (const auto& chunk : sorted) {
            on_chunk_received(chunk);  // Reuse display logic
        }
    }
    
private:
    TranscriptionController controller_;
    QTextEdit* text_display_;
    QPushButton* start_button_;
    QPushButton* stop_button_;
    std::map<uint64_t, TranscriptionChunk> chunks_;  // For retroactive updates
};
```

### Reclassification Example

```cpp
// Scenario: "Yeah. If you were Aristotle..." initially assigned S0, then S1

// Time T=0: First chunk arrives
TranscriptionChunk chunk1;
chunk1.id = 1;
chunk1.text = "Yeah.";
chunk1.speaker_id = 0;  // Initial guess
chunk1.speaker_confidence = 0.6;  // Not very confident
chunk1.is_finalized = false;
// → GUI displays: [S0] Yeah.

// Time T=500ms: More context arrives
TranscriptionChunk chunk2;
chunk2.id = 2;
chunk2.text = "If you were";
chunk2.speaker_id = 1;  // Clearly S1
chunk2.speaker_confidence = 0.9;
chunk2.is_finalized = false;
// → GUI displays: [S0] Yeah. [S1] If you were

// Time T=600ms: Engine realizes chunk1 should be S1 too!
SpeakerReclassification recl;
recl.chunk_ids = {1};  // Reclassify chunk1
recl.old_speaker_id = 0;
recl.new_speaker_id = 1;
recl.reason = "better_context";
// → GUI updates: [S1] Yeah. [S1] If you were

// Time T=2000ms: Rest of sentence arrives
TranscriptionChunk chunk3;
chunk3.id = 3;
chunk3.text = "Aristotle, when Aristotle wrote his book on";
chunk3.speaker_id = 1;
chunk3.speaker_confidence = 0.7;  // Medium confidence
chunk3.is_finalized = true;  // Finalized after silence
// → GUI displays: [S1] Yeah. If you were Aristotle, when Aristotle wrote his book on
```

---

## Implementation Strategy

### Phase 1: Core Controller (Week 1)

**File:** `src/app/transcription_controller.hpp` + `.cpp`

Tasks:
1. ✅ Define all structs and interfaces (this document)
2. Implement `TranscriptionController` skeleton
3. Add audio device enumeration (reuse WASAPI code)
4. Implement start/stop lifecycle
5. Wire up callbacks (observer pattern)

### Phase 2: Event Emission (Week 1-2)

**Integration with existing engine:**

```cpp
// Pseudocode: Inside controller's audio processing thread

void TranscriptionController::process_audio_chunk(const int16_t* audio, size_t samples) {
    // 1. Feed to Whisper
    auto segments = whisper_.transcribe_chunk_with_words(audio, samples);
    
    // 2. Feed to diarization
    frame_analyzer_.add_audio(audio, samples);
    auto frames = frame_analyzer_.get_recent_frames();
    
    // 3. For each segment, do frame voting
    for (const auto& seg : segments) {
        auto seg_frames = get_overlapping_frames(seg, frames);
        int speaker = vote_speaker(seg_frames);
        float confidence = compute_confidence(seg_frames, speaker);
        
        // 4. Emit chunk event
        TranscriptionChunk chunk;
        chunk.id = next_chunk_id_++;
        chunk.text = seg.text;
        chunk.speaker_id = speaker;
        chunk.timestamp_ms = current_time_ms_;
        chunk.duration_ms = seg.t1_ms - seg.t0_ms;
        chunk.speaker_confidence = confidence;
        chunk.is_finalized = false;
        
        emit_chunk(chunk);
        pending_chunks_.push_back(chunk);
    }
    
    // 5. Check for reclassification opportunities
    check_reclassification(pending_chunks_);
}
```

### Phase 3: Reclassification Logic (Week 2)

**Strategy:**
```cpp
void TranscriptionController::check_reclassification(
    std::vector<TranscriptionChunk>& chunks) {
    
    // Only consider chunks from last 5 seconds
    auto now = current_time_ms_;
    auto recent = chunks | filter([now](const auto& c) { 
        return !c.is_finalized && (now - c.timestamp_ms) < 5000;
    });
    
    // Look for patterns:
    // 1. Isolated single chunk surrounded by different speaker
    for (size_t i = 1; i < recent.size() - 1; ++i) {
        if (recent[i].speaker_id != recent[i-1].speaker_id &&
            recent[i].speaker_id != recent[i+1].speaker_id &&
            recent[i-1].speaker_id == recent[i+1].speaker_id) {
            
            // Middle chunk is isolated - probably misclassified
            reclassify_chunk(recent[i], recent[i-1].speaker_id, "isolated_chunk");
        }
    }
    
    // 2. Very short turn (<2 chunks) followed by long turn
    // 3. Low confidence chunks followed by high confidence
    // etc.
}
```

### Phase 4: Testing (Week 2)

**Test Files:**
1. `apps/test_controller_api.cpp` - Unit tests for controller
2. `apps/test_reclassification.cpp` - Reclassification logic tests
3. `apps/demo_controller.cpp` - Interactive demo (console-based)

---

## Testing Plan

### Unit Tests

```cpp
// test_controller_api.cpp

TEST(TranscriptionController, DeviceEnumeration) {
    TranscriptionController controller;
    auto devices = controller.list_audio_devices();
    EXPECT_FALSE(devices.empty());
    
    // Should have exactly one default device
    int default_count = 0;
    for (const auto& dev : devices) {
        if (dev.is_default) default_count++;
    }
    EXPECT_EQ(default_count, 1);
}

TEST(TranscriptionController, StartStop) {
    TranscriptionController controller;
    
    EXPECT_FALSE(controller.is_running());
    
    TranscriptionConfig config;
    EXPECT_TRUE(controller.start_transcription(config));
    EXPECT_TRUE(controller.is_running());
    
    controller.stop_transcription();
    EXPECT_FALSE(controller.is_running());
}

TEST(TranscriptionController, ChunkEmission) {
    TranscriptionController controller;
    
    std::vector<TranscriptionChunk> received_chunks;
    controller.subscribe_to_chunks([&](const TranscriptionChunk& chunk) {
        received_chunks.push_back(chunk);
    });
    
    TranscriptionConfig config;
    controller.start_transcription(config);
    
    // Speak into mic or use test audio...
    std::this_thread::sleep_for(std::chrono::seconds(5));
    
    controller.stop_transcription();
    
    EXPECT_FALSE(received_chunks.empty());
    
    // Verify chunks are ordered by timestamp
    for (size_t i = 1; i < received_chunks.size(); ++i) {
        EXPECT_GE(received_chunks[i].timestamp_ms, 
                  received_chunks[i-1].timestamp_ms);
    }
}

TEST(TranscriptionController, Reclassification) {
    TranscriptionController controller;
    
    std::vector<SpeakerReclassification> recls;
    controller.subscribe_to_reclassification([&](const SpeakerReclassification& r) {
        recls.push_back(r);
    });
    
    TranscriptionConfig config;
    config.enable_reclassification = true;
    controller.start_transcription(config);
    
    // Use test audio with known speaker pattern...
    // (Could use the Sean Carroll clip with "Yeah. If you were Aristotle...")
    
    controller.stop_transcription();
    
    // Should have detected at least one reclassification
    EXPECT_FALSE(recls.empty());
}
```

### Integration Tests

```cpp
// test_reclassification.cpp

TEST(Reclassification, IsolatedChunk) {
    // Simulate: S0 S0 S0 S1 S0 S0 S0
    //                     ↑ This S1 should be reclassified to S0
    
    std::vector<TranscriptionChunk> chunks;
    // ... create test chunks ...
    
    auto recls = detect_reclassifications(chunks);
    
    ASSERT_EQ(recls.size(), 1);
    EXPECT_EQ(recls[0].old_speaker_id, 1);
    EXPECT_EQ(recls[0].new_speaker_id, 0);
}

TEST(Reclassification, LowConfidenceChunk) {
    // Chunk with confidence <0.5 followed by high confidence opposite speaker
    // → Reclassify low confidence chunk
    
    TranscriptionChunk chunk1;
    chunk1.speaker_id = 0;
    chunk1.speaker_confidence = 0.4;  // Low!
    
    TranscriptionChunk chunk2;
    chunk2.speaker_id = 1;
    chunk2.speaker_confidence = 0.9;  // High!
    
    auto recls = detect_reclassifications({chunk1, chunk2});
    
    EXPECT_EQ(recls.size(), 1);
    EXPECT_THAT(recls[0].chunk_ids, Contains(chunk1.id));
}
```

### Demo Application

```cpp
// demo_controller.cpp - Interactive console demo

int main() {
    TranscriptionController controller;
    
    std::cout << "=== Transcription Controller Demo ===\n\n";
    
    // Device selection
    auto devices = controller.list_audio_devices();
    std::cout << "Available audio devices:\n";
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << i << ". " << devices[i].name 
                  << (devices[i].is_default ? " [DEFAULT]" : "") << "\n";
    }
    std::cout << "Select device (0-" << devices.size()-1 << "): ";
    int choice;
    std::cin >> choice;
    controller.select_audio_device(devices[choice].id);
    
    // Event handlers
    controller.subscribe_to_chunks([](const TranscriptionChunk& chunk) {
        std::cout << "\n[S" << chunk.speaker_id << "] " << chunk.text 
                  << " (conf: " << std::fixed << std::setprecision(2) 
                  << chunk.speaker_confidence << ")";
        std::cout.flush();
    });
    
    controller.subscribe_to_reclassification([](const SpeakerReclassification& recl) {
        std::cout << "\n>>> RECLASSIFIED " << recl.chunk_ids.size() << " chunks: "
                  << "S" << recl.old_speaker_id << " → S" << recl.new_speaker_id
                  << " (" << recl.reason << ")\n";
    });
    
    controller.subscribe_to_status([](const TranscriptionStatus& status) {
        if (status.state == TranscriptionStatus::RUNNING) {
            std::cout << "\r[" << (status.elapsed_ms / 1000) << "s] "
                      << "Chunks: " << status.chunks_emitted << " "
                      << "Recls: " << status.reclassifications_count << "   ";
            std::cout.flush();
        }
    });
    
    controller.subscribe_to_errors([](const TranscriptionError& error) {
        std::cerr << "\nERROR: " << error.message << "\n";
    });
    
    // Start transcription
    TranscriptionConfig config;
    config.whisper_model = "tiny.en";
    config.max_speakers = 2;
    config.enable_reclassification = true;
    
    std::cout << "\nStarting transcription...\n";
    std::cout << "Press Ctrl+C to stop.\n\n";
    
    if (!controller.start_transcription(config)) {
        std::cerr << "Failed to start transcription!\n";
        return 1;
    }
    
    // Wait for Ctrl+C
    signal(SIGINT, [](int) {
        std::cout << "\n\nStopping...\n";
        exit(0);
    });
    
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}
```

---

## Thread Safety Considerations

### Controller Internal State
- **Audio thread:** Captures audio from device
- **Processing thread:** Runs Whisper + diarization
- **Callback thread:** Executes user callbacks

**Safety measures:**
```cpp
class TranscriptionController {
private:
    std::mutex state_mutex_;
    std::mutex chunks_mutex_;
    std::atomic<bool> is_running_{false};
    
    // Callback storage (protected by mutex)
    std::vector<ChunkCallback> chunk_callbacks_;
    std::mutex callbacks_mutex_;
    
    void emit_chunk(const TranscriptionChunk& chunk) {
        std::lock_guard<std::mutex> lock(callbacks_mutex_);
        for (const auto& callback : chunk_callbacks_) {
            try {
                callback(chunk);
            } catch (const std::exception& e) {
                // Log but don't crash
                std::cerr << "Callback exception: " << e.what() << "\n";
            }
        }
    }
};
```

### GUI Thread Safety
- Callbacks execute in transcription thread
- GUI must use `QMetaObject::invokeMethod()` or similar to marshal to UI thread
- See Qt example above

---

## Performance Requirements

| Metric | Target | Notes |
|--------|--------|-------|
| Chunk latency | <500ms | Time from speech to chunk emission |
| Reclassification latency | <1s | Time to detect and emit reclassification |
| Realtime factor | <1.0x | Must keep up with live audio |
| Memory usage | <500 MB | Including models |
| CPU usage | <50% | On modern multi-core CPU |

---

## Future Enhancements

### V2 Features (Not in initial scope)

1. **Speaker Naming**
   ```cpp
   void set_speaker_name(int speaker_id, const std::string& name);
   // GUI can display: [Alice] instead of [S0]
   ```

2. **Confidence Thresholds**
   ```cpp
   config.min_speaker_confidence = 0.7;  // Don't emit if confidence too low
   ```

3. **Export Functionality**
   ```cpp
   void export_transcription(const std::string& path, ExportFormat format);
   // Formats: TXT, JSON, SRT (subtitles), VTT
   ```

4. **Playback Synchronization**
   ```cpp
   void seek_to_time(int64_t timestamp_ms);
   TranscriptionChunk get_chunk_at_time(int64_t timestamp_ms);
   // For reviewing recorded audio with synchronized transcript
   ```

5. **Multi-channel Audio**
   ```cpp
   config.audio_channels = 2;  // Stereo
   config.speaker_separation_mode = SpeakerSeparationMode::BY_CHANNEL;
   // Speaker 0 = left channel, Speaker 1 = right channel
   ```

---

## Summary

This API provides a clean separation between:
- **Low-level:** Audio capture, Whisper, diarization (existing code)
- **Mid-level:** TranscriptionController (this API)
- **High-level:** GUI application

**Key design principles:**
✅ Event-driven (no polling)  
✅ Retroactive updates (reclassification)  
✅ Thread-safe  
✅ Performance-conscious  
✅ Easy to test  
✅ GUI-framework agnostic  

**Next steps:**
1. Implement `TranscriptionController` class
2. Wire up to existing transcription engine
3. Add reclassification logic
4. Create test applications
5. Document for GUI team

---

**Status:** Design Complete - Ready for Implementation  
**Estimated effort:** 2 weeks (1 developer)  
**Dependencies:** Existing transcription engine (Whisper + diarization)
