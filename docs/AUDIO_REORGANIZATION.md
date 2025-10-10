# Audio Input Architecture - Reorganization Complete

**Date:** 2025-01-09  
**Status:** ✅ Reorganized - Uses Existing Working Code  

---

## File Organization

### Platform-Agnostic (src/audio/)

**Core Interfaces:**
- `audio_input_device.hpp` - `IAudioInputDevice` interface, factory, device info structs
- `audio_input_device.cpp` - Factory implementation (platform dispatch)
- `audio_input_device_synthetic.{hpp,cpp}` - Synthetic device (wraps `FileCapture`)

**Existing Working Implementations:**
- `file_capture.{hpp,cpp}` - ✅ WAV file reader with chunked output (~20ms)
- `windows_wasapi.{hpp,cpp}` - ✅ Working WASAPI capture (mono int16)
- `windows_wasapi_out.{hpp,cpp}` - ✅ Working WASAPI playback
- `drwav_resample.{hpp,cpp}` - ✅ Sample rate conversion
- `audio_queue.hpp` - Queue utilities

### Platform-Specific

**src/audio/win/**
- `audio_input_device_windows.{hpp,cpp}` - Windows WASAPI adapter
  - Wraps existing `WindowsWasapiCapture` into `IAudioInputDevice` interface
  - Handles threading and callback marshalling
  - Device enumeration

**src/audio/mac/** (TODO)
- `audio_input_device_macos.{hpp,cpp}` - macOS CoreAudio adapter (planned)

---

## Architecture: Reuse Existing Code

### Design Decision: Adapter Pattern

Instead of rewriting working code, we created thin adapters:

```
┌─────────────────────────────────────────────────┐
│      IAudioInputDevice (Abstract Interface)     │
│  - initialize(), start(), stop()                │
│  - Audio callback: (samples, count, sr, ch)     │
└───────────┬─────────────────┬───────────────────┘
            │                 │
   ┌────────▼────────┐   ┌───▼──────────────┐
   │ Synthetic       │   │ Windows          │
   │ Adapter         │   │ Adapter          │
   └────────┬────────┘   └───┬──────────────┘
            │                │
   ┌────────▼────────┐   ┌───▼──────────────┐
   │ FileCapture     │   │WindowsWasapiCapture│
   │ (EXISTING ✅)   │   │ (EXISTING ✅)      │
   └─────────────────┘   └──────────────────┘
```

###Benefits:
- ✅ No code duplication
- ✅ Proven, tested implementations
- ✅ Clean interface for controller
- ✅ Easy to add new platforms

---

## Existing Implementations (Already Working)

### 1. FileCapture (src/audio/file_capture.{hpp,cpp})

**Features:**
```cpp
class FileCapture {
    bool start_from_wav(const std::string& path);
    void stop();
    std::vector<int16_t> read_chunk();  // Returns ~20ms chunks
    
    int sample_rate() const;
    int channels() const;
    double duration_seconds() const;
};
```

**What it does:**
- Reads entire WAV file into memory (mono int16)
- Returns chunks on demand (~20ms worth)
- Already handles stereo→mono conversion
- Used in existing tests

### 2. WindowsWasapiCapture (src/audio/windows_wasapi.{hpp,cpp})

**Features:**
```cpp
class WindowsWasapiCapture {
    static std::vector<DeviceInfo> list_input_devices();  // Enumerate
    
    bool start();
    bool start_with_device(const std::string& device_id);
    void stop();
    
    std::vector<int16_t> read_chunk();  // Returns captured audio (mono int16)
    
    int sample_rate() const;
    int channels() const;
};
```

**What it does:**
- WASAPI shared mode capture
- Handles device enumeration
- Converts to mono int16 automatically
- Already used in console tests

### 3. WindowsWasapiOut (src/audio/windows_wasapi_out.{hpp,cpp})

**Features:**
```cpp
class WindowsWasapiOut {
    bool start(int sample_rate, int channels = 1);
    void stop();
    void write(const short* data, unsigned long long frames);
};
```

**What it does:**
- WASAPI playback
- Used for "hear what's being transcribed" feature
- Already working

---

## New Adapter Implementations

### AudioInputDevice_Synthetic

**Location:** `src/audio/audio_input_device_synthetic.{hpp,cpp}`

**Strategy:** Wraps `FileCapture` + `WindowsWasapiOut`

```cpp
class AudioInputDevice_Synthetic : public IAudioInputDevice {
    FileCapture file_capture_;           // Reads WAV
    WindowsWasapiOut playback_out_;      // Plays to speakers
    
    void capture_thread_func() {
        while (!should_stop_) {
            auto chunk = file_capture_.read_chunk();  // Get ~20ms
            
            if (playback_enabled_) {
                playback_out_.write(chunk.data(), chunk.size());  // Play it
            }
            
            audio_callback_(chunk.data(), chunk.size(), sr, 1);  // Send to controller
            
            std::this_thread::sleep_for(100ms);  // Real-time simulation
        }
    }
};
```

**Features:**
- ✅ Real-time file playback simulation
- ✅ Optional speaker output (hear what's transcribed)
- ✅ Looping support
- ✅ Uses existing tested code

### AudioInputDevice_Windows

**Location:** `src/audio/win/audio_input_device_windows.{hpp,cpp}`

**Strategy:** Wraps `WindowsWasapiCapture`

```cpp
class AudioInputDevice_Windows : public IAudioInputDevice {
    WindowsWasapiCapture wasapi_capture_;  // Existing implementation
    
    void capture_thread_func() {
        while (!should_stop_) {
            auto chunk = wasapi_capture_.read_chunk();  // Get captured audio
            
            if (!chunk.empty()) {
                audio_callback_(chunk.data(), chunk.size(), sr, 1);  // Send to controller
            }
            
            std::this_thread::sleep_for(1ms);  // Brief sleep
        }
    }
    
    static std::vector<AudioDeviceInfo> enumerate_windows_devices() {
        auto wasapi_devs = WindowsWasapiCapture::list_input_devices();
        // Convert to AudioDeviceInfo format
        ...
    }
};
```

**Features:**
- ✅ Device enumeration (wraps WASAPI list)
- ✅ Default device support
- ✅ Specific device selection by ID
- ✅ Uses existing tested WASAPI code

---

## API Usage (Unchanged)

### Enumerate Devices

```cpp
auto devices = audio::AudioInputFactory::enumerate_devices();

for (const auto& dev : devices) {
    printf("%s: %s (%s)\n", dev.id.c_str(), dev.name.c_str(), dev.driver.c_str());
}

// Output:
// default: Microphone (Realtek HD) (WASAPI)
// {8DB9...}: Headset Microphone (WASAPI)
// synthetic: Synthetic Device (File Playback) (Synthetic)
```

### Create & Use Device

```cpp
// Create synthetic device
auto device = audio::AudioInputFactory::create_device("synthetic");

// Configure
audio::AudioInputConfig config;
config.synthetic_file_path = "test.wav";
config.synthetic_playback = true;  // Play to speakers
config.buffer_size_ms = 100;

// Initialize with callbacks
device->initialize(config,
    [](const int16_t* samples, size_t count, int sr, int ch) {
        // Process audio
    },
    [](const std::string& err, bool fatal) {
        // Handle errors
    }
);

// Start
device->start();

// ... audio callbacks fire automatically ...

// Stop
device->stop();
```

---

## Integration with TranscriptionController

### Add to TranscriptionController

```cpp
// In transcription_controller.hpp:
class TranscriptionController {
public:
    bool start_with_device(const std::string& device_id = "");
    
private:
    std::unique_ptr<audio::IAudioInputDevice> audio_device_;
};

// In transcription_controller.cpp:
bool TranscriptionController::Impl::start_with_device(const std::string& device_id) {
    // Create device
    audio_device_ = audio::AudioInputFactory::create_device(device_id);
    
    // Configure
    audio::AudioInputConfig audio_config;
    audio_config.device_id = device_id;
    audio_config.synthetic_file_path = config.synthetic_file_path;
    audio_config.synthetic_playback = config.synthetic_playback;
    
    // Initialize with callback
    audio_device_->initialize(audio_config,
        [this](const int16_t* samples, size_t count, int sr, int ch) {
            this->add_audio(samples, count, sr);  // Feed existing pipeline
        },
        [this](const std::string& err, bool fatal) {
            if (config.on_status) {
                config.on_status(err, fatal);
            }
        }
    );
    
    // Start
    return audio_device_->start();
}
```

---

## Testing Plan

### Test 1: Synthetic Device

```cpp
// apps/test_audio_device_synthetic.cpp

int main() {
    auto device = audio::AudioInputFactory::create_device("synthetic");
    
    audio::AudioInputConfig config;
    config.synthetic_file_path = "test_data/podcast_30s.wav";
    config.synthetic_playback = true;
    
    device->initialize(config,
        [](const int16_t* s, size_t n, int sr, int ch) {
            printf("Got %zu samples at %dHz\n", n, sr);
        },
        [](const std::string& err, bool fatal) {
            fprintf(stderr, "Error: %s\n", err.c_str());
        }
    );
    
    device->start();
    
    while (device->is_capturing()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    device->stop();
}
```

### Test 2: Windows Microphone

```cpp
// apps/test_audio_device_windows.cpp

int main() {
    // List devices
    auto devices = audio::AudioInputFactory::enumerate_devices();
    for (const auto& dev : devices) {
        printf("[%s] %s\n", dev.id.c_str(), dev.name.c_str());
    }
    
    // Use default
    auto device = audio::AudioInputFactory::create_device("");
    
    device->initialize({},
        [](const int16_t* s, size_t n, int sr, int ch) {
            printf("Captured %zu samples\n", n);
        },
        nullptr
    );
    
    device->start();
    std::this_thread::sleep_for(std::chrono::seconds(5));
    device->stop();
}
```

### Test 3: Full Integration with Controller

```cpp
// apps/test_controller_with_audio_device.cpp

int main(int argc, char** argv) {
    TranscriptionController::Config config;
    config.model_path = "models/ggml-base.en.bin";
    config.synthetic_file_path = argv[1];
    config.synthetic_playback = true;
    
    config.on_segment = [](const auto& seg) {
        printf("[%d] %s\n", seg.speaker_id, seg.text.c_str());
    };
    
    TranscriptionController controller;
    controller.initialize(config);
    controller.start_with_device("synthetic");
    
    while (controller.is_running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    controller.stop();
}
```

---

## CMakeLists.txt Updates

```cmake
# Audio library
add_library(audio_lib STATIC
    src/audio/audio_input_device.cpp
    src/audio/audio_input_device_synthetic.cpp
    src/audio/file_capture.cpp
    src/audio/drwav_resample.cpp
)

if(WIN32)
    target_sources(audio_lib PRIVATE
        src/audio/windows_wasapi.cpp
        src/audio/windows_wasapi_out.cpp
        src/audio/win/audio_input_device_windows.cpp
    )
elseif(APPLE)
    target_sources(audio_lib PRIVATE
        # src/audio/mac/audio_input_device_macos.cpp  # TODO
    )
endif()

target_include_directories(audio_lib PUBLIC src)

# Tests
add_executable(test_audio_device_synthetic
    apps/test_audio_device_synthetic.cpp
)
target_link_libraries(test_audio_device_synthetic PRIVATE audio_lib)

add_executable(test_audio_device_windows
    apps/test_audio_device_windows.cpp
)
target_link_libraries(test_audio_device_windows PRIVATE audio_lib)

add_executable(test_controller_with_audio_device
    apps/test_controller_with_audio_device.cpp
)
target_link_libraries(test_controller_with_audio_device PRIVATE
    core_lib
    asr_whisper
    diarization
    audio_lib
)
```

---

## Summary

### ✅ What We Have Now

1. **Clean Abstraction**: `IAudioInputDevice` interface
2. **Adapter Pattern**: Wraps existing working code
3. **Platform Organization**: `win/` and `mac/` subfolders
4. **No Duplication**: Reuses `FileCapture`, `WindowsWasapiCapture`, `WindowsWasapiOut`
5. **Testable**: Can test audio layer independently
6. **Ready for Integration**: Controller just needs `start_with_device()`

### 🔄 Next Steps

1. Add `start_with_device()` to TranscriptionController
2. Create test programs
3. Update CMakeLists.txt
4. Test synthetic device with 30s file
5. Test Windows microphone
6. Full integration test (controller + device + GUI)

### 📋 Future

- macOS CoreAudio adapter in `src/audio/mac/`
- Linux ALSA/PulseAudio adapter
- Network stream device
- Multi-device mixing
