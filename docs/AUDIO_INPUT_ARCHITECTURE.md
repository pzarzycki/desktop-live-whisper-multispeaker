# Audio Input Device Architecture

**Status:** ✅ Designed | 🔄 Implementation In Progress  
**Location:** `src/audio/audio_input_device*.{hpp,cpp}`

---

## Design Overview

### Separation of Concerns

```
┌─────────────────────────────────────────────────────────────┐
│                    UI Layer (ImGui)                         │
│  - Device enumeration dropdown                              │
│  - Start/Stop buttons                                       │
│  - Status display                                           │
└─────────────────────┬───────────────────────────────────────┘
                      │ Controls
                      ▼
┌─────────────────────────────────────────────────────────────┐
│            TranscriptionController                          │
│  - Manages audio device lifecycle                           │
│  - Receives audio callbacks                                 │
│  - Processes transcription                                  │
│  - Emits events (segments, stats)                          │
└─────────────────────┬───────────────────────────────────────┘
                      │ Audio Callback
                      ▼
┌─────────────────────────────────────────────────────────────┐
│            IAudioInputDevice (Abstract)                     │
│  - initialize() / start() / stop()                         │
│  - Audio callback: (samples, count, sr, channels)          │
│  - Error callback: (message, is_fatal)                     │
└─────────────────────┬───────────────────────────────────────┘
                      │ Platform Implementations
        ┌─────────────┼─────────────┬──────────────┐
        ▼             ▼             ▼              ▼
┌──────────────┐ ┌─────────┐ ┌──────────┐ ┌──────────────┐
│   Windows    │ │  macOS  │ │Synthetic │ │   Future     │
│   WASAPI     │ │CoreAudio│ │   File   │ │  (Linux...)  │
└──────────────┘ └─────────┘ └──────────┘ └──────────────┘
```

### Key Design Decisions

#### 1. **Device Lifecycle Ownership**

**Decision:** TranscriptionController owns the audio device

**Rationale:**
- Controller needs audio to function - they're tightly coupled
- Controller manages start/stop of transcription + audio capture together
- Simpler API for users: one `controller.start()` does everything

**Implementation:**
```cpp
class TranscriptionController {
    std::unique_ptr<audio::IAudioInputDevice> audio_device_;
    
    bool start_with_device(const std::string& device_id);
    void stop();
};
```

#### 2. **Push-Based Audio Delivery**

**Decision:** Audio device pushes data via callback (not pull)

**Rationale:**
- Matches native OS audio APIs (WASAPI, CoreAudio)
- Low latency - no polling
- Audio thread calls controller directly

**Implementation:**
```cpp
audio_device_->initialize(config, 
    [this](const int16_t* samples, size_t count, int sr, int ch) {
        // Called from audio thread
        this->add_audio(samples, count, sr);
    },
    [this](const std::string& error, bool fatal) {
        // Called on errors
        if (config.on_status) {
            config.on_status(error, fatal);
        }
    }
);
```

#### 3. **Device Enumeration at UI Level**

**Decision:** UI enumerates devices, user selects, passes ID to controller

**Rationale:**
- UI needs device list for dropdown
- User selection happens in UI
- Controller doesn't need UI concerns

**Implementation:**
```cpp
// In ImGui UI code:
auto devices = audio::AudioInputFactory::enumerate_devices();

if (ImGui::BeginCombo("Microphone", selected_device_name.c_str())) {
    for (const auto& dev : devices) {
        if (ImGui::Selectable(dev.name.c_str())) {
            selected_device_id = dev.id;
            selected_device_name = dev.name;
        }
    }
    ImGui::EndCombo();
}

if (ImGui::Button("Start")) {
    controller.start_with_device(selected_device_id);
}
```

---

## API Reference

### Core Interfaces

#### AudioDeviceInfo
```cpp
struct AudioDeviceInfo {
    std::string id;              // "wasapi:default", "synthetic", etc.
    std::string name;            // "Microphone (Realtek HD Audio)"
    std::string driver;          // "WASAPI", "CoreAudio", "Synthetic"
    int default_sample_rate;     // 48000, 44100, etc.
    int max_channels;            // 1=mono only, 2=stereo capable
    bool is_default;             // System default device?
};
```

#### AudioInputConfig
```cpp
struct AudioInputConfig {
    std::string device_id;       // From AudioDeviceInfo.id
    int sample_rate = 48000;     // Requested sample rate
    int channels = 1;            // 1=mono, 2=stereo
    int buffer_size_ms = 100;    // Latency (50-200ms typical)
    
    // For synthetic device:
    std::string synthetic_file_path;    // "test_data/podcast.wav"
    bool synthetic_loop = false;        // Loop playback?
    bool synthetic_playback = true;     // Play to speakers?
};
```

#### IAudioInputDevice (Abstract)
```cpp
class IAudioInputDevice {
public:
    virtual bool initialize(
        const AudioInputConfig& config,
        AudioCallback audio_callback,  // (samples, count, sr, channels)
        ErrorCallback error_callback   // (message, is_fatal)
    ) = 0;
    
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool is_capturing() const = 0;
    
    virtual AudioDeviceInfo get_device_info() const = 0;
    virtual AudioInputConfig get_actual_config() const = 0;
};
```

#### AudioInputFactory
```cpp
class AudioInputFactory {
public:
    // Enumerate all devices (Windows WASAPI + synthetic)
    static std::vector<AudioDeviceInfo> enumerate_devices();
    
    // Create device by ID
    static std::unique_ptr<IAudioInputDevice> create_device(
        const std::string& device_id = ""  // "" = default
    );
    
    // Utilities
    static std::string get_default_device_id();
    static bool is_device_available(const std::string& device_id);
};
```

---

## Platform Implementations

### ✅ Synthetic Device (DONE)

**File:** `audio_input_device_synthetic.{hpp,cpp}`

**Features:**
- Reads WAV file in real-time chunks
- Simulates microphone latency (buffer_size_ms intervals)
- Optional looping for continuous testing
- Optional playback to speakers (TODO: needs AudioOutputDevice)

**Usage:**
```cpp
AudioInputConfig config;
config.device_id = "synthetic";
config.synthetic_file_path = "test_data/podcast_30s.wav";
config.synthetic_loop = false;
config.synthetic_playback = true;  // Play while capturing
config.buffer_size_ms = 100;       // 100ms chunks

auto device = AudioInputFactory::create_device("synthetic");
device->initialize(config, 
    [](const int16_t* samples, size_t count, int sr, int ch) {
        printf("Got %zu samples at %dHz\n", count, sr);
    },
    [](const std::string& err, bool fatal) {
        fprintf(stderr, "Error: %s\n", err.c_str());
    }
);
device->start();
```

**Lifecycle:**
1. `load_wav_file()` - Read entire file into memory
2. `start()` - Spawn capture thread
3. Capture thread:
   - Read `buffer_size_ms` worth of samples
   - Call `audio_callback_` with chunk
   - Sleep until next interval (real-time simulation)
   - Loop or stop at end
4. `stop()` - Signal thread to exit, join

### 🔄 Windows WASAPI (STUB)

**File:** `audio_input_device_windows.{hpp,cpp}`

**TODO:**
- Implement `initialize_wasapi()` - Create WASAPI client
- Implement `capture_thread_func()` - Read from capture buffer
- Implement `enumerate_windows_devices()` - Use IMMDeviceEnumerator
- Convert format to PCM16 if needed
- Handle device disconnection / format changes

**Reference:** Existing `src/audio/windows_wasapi.cpp` has working WASAPI code

### 📋 macOS CoreAudio (PLANNED)

**File:** `audio_input_device_macos.{hpp,cpp}`

**TODO:**
- Use AudioQueue or AudioUnit for capture
- Enumerate devices via AudioObjectGetPropertyData
- Handle sample rate conversion if needed

---

## Integration with TranscriptionController

### Updated TranscriptionController API

```cpp
class TranscriptionController {
public:
    // NEW: Start with specific audio device
    bool start_with_device(const std::string& device_id = "");
    
    // NEW: Start with custom device (for testing)
    bool start_with_custom_device(std::unique_ptr<audio::IAudioInputDevice> device);
    
    // Existing methods still work:
    bool start();  // Uses default device
    void stop();
    void add_audio(...);  // Can still be called manually
    
private:
    std::unique_ptr<audio::IAudioInputDevice> audio_device_;
    
    void on_audio_captured(const int16_t* samples, size_t count, int sr, int ch);
    void on_audio_error(const std::string& message, bool is_fatal);
};
```

### Implementation Strategy

```cpp
// In transcription_controller.cpp:

bool TranscriptionController::Impl::start_with_device(const std::string& device_id) {
    if (running) return false;
    
    // 1. Create audio device
    audio_device_ = audio::AudioInputFactory::create_device(device_id);
    if (!audio_device_) {
        if (config.on_status) {
            config.on_status("Failed to create audio device", true);
        }
        return false;
    }
    
    // 2. Configure audio device
    audio::AudioInputConfig audio_config;
    audio_config.device_id = device_id;
    audio_config.sample_rate = 48000;  // Or from config
    audio_config.channels = 1;         // Mono for Whisper
    audio_config.buffer_size_ms = 100; // 100ms chunks
    
    // For synthetic device:
    if (device_id == "synthetic" || device_id.find("synthetic:") == 0) {
        audio_config.synthetic_file_path = config.synthetic_file_path;
        audio_config.synthetic_loop = config.synthetic_loop;
        audio_config.synthetic_playback = config.synthetic_playback;
    }
    
    // 3. Initialize with callbacks
    bool ok = audio_device_->initialize(
        audio_config,
        [this](const int16_t* samples, size_t count, int sr, int ch) {
            this->on_audio_captured(samples, count, sr, ch);
        },
        [this](const std::string& err, bool fatal) {
            this->on_audio_error(err, fatal);
        }
    );
    
    if (!ok) {
        if (config.on_status) {
            config.on_status("Failed to initialize audio device", true);
        }
        audio_device_.reset();
        return false;
    }
    
    // 4. Start audio capture
    if (!audio_device_->start()) {
        if (config.on_status) {
            config.on_status("Failed to start audio capture", true);
        }
        audio_device_.reset();
        return false;
    }
    
    running = true;
    if (config.on_status) {
        auto info = audio_device_->get_device_info();
        config.on_status("Started: " + info.name, false);
    }
    
    return true;
}

void TranscriptionController::Impl::on_audio_captured(
    const int16_t* samples, 
    size_t count, 
    int sr, 
    int ch
) {
    // Convert stereo to mono if needed
    if (ch == 2) {
        std::vector<int16_t> mono(count / 2);
        for (size_t i = 0; i < mono.size(); ++i) {
            int32_t left = samples[i * 2];
            int32_t right = samples[i * 2 + 1];
            mono[i] = static_cast<int16_t>((left + right) / 2);
        }
        add_audio(mono.data(), mono.size(), sr);
    } else {
        add_audio(samples, count, sr);
    }
}

void TranscriptionController::Impl::on_audio_error(
    const std::string& message, 
    bool is_fatal
) {
    if (config.on_status) {
        config.on_status("Audio error: " + message, is_fatal);
    }
    
    if (is_fatal) {
        // Device failed, stop everything
        stop();
    }
}

void TranscriptionController::Impl::stop() {
    running = false;
    
    if (audio_device_) {
        audio_device_->stop();
        audio_device_.reset();
    }
    
    // Flush remaining audio...
}
```

---

## Complete Usage Example

### Test Program: Synthetic Microphone

```cpp
// apps/test_controller_with_device.cpp

#include "core/transcription_controller.hpp"
#include "audio/audio_input_device.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <audio_file.wav>\n", argv[0]);
        return 1;
    }
    
    // 1. Enumerate available devices
    std::cout << "=== Available Audio Devices ===\n";
    auto devices = audio::AudioInputFactory::enumerate_devices();
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << i << ": " << devices[i].name 
                  << " (" << devices[i].driver << ")"
                  << (devices[i].is_default ? " [DEFAULT]" : "")
                  << "\n";
    }
    std::cout << "\n";
    
    // 2. Configure transcription controller
    TranscriptionController::Config config;
    config.model_path = "models/ggml-base.en.bin";
    config.enable_diarization = true;
    config.max_speakers = 2;
    
    // Synthetic device config
    config.synthetic_file_path = argv[1];
    config.synthetic_loop = false;
    config.synthetic_playback = true;
    
    // 3. Setup callbacks
    config.on_segment = [](const auto& seg) {
        printf("[Speaker %d] %.2f-%.2f: %s\n",
               seg.speaker_id, 
               seg.start_ms / 1000.0, 
               seg.end_ms / 1000.0,
               seg.text.c_str());
    };
    
    config.on_stats = [](const auto& stats) {
        std::cout << "\n=== Speaker Statistics ===\n";
        for (const auto& s : stats) {
            printf("Speaker %d: %.1fs (%d segments)\n",
                   s.speaker_id,
                   s.total_speaking_time_ms / 1000.0,
                   s.segment_count);
        }
        std::cout << "\n";
    };
    
    config.on_status = [](const std::string& msg, bool is_error) {
        printf("[%s] %s\n", is_error ? "ERROR" : "INFO", msg.c_str());
    };
    
    // 4. Initialize controller
    TranscriptionController controller;
    if (!controller.initialize(config)) {
        fprintf(stderr, "Failed to initialize controller\n");
        return 1;
    }
    
    // 5. Start with synthetic device
    std::cout << "Starting with synthetic device...\n";
    if (!controller.start_with_device("synthetic")) {
        fprintf(stderr, "Failed to start controller\n");
        return 1;
    }
    
    // 6. Wait for audio to finish
    std::cout << "Processing audio (real-time simulation)...\n";
    while (controller.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // 7. Stop and print metrics
    controller.stop();
    
    auto metrics = controller.get_performance_metrics();
    std::cout << "\n=== Performance Metrics ===\n";
    printf("Realtime factor: %.2fx\n", metrics.realtime_factor);
    printf("Whisper time: %.2fs\n", metrics.whisper_time_s);
    printf("Diarization time: %.2fs\n", metrics.diarization_time_s);
    printf("Segments processed: %zu\n", metrics.segments_processed);
    
    return 0;
}
```

### ImGui Integration

```cpp
// In ImGui app window:

class TranscriptionWindow {
    TranscriptionController controller_;
    std::vector<audio::AudioDeviceInfo> available_devices_;
    int selected_device_index_ = 0;
    
    void init() {
        // Enumerate devices once
        available_devices_ = audio::AudioInputFactory::enumerate_devices();
        
        // Find default
        for (size_t i = 0; i < available_devices_.size(); ++i) {
            if (available_devices_[i].is_default) {
                selected_device_index_ = static_cast<int>(i);
                break;
            }
        }
        
        // Setup controller
        TranscriptionController::Config config;
        config.model_path = "models/ggml-base.en.bin";
        config.on_segment = [this](const auto& seg) {
            segments_.push_back(seg);
        };
        config.on_stats = [this](const auto& stats) {
            speaker_stats_ = stats;
        };
        config.on_status = [this](const std::string& msg, bool err) {
            status_messages_.push_back({msg, err});
        };
        
        controller_.initialize(config);
    }
    
    void render_controls() {
        // Device selection
        ImGui::Text("Microphone:");
        ImGui::SameLine();
        
        if (ImGui::BeginCombo("##device", 
                              available_devices_[selected_device_index_].name.c_str())) {
            for (size_t i = 0; i < available_devices_.size(); ++i) {
                const auto& dev = available_devices_[i];
                bool is_selected = (i == selected_device_index_);
                
                if (ImGui::Selectable(dev.name.c_str(), is_selected)) {
                    selected_device_index_ = static_cast<int>(i);
                }
                
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        
        // Start/Stop button
        ImGui::SameLine();
        if (controller_.is_running()) {
            if (ImGui::Button("Stop")) {
                controller_.stop();
            }
        } else {
            if (ImGui::Button("Start")) {
                const auto& device = available_devices_[selected_device_index_];
                controller_.start_with_device(device.id);
            }
        }
        
        // Status indicator
        if (controller_.is_running()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "● Recording");
        }
    }
    
    void render_transcript() {
        ImGui::BeginChild("Transcript", ImVec2(0, 300), true);
        for (const auto& seg : segments_) {
            ImVec4 color = speaker_colors_[seg.speaker_id % 8];
            ImGui::TextColored(color, "[%.1fs] %s", 
                             seg.start_ms / 1000.0, seg.text.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
    }
    
    void render_speaker_stats() {
        ImGui::Text("Speaker Statistics:");
        for (const auto& s : speaker_stats_) {
            ImGui::Text("Speaker %d: %.1fs (%d segments)",
                       s.speaker_id,
                       s.total_speaking_time_ms / 1000.0,
                       s.segment_count);
        }
    }
};
```

---

## Implementation Checklist

### ✅ Phase 1: Foundation (DONE)
- [x] `IAudioInputDevice` interface
- [x] `AudioDeviceInfo`, `AudioInputConfig` structs
- [x] `AudioInputFactory` class
- [x] `AudioInputDevice_Synthetic` implementation
- [x] Windows stub (`audio_input_device_windows.hpp`)

### 🔄 Phase 2: Integration (NEXT)
- [ ] Add `start_with_device()` to TranscriptionController
- [ ] Add synthetic device config to TranscriptionController::Config
- [ ] Implement `on_audio_captured()` callback handler
- [ ] Update CMakeLists.txt to build new audio files
- [ ] Create `test_controller_with_device.cpp`
- [ ] Test with 30s podcast WAV file

### 📋 Phase 3: Windows WASAPI
- [ ] Copy working code from `windows_wasapi.cpp`
- [ ] Implement `AudioInputDevice_Windows::initialize_wasapi()`
- [ ] Implement `AudioInputDevice_Windows::capture_thread_func()`
- [ ] Implement `AudioInputDevice_Windows::enumerate_windows_devices()`
- [ ] Test with real microphone

### 📋 Phase 4: macOS CoreAudio
- [ ] Create `audio_input_device_macos.{hpp,cpp}`
- [ ] Implement using AudioQueue or AudioUnit
- [ ] Device enumeration
- [ ] Test on macOS

---

## Benefits of This Design

✅ **Clean separation:** Audio input is its own concern  
✅ **Testable:** Synthetic device for automated testing  
✅ **Cross-platform:** Platform-specific implementations behind common interface  
✅ **UI-friendly:** Device enumeration for dropdowns  
✅ **Flexible:** Can add new device types (network streams, etc.)  
✅ **Real-time:** Push-based callbacks for low latency  
✅ **Error handling:** Graceful degradation on device failures  
✅ **Lifecycle management:** Clear ownership (controller owns device)
