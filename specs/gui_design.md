# GUI Design Document - Desktop Live Whisper

**Date:** 2025-10-08  
**Framework:** Qt6 Quick (QML)  
**Target Platforms:** Windows, macOS  
**Design Philosophy:** Modern, clean, custom look

---

## Overview

A minimal, modern real-time transcription application with speaker diarization. The UI prioritizes readability and real-time feedback.

---

## Main Window Design

### Layout

```
┌─────────────────────────────────────────────────────────────────┐
│  Desktop Live Whisper                                    [−][□][×]│
├─────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ┌──────────────────────────────────┐  ┌──────────────────────┐ │
│  │                                  │  │  Speaker Statistics  │ │
│  │       [  START RECORDING  ]      │  │                      │ │
│  │                                  │  │  🔵 Speaker 0        │ │
│  │                                  │  │     0:42  (65%)      │ │
│  │  ┌────────────────────────────┐  │  │                      │ │
│  │  │                            │  │  │  🔴 Speaker 1        │ │
│  │  │  🔵 What is the most       │  │  │     0:23  (35%)      │ │
│  │  │     beautiful idea in      │  │  │                      │ │
│  │  │     physics?               │  │  │  ────────────────    │ │
│  │  │                            │  │  │  Total: 1:05         │ │
│  │  │  🔴 Conservation of        │  │  │                      │ │
│  │  │     momentum.              │  │  └──────────────────────┘ │
│  │  │                            │  │                            │
│  │  │  🔵 Can you elaborate?     │  │  ┌──────────────────────┐ │
│  │  │                            │  │  │  Settings            │ │
│  │  │  🔴 Yeah. If you were      │  │  │                      │ │
│  │  │     Aristotle, when        │  │  │  Model: tiny.en      │ │
│  │  │     Aristotle wrote...     │  │  │  Max Speakers: 2     │ │
│  │  │                            │  │  │  Threshold: 0.35     │ │
│  │  │                            │  │  │                      │ │
│  │  └────────────────────────────┘  │  └──────────────────────┘ │
│  └──────────────────────────────────┘                            │
│                                                                   │
└─────────────────────────────────────────────────────────────────┘
```

### Color Scheme

**Modern Dark Theme:**
- Background: `#1E1E1E` (dark gray)
- Surface: `#2D2D2D` (lighter gray)
- Text: `#E0E0E0` (light gray)
- Accent: `#007ACC` (blue)
- Speaker 0: `#4A9EFF` (blue)
- Speaker 1: `#FF6B6B` (coral red)
- Speaker 2: `#4ECDC4` (teal)
- Speaker 3: `#FFE66D` (yellow)

**Alternative Light Theme:**
- Background: `#FFFFFF` (white)
- Surface: `#F5F5F5` (light gray)
- Text: `#333333` (dark gray)
- Speaker colors: Same as dark theme (high contrast)

---

## Component Breakdown

### 1. Start/Stop Button

**States:**
- **Idle:** "START RECORDING" - Blue background
- **Recording:** "STOP RECORDING" - Red background
- **Loading:** "Loading..." - Gray background with spinner

**Behavior:**
- Click → Toggle recording state
- Keyboard shortcut: `Ctrl+R` (or `Cmd+R` on macOS)
- Disabled when loading models

**Visual:**
- Rounded corners (8px)
- Large text (16pt)
- Subtle shadow
- Smooth color transition (200ms)

### 2. Transcription Display

**Features:**
- Auto-scroll to bottom (with manual override)
- Copy-to-clipboard functionality
- Each speaker turn is a separate block
- Word-level timestamps (tooltip on hover)
- Confidence indicators (opacity)

**Layout:**
- Margin: 16px
- Line spacing: 1.5x
- Font: 14pt sans-serif
- Max width: 800px (centered)

**Speaker Block:**
```qml
Item {
    // Speaker icon (colored circle)
    Rectangle {
        radius: 8
        color: speakerColor
        
        Text {
            text: "S0"
            color: "white"
        }
    }
    
    // Transcription text
    Text {
        text: chunk.text
        font.pixelSize: 18
        color: textColor
        opacity: chunk.speaker_confidence
    }
    
    // Timestamp (subtle)
    Text {
        text: formatTime(chunk.timestamp_ms)
        font.pixelSize: 10
        color: mutedColor
    }
}
```

### 3. Speaker Statistics Panel

**Content:**
- Speaker list (dynamically grows)
- Color indicator per speaker
- Total speaking time per speaker
- Percentage of total time
- Total session duration

**Updates:**
- Real-time (every chunk)
- Smooth animations for percentage bars

**Visual:**
- Compact cards
- Progress bars showing percentage
- Time formatted as `MM:SS`

### 4. Settings Panel (Collapsible)

**Options:**
- Model selection (dropdown: tiny, base, small)
- Max speakers (spinner: 1-5)
- Speaker threshold (slider: 0.1-0.9)
- Reclassification toggle
- Audio device selection

**Behavior:**
- Disabled during recording
- Changes saved to config
- Visual feedback on change

---

## API Integration

### Events from TranscriptionController

**1. Chunk Received:**
```cpp
controller.subscribe_to_chunks([](const TranscriptionChunk& chunk) {
    // Emit to QML
    emit chunkReceived(
        chunk.id,
        QString::fromStdString(chunk.text),
        chunk.speaker_id,
        chunk.timestamp_ms,
        chunk.duration_ms,
        chunk.speaker_confidence,
        chunk.is_finalized
    );
});
```

**2. Speaker Reclassified:**
```cpp
controller.subscribe_to_reclassification([](const SpeakerReclassification& recl) {
    // Update existing chunks in QML
    emit speakerReclassified(
        QVector<uint64_t>::fromStdVector(recl.chunk_ids),
        recl.old_speaker_id,
        recl.new_speaker_id,
        QString::fromStdString(recl.reason)
    );
});
```

**3. Status Update:**
```cpp
controller.subscribe_to_status([](const TranscriptionStatus& status) {
    emit statusChanged(
        static_cast<int>(status.state),
        status.elapsed_ms,
        status.chunks_emitted,
        status.reclassifications_count,
        status.realtime_factor
    );
});
```

### New API Addition: Speaker Time Tracking

**Add to `TranscriptionChunk`:**
```cpp
struct TranscriptionChunk {
    // ... existing fields ...
    
    int64_t speaker_total_time_ms;  // Total time this speaker has spoken so far
};
```

**OR maintain in controller:**
```cpp
class TranscriptionControllerImpl {
private:
    std::map<int, int64_t> speaker_total_times_;  // speaker_id -> total_ms
    
    void emit_chunk_for_segment(...) {
        // Update total time
        speaker_total_times_[speaker_id] += chunk.duration_ms;
        
        chunk.speaker_total_time_ms = speaker_total_times_[speaker_id];
        emit_chunk(chunk);
    }
};
```

---

## QML Architecture

### File Structure

```
src/ui/qml/
  Main.qml                    # Main window
  components/
    StartStopButton.qml       # Custom button
    TranscriptionView.qml     # Scrollable transcript
    SpeakerBlock.qml          # Individual speaker turn
    SpeakerStatsPanel.qml     # Right-side statistics
    SettingsPanel.qml         # Settings (collapsible)
  models/
    TranscriptionModel.qml    # Data model for chunks
  theme/
    Theme.qml                 # Color scheme and styling
```

### Main.qml Structure

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import App 1.0

ApplicationWindow {
    id: root
    visible: true
    width: 1200
    height: 800
    title: "Desktop Live Whisper"
    
    // Theme
    color: Theme.backgroundColor
    
    // C++ backend bridge
    TranscriptionBridge {
        id: bridge
        
        onChunkReceived: (id, text, speakerId, timestamp, duration, confidence) => {
            transcriptionModel.addChunk({
                id: id,
                text: text,
                speakerId: speakerId,
                timestamp: timestamp,
                duration: duration,
                confidence: confidence
            });
        }
        
        onSpeakerReclassified: (chunkIds, oldSpeaker, newSpeaker, reason) => {
            transcriptionModel.reclassifyChunks(chunkIds, newSpeaker);
        }
        
        onStatusChanged: (state, elapsed, chunks, recls, rtf) => {
            statusBar.updateStatus(state, elapsed, rtf);
        }
    }
    
    // Layout
    RowLayout {
        anchors.fill: parent
        spacing: 0
        
        // Main content area
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16
            
            // Start/Stop button
            StartStopButton {
                id: startStopButton
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 16
                
                onClicked: {
                    if (bridge.isRecording) {
                        bridge.stopRecording();
                    } else {
                        bridge.startRecording();
                    }
                }
            }
            
            // Transcription display
            TranscriptionView {
                id: transcriptionView
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 16
                
                model: transcriptionModel
            }
        }
        
        // Right sidebar
        ColumnLayout {
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            spacing: 16
            
            SpeakerStatsPanel {
                id: statsPanel
                Layout.fillWidth: true
                Layout.margins: 16
                
                model: speakerStatsModel
            }
            
            SettingsPanel {
                id: settingsPanel
                Layout.fillWidth: true
                Layout.margins: 16
                
                enabled: !bridge.isRecording
            }
        }
    }
}
```

---

## Testing Strategy: Synthetic Microphone

### Implementation

**1. Add "Synthetic Mode" to API:**

```cpp
// In TranscriptionConfig
struct TranscriptionConfig {
    // ... existing fields ...
    
    bool use_synthetic_audio = false;
    std::string synthetic_audio_file = "";
    bool playback_synthetic = true;  // Play audio while transcribing
};
```

**2. Wire Up in Controller:**

```cpp
void TranscriptionControllerImpl::processing_loop() {
    if (config_.use_synthetic_audio) {
        // Load WAV file
        auto audio_data = load_wav(config_.synthetic_audio_file);
        
        // Optional: Start playback
        if (config_.playback_synthetic) {
            start_audio_playback(audio_data);
        }
        
        // Process in real-time chunks
        const int chunk_size = 16000 / 2;  // 500ms chunks @ 16kHz
        size_t offset = 0;
        
        while (running_ && offset < audio_data.size()) {
            if (paused_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            size_t chunk_samples = std::min(chunk_size, audio_data.size() - offset);
            process_audio_chunk(audio_data.data() + offset, chunk_samples, offset * 1000 / 16);
            
            // Sleep to simulate real-time
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            offset += chunk_samples;
        }
        
        // Done - emit stopped status
        running_ = false;
    } else {
        // Real microphone capture
        // ... existing code ...
    }
}
```

**3. GUI Integration:**

```qml
// In SettingsPanel.qml
CheckBox {
    text: "Test Mode (Synthetic Audio)"
    checked: bridge.useSyntheticAudio
    onCheckedChanged: bridge.useSyntheticAudio = checked
}

TextField {
    text: bridge.syntheticAudioFile
    placeholderText: "Path to test.wav"
    enabled: bridge.useSyntheticAudio
    onTextChanged: bridge.syntheticAudioFile = text
}
```

---

## Development Phases

### Phase 5.1: Basic GUI (Current) - 2-3 days

**Tasks:**
1. ✅ Design document (this file)
2. Create QML components:
   - Main.qml
   - StartStopButton.qml
   - TranscriptionView.qml
   - SpeakerBlock.qml
3. Create C++ bridge class:
   - TranscriptionBridge (exposes controller to QML)
4. Wire up basic start/stop
5. Test with synthetic audio

**Deliverable:** Working GUI with start/stop, displays transcription, synthetic mode works

### Phase 5.2: Speaker Statistics - 1 day

**Tasks:**
1. Add speaker time tracking to API
2. Create SpeakerStatsPanel.qml
3. Create SpeakerStatsModel
4. Wire up real-time updates

**Deliverable:** Right sidebar shows speaker statistics

### Phase 5.3: Settings & Polish - 1-2 days

**Tasks:**
1. Create SettingsPanel.qml
2. Wire up model/threshold/device selection
3. Add keyboard shortcuts
4. Add copy-to-clipboard
5. Add theme switching
6. Smooth animations

**Deliverable:** Feature-complete GUI

### Phase 5.4: Testing & Packaging - 1 day

**Tasks:**
1. Test on Windows
2. Test on macOS
3. Create installer/package
4. Write user documentation

**Deliverable:** Production-ready application

---

## Implementation Plan

### Step 1: Create TranscriptionBridge Class

**File:** `src/ui/transcription_bridge.hpp`

```cpp
#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include "app/transcription_controller.hpp"

class TranscriptionBridge : public QObject {
    Q_OBJECT
    
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY isRecordingChanged)
    Q_PROPERTY(bool useSyntheticAudio READ useSyntheticAudio WRITE setUseSyntheticAudio NOTIFY useSyntheticAudioChanged)
    Q_PROPERTY(QString syntheticAudioFile READ syntheticAudioFile WRITE setSyntheticAudioFile NOTIFY syntheticAudioFileChanged)
    
public:
    explicit TranscriptionBridge(QObject* parent = nullptr);
    ~TranscriptionBridge();
    
    bool isRecording() const;
    bool useSyntheticAudio() const;
    QString syntheticAudioFile() const;
    
    void setUseSyntheticAudio(bool value);
    void setSyntheticAudioFile(const QString& path);
    
public slots:
    void startRecording();
    void stopRecording();
    QStringList listAudioDevices();
    void selectAudioDevice(const QString& deviceId);
    
signals:
    void isRecordingChanged();
    void useSyntheticAudioChanged();
    void syntheticAudioFileChanged();
    
    void chunkReceived(quint64 id, QString text, int speakerId, 
                      qint64 timestamp, qint64 duration, float confidence);
    void speakerReclassified(QVector<quint64> chunkIds, int oldSpeaker, 
                            int newSpeaker, QString reason);
    void statusChanged(int state, qint64 elapsed, int chunks, 
                      int recls, float rtf);
    void errorOccurred(int severity, QString message, QString details);
    
private:
    std::unique_ptr<app::TranscriptionController> controller_;
    bool is_recording_ = false;
    bool use_synthetic_audio_ = false;
    QString synthetic_audio_file_ = "output/whisper_input_16k.wav";
};
```

### Step 2: Update CMakeLists.txt

```cmake
# Enable Qt app build
set(BUILD_APP ON CACHE BOOL "Build Qt application" FORCE)

# Qt Quick app
qt_add_executable(app_desktop_whisper
    src/ui/main.cpp
    src/ui/transcription_bridge.cpp
    src/ui/transcription_bridge.hpp
)

target_link_libraries(app_desktop_whisper PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
    app_controller
)

qt_add_qml_module(app_desktop_whisper
    URI App
    VERSION 1.0
    QML_FILES 
        src/ui/qml/Main.qml
        src/ui/qml/components/StartStopButton.qml
        src/ui/qml/components/TranscriptionView.qml
        src/ui/qml/components/SpeakerBlock.qml
)
```

### Step 3: Create Basic QML UI

Start with minimal working UI, then iterate.

---

## Performance Considerations

### QML Performance

- Use `Repeater` with `DelegateModel` for transcription list
- Lazy loading for off-screen items
- Smooth animations (60 FPS target)
- Avoid binding loops

### Threading

- Transcription runs in background thread (already implemented)
- QML updates via signals (thread-safe)
- Heavy operations (model changes) queued on main thread

### Memory

- Limit stored chunks (e.g., last 1000)
- Clear old chunks on session end
- Efficient string handling (QString COW)

---

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `Ctrl+R` / `Cmd+R` | Start/Stop recording |
| `Ctrl+C` / `Cmd+C` | Copy transcript |
| `Ctrl+L` / `Cmd+L` | Clear transcript |
| `Ctrl+S` / `Cmd+S` | Export to file |
| `Ctrl+,` / `Cmd+,` | Open settings |

---

## Accessibility

- Keyboard navigation for all controls
- Screen reader support (Qt handles this)
- High contrast mode
- Configurable font sizes
- Focus indicators

---

## Future Enhancements

### Phase 6+:

1. **Export Formats:** TXT, JSON, SRT, VTT
2. **Playback Mode:** Review recorded audio with transcript
3. **Speaker Naming:** Replace S0/S1 with custom names
4. **Multi-session:** Multiple recordings in tabs
5. **Cloud Sync:** Save sessions to cloud
6. **Live Editing:** Correct transcription mistakes
7. **Highlights:** Mark important moments
8. **Search:** Find text in transcript

---

## Summary

**Design Philosophy:** Clean, modern, functional

**Key Features:**
- ✅ One-button operation (Start/Stop)
- ✅ Real-time transcription display
- ✅ Color-coded speakers
- ✅ Speaker statistics (time, percentage)
- ✅ Synthetic audio for testing
- ✅ Cross-platform (Windows, macOS)
- ✅ Modern Qt Quick UI

**Next Steps:**
1. Create TranscriptionBridge class
2. Create basic QML components
3. Wire up start/stop with synthetic audio
4. Test with output/whisper_input_16k.wav
5. Iterate on design

**Estimated Timeline:** 5-7 days to production-ready GUI

---

**Status:** ✅ Design complete, ready to implement!
