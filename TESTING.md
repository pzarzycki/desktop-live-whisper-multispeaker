# Quick Test - TranscriptionController

## Prerequisites

Models must be in `models/` directory:
- **Whisper model**: `ggml-tiny.en.bin` or `ggml-base.en-q5_1.bin`
- **Speaker embedding model**: `campplus_voxceleb.onnx` (default)

## Run Test

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
  - Realtime factor (should be < 1.5x)
  - Whisper processing time
  - Diarization time
  - Speaker statistics

## Architecture

```
TranscriptionController (core/transcription_controller.cpp)
├── WhisperBackend (asr/whisper_backend.cpp)
│   └── Uses: models/ggml-*.bin
├── ContinuousFrameAnalyzer (diar/speaker_cluster.cpp)
│   └── Uses: models/campplus_voxceleb.onnx
└── AudioInputDevice (audio/audio_input_device.cpp)
    ├── Synthetic (file playback for testing)
    └── Windows WASAPI (live microphone)
```

## Configuration Defaults

Defaults are hardcoded in headers - no arguments needed:
- **Whisper model**: Command line argument #1
- **Audio file**: Command line argument #2  
- **ONNX model**: `models/campplus_voxceleb.onnx` (hardcoded in `speaker_cluster.hpp:109`)
- **Language**: `"en"` (hardcoded)
- **Buffer**: 10s window, 5s overlap (hardcoded)
- **Speakers**: max 2, threshold 0.35 (hardcoded)

Override by editing test source if needed, but defaults work for 2-speaker English podcasts.
