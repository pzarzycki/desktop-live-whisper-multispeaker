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

5. **Run test:**
   ```bash
   ./build/macos-debug/test_transcription \
     models/ggml-tiny.en.bin \
     test_data/Sean_Carroll_podcast.wav
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

**Prerequisites:** Make sure you have Visual Studio Build Tools and CMake installed.

**Build Commands:**

```powershell
# Configure
cmake --preset windows-debug  # or windows-release

# Build
cmake --build build/windows-debug -j 4

# Run GUI application
.\build\windows-debug\app_desktop_whisper.exe

# Run tests
.\build\windows-debug\test_transcription.exe models\ggml-tiny.en.bin test_data\Sean_Carroll_podcast.wav
```

**Note:** GUI uses Dear ImGui with DirectX 11 backend (no external dependencies needed).

---

## Configure and run tests-only (no Qt)

This path configures only the failing integration tests to drive TDD.

1. Configure

   - Use the Visual Studio generator preset:
     - `cmake --preset tests-only-debug`

2. Build

   - `cmake --build --preset build-tests-only-debug`

3. Run tests

   - `ctest --test-dir build/tests-only-debug -C Debug --output-on-failure`

Expected: tests currently fail (they return exit code 1 by design). We'll implement features to turn them green.

## Configure full app (Qt via vcpkg, pending)

Once vcpkg is available at `${repo}/vcpkg`:

1. Configure

- `cmake --preset windows-debug`

1. Build

- `cmake --build --preset build-debug`

1. Run app

- Executable will be under `build/windows-debug`.

If you don't have `vcpkg` yet, clone it into the repo root:

- `git clone https://github.com/microsoft/vcpkg.git vcpkg`

## Troubleshooting


## Next steps


### Whisper (Option B: vendored third_party)

1. Add whisper.cpp as a submodule:

   - `git submodule add https://github.com/ggerganov/whisper.cpp third_party/whisper.cpp`
   - `git submodule update --init --recursive`

2. Download a GGUF model and place under `models/`, e.g.:

   - `models/small.en.gguf` (recommended to start)

3. Build console transcriber (tests-only preset is fine):

   - `cmake --preset tests-only-debug`
   - `cmake --build --preset build-tests-only-debug`

4. Run with a specific device ID:

    - List devices (if a device lister is present):
       - app_list_devices.exe
    - Example run (mic mode, 10s window):
       - app_transcribe_file.exe --device "{0.0.1.00000000}.{8d279ef3-e64f-477d-9aab-c253a44360ea}" --limit-seconds 10 --model third_party/whisper.cpp/models/ggml-small.en.bin

    Preferred test microphone (saved in test_data/preferred_mic.txt):
    - 2: Desktop Microphone (Microsoft® LifeCam HD-3000)
    - ID: {0.0.1.00000000}.{8d279ef3-e64f-477d-9aab-c253a44360ea}

