# Desktop Live Whisper - Multiplatform

Real-time speech transcription with speaker diarization for Windows and macOS.

## Features

- ✅ **Real-time transcription** with OpenAI Whisper (tiny.en model)
- ✅ **Speaker diarization** - identifies who spoke when
- ✅ **Low latency** - first transcription in ~4 seconds
- ✅ **Production-ready** - async streaming architecture, thoroughly tested
- ✅ **Multiplatform** - Windows & macOS fully supported

## Performance

- **Processing speed:** 0.87x realtime (faster than audio playback)
- **First output:** ~4 seconds from start
- **Memory usage:** ~320 MB
- **Audio quality:** Zero stuttering, zero dropped frames

**Note:** Speaker identification is functional but accuracy needs improvement with better embedding models.

## Quick Start

See detailed setup instructions in [`docs/`](docs/) folder.

**For Developers:** See [`TESTING.md`](TESTING.md) for test instructions and [`specs/plan.md`](specs/plan.md) for project status.

**Technical Details:** See [`specs/architecture.md`](specs/architecture.md) for complete system architecture.

## Project Structure

```
desktop-live-whisper-multispeaker/
├── apps/                          # Active test programs
│   ├── test_transcription_controller.cpp  # PRIMARY TEST (uses TranscriptionController)
│   ├── test_audio_device.cpp              # Audio device debugging
│   └── test_controller_api.cpp            # Legacy API test (source only, not built)
├── src/
│   ├── app/                       # TranscriptionController implementation
│   ├── asr/                       # Whisper ASR backend
│   ├── audio/                     # Audio input/output
│   │   ├── win/                   # Windows-specific (WASAPI)
│   │   └── mac/                   # macOS-specific (CoreAudio)
│   ├── core/                      # Core application logic
│   ├── diar/                      # Speaker diarization (ONNX embeddings)
│   └── ui/                        # ImGui-based GUI
├── tests/
│   └── archive/                   # Historical tests from research phases
├── models/                        # AI models (downloaded separately)
├── test_data/                     # Test audio files
└── third_party/                   # Dependencies (whisper.cpp, imgui, onnxruntime)
```

## Prerequisites

### macOS

**Required:**
- macOS 11.0 (Big Sur) or later
- Xcode Command Line Tools: `xcode-select --install`
- CMake 3.24+: `brew install cmake` or `uv tool install cmake`
- Git (for submodules)

**Build Toolchain:**
- Clang/LLVM (included with Xcode Command Line Tools)
- Metal framework (GPU acceleration, included with macOS)
- Accelerate framework (BLAS operations, included with macOS)

### Windows

**Core Build Tools:**
- CMake 3.24+
- A C++ toolchain. Either:
  - Visual Studio 2022 with "Desktop development with C++" workload, or
  - Visual Studio 2022 Build Tools with MSVC and Windows SDK
- Windows 10/11 SDK (provides the Universal CRT headers like `ucrt\math.h`, `rc.exe`, `mt.exe`)
- Optional: Ninja (faster single-config builds)

---

## Setup & Build

### macOS Setup

1. **Install dependencies:**
   ```bash
   # Install Xcode Command Line Tools (if not already installed)
   xcode-select --install
   
   # Install CMake (choose one method)
   brew install cmake
   # OR using uv:
   uv tool install cmake
   ```

2. **Clone repository with submodules:**
   ```bash
   git clone <repository-url>
   cd desktop-live-whisper-multispeaker
   git submodule update --init --recursive
   ```

3. **Download dependencies and models:**
   ```bash
   # Download ONNX Runtime for macOS
   chmod +x scripts/download_onnxruntime_macos.sh
   ./scripts/download_onnxruntime_macos.sh
   
   # Download models (Whisper + Speaker embedding)
   chmod +x scripts/download_models.sh
   ./scripts/download_models.sh
   ```

4. **Configure and build:**
   ```bash
   # Configure for debug
   cmake --preset macos-debug
   
   # Or for release
   cmake --preset macos-release
   
   # Build (use -j to parallelize)
   cmake --build build/macos-debug -j 4
   ```

5. **Run the application:**
   
   **GUI Application:**
   ```bash
   # Launch GUI app
   open build/macos-debug/app_desktop_whisper.app
   # Or run directly from terminal to see logs:
   ./build/macos-debug/app_desktop_whisper.app/Contents/MacOS/app_desktop_whisper
   ```
   
   **CLI Test:**
   ```bash
   ./build/macos-debug/test_transcription \
     tiny.en \
     test_data/Sean_Carroll_podcast_16k.wav \
     --limit-seconds 20
   ```

**Expected Output:**
- Real-time transcription with speaker labels ([S0], [S1])
- Audio playback through speakers
- Performance metrics (should be <1.0x realtime factor)
- Zero dropped frames

**Platform Features:**
- ✅ CoreAudio for microphone input
- ✅ Metal backend for GPU acceleration
- ✅ Accelerate framework for BLAS operations
- ✅ Universal binary support (arm64 + x86_64)
- ✅ Audio playback in synthetic test mode

**Available Test Programs:**
- `test_transcription` - Main integration test (TranscriptionController + audio + diarization)
- `test_audio_device` - Audio device enumeration and capture testing

---

### Windows Setup

1. **Install prerequisites:**
   - Visual Studio 2022 (or Build Tools) with "Desktop development with C++"
   - CMake 3.24+
   - Git (for submodules)

2. **Clone repository with submodules:**
   ```powershell
   git clone <repository-url>
   cd desktop-live-whisper-multispeaker
   git submodule update --init --recursive
   ```

3. **Download dependencies and models:**
   ```powershell
   # Download ONNX Runtime for Windows
   .\scripts\download_onnxruntime.ps1
   
   # Download models (Whisper + Speaker embedding)
   .\scripts\download_models.ps1
   ```

4. **Configure and build:**
   ```powershell
   # Configure for debug
   cmake --preset windows-debug
   
   # Or for release
   cmake --preset windows-release
   
   # Build (use -j to parallelize)
   cmake --build build/windows-debug -j 4
   ```

5. **Run the application:**
   
   **GUI Application:**
   ```powershell
   # Launch GUI app
   .\build\windows-debug\app_desktop_whisper.exe
   ```
   
   **CLI Test:**
   ```powershell
   .\build\windows-debug\test_transcription.exe tiny.en test_data\Sean_Carroll_podcast.wav --limit-seconds 20
   ```

**Platform Features:**
- ✅ WASAPI for microphone input
- ✅ DirectX 11 backend for GPU-accelerated rendering
- ✅ ImGui-based GUI (no external UI framework needed)
- ✅ Audio playback in synthetic test mode

---

---

## Testing

See [`TESTING.md`](TESTING.md) for detailed test instructions.

**Quick test on macOS:**
```bash
./build/macos-debug/test_transcription \
  tiny.en \
  test_data/Sean_Carroll_podcast_16k.wav \
  --limit-seconds 20
```

**Quick test on Windows:**
```powershell
.\build\windows-debug\test_transcription.exe tiny.en test_data\Sean_Carroll_podcast.wav --limit-seconds 20
```

---

## Troubleshooting

See [`TESTING.md`](TESTING.md) for common issues and solutions.

**Common Issues:**
- Model not found: Run the download scripts in `scripts/`
- Audio device errors: Check microphone permissions (macOS) or device availability (Windows)
- Build errors: Ensure all prerequisites are installed

---

## Development

See [`specs/architecture.md`](specs/architecture.md) for system architecture and design decisions.

**Key Components:**
- `TranscriptionController` - Main streaming transcription API
- `WhisperBackend` - Whisper.cpp integration
- `SpeakerClusterer` - ONNX-based speaker embeddings
- `AudioInputDevice` - Platform-specific audio capture

**Build Presets:**
- `macos-debug` / `macos-release` - macOS Universal builds
- `windows-debug` / `windows-release` - Windows x64 builds

