# Testing Guide - TranscriptionController

## Overview

The primary test executable is **`test_transcription`**, which validates the complete system:
- ✅ TranscriptionController API (`core/transcription_controller.cpp`)
- ✅ Real-time audio processing with synthetic file playback
- ✅ Whisper transcription engine
- ✅ Speaker diarization (ONNX embeddings)
- ✅ Audio playback (hear what's being transcribed)
- ✅ Platform: Windows & macOS fully supported

## Prerequisites

Models must be in `models/` directory:
- **Whisper model**: `ggml-tiny.en.bin` or `ggml-base.en-q5_1.bin`
- **Speaker embedding model**: `campplus_voxceleb.onnx` (default)

## Run Test

### macOS

```bash
# Build
cmake --preset macos-debug
cmake --build build/macos-debug --target test_transcription -j 4

# Run
./build/macos-debug/test_transcription \
  models/ggml-tiny.en.bin \
  test_data/Sean_Carroll_podcast.wav
```

### Windows

```powershell
# Build
. .\Enter-VSDev.ps1
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target test_transcription

# Run
.\build\test_transcription.exe models\ggml-tiny.en.bin test_data\Sean_Carroll_podcast.wav
```

## Expected Output

- Audio plays through speakers in real-time
- Transcription segments printed as they complete
- Speaker identification (Speaker 0, Speaker 1, etc.)
- Performance metrics at end:
  - Audio duration
  - Wall-clock time
  - Realtime factor (should be < 1.5x for real-time capability)
  - Whisper processing time
  - Diarization time
  - Speaker statistics

## System Architecture

```
test_transcription (apps/test_transcription_controller.cpp)
    ↓
TranscriptionController (core/transcription_controller.cpp)
    ├── WhisperBackend (asr/whisper_backend.cpp)
    │   └── Uses: models/ggml-*.bin
    ├── ContinuousFrameAnalyzer (diar/speaker_cluster.cpp)
    │   └── Uses: models/campplus_voxceleb.onnx
    └── AudioInputDevice (audio/audio_input_device.cpp)
        ├── Synthetic (file playback for testing)
        │   ├── Windows: WASAPI playback (win/windows_wasapi_out.cpp)
        │   └── macOS: CoreAudio playback (mac/coreaudio_output.mm)
        ├── Windows: WASAPI capture (win/audio_input_device_windows.cpp)
        └── macOS: CoreAudio capture (mac/audio_input_device_macos.mm)
```

## Other Test Programs

### test_audio_device
Tests audio device enumeration and basic capture:
```bash
# macOS
./build/macos-debug/test_audio_device

# Windows
.\build\Debug\test_audio_device.exe
```

Use this to debug microphone access issues or list available devices.

### Archived Tests
Historical tests from diarization research (Phase 2-3) are in `tests/archive/`.
These are not built by default and are kept for reference only.

## Configuration Defaults

Defaults are hardcoded in headers - no arguments needed:
- **Whisper model**: Command line argument #1
- **Audio file**: Command line argument #2  
- **ONNX model**: `models/campplus_voxceleb.onnx` (hardcoded in `speaker_cluster.hpp:109`)
- **Language**: `"en"` (hardcoded)
- **Buffer**: 10s window, 5s overlap (hardcoded)
- **Speakers**: max 2, threshold 0.35 (hardcoded)

Override by editing test source if needed, but defaults work for 2-speaker English podcasts.
