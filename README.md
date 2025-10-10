# Desktop Live Whisper - Multiplatform

Real-time speech transcription with speaker diarization for Windows and macOS.

## Features

- ✅ **Real-time transcription** with OpenAI Whisper (tiny.en model)
- ✅ **Speaker diarization** - identifies who spoke when
- ✅ **Low latency** - first transcription in ~4 seconds
- ✅ **Production-ready** - async streaming architecture, thoroughly tested
- 🔧 **Multiplatform** - Windows (complete), macOS (in progress)

## Performance

- **Processing speed:** 0.87x realtime (faster than audio playback)
- **First output:** ~4 seconds from start
- **Memory usage:** ~320 MB
- **Audio quality:** Zero stuttering, zero dropped frames

**Note:** Speaker identification is functional but accuracy needs improvement with better embedding models.

## Quick Start

See detailed setup instructions in [`docs/`](docs/) folder.

**For Developers:** See [`specs/plan.md`](specs/plan.md) for project status and [`specs/architecture.md`](specs/architecture.md) for technical details.

## Prerequisites (Windows)

### Core Build Tools

- CMake 3.24+
- A C++ toolchain. Either:
  - Visual Studio 2022 with "Desktop development with C++" workload, or
  - Visual Studio 2022 Build Tools with MSVC and Windows SDK
- Windows 10/11 SDK (provides the Universal CRT headers like `ucrt\math.h`, `rc.exe`, `mt.exe`)
- Optional: Ninja (faster single-config builds)

### Qt 6 (Required for GUI Application)

**Download:** Visit [www.qt.io/download-qt-installer](https://www.qt.io/download-qt-installer) and download the Qt Online Installer.

**Quick Setup:**
1. Run `qt-unified-windows-x64-online.exe`
2. Select Qt 6.8.0 → MSVC 2022 64-bit + Qt Quick
3. Install to `C:\Qt` (default)

**Or Automated CLI:**
```powershell
# From Downloads folder after downloading installer
.\qt-unified-windows-x64-online.exe --root C:\Qt --accept-licenses --default-answer --confirm-command install qt.qt6.680.win64_msvc2022_64 qt.qt6.680.addons.qtdeclarative
```

**Full Instructions:** See [docs/QUICK_START_GUI.md](docs/QUICK_START_GUI.md) for step-by-step installation and [docs/qt_setup.md](docs/qt_setup.md) for detailed configuration.

**Note:** Tests can build without Qt (use `tests-only-*` presets). GUI app requires Qt 6 with QML/Quick support.



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

