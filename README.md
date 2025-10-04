# Desktop Live Whisper (multiplatform) — Windows-first

Minimal Qt Quick desktop app scaffold for live transcription with Whisper and local diarization. This repo is currently in TDD bring-up: failing integration tests are present and the core implementation will follow.

## Prerequisites (Windows)

- CMake 3.24+
- A C++ toolchain. Either:
  - Visual Studio 2022 with "Desktop development with C++" workload, or
  - Visual Studio 2022 Build Tools with MSVC and Windows SDK
- Windows 10/11 SDK (provides the Universal CRT headers like `ucrt\math.h`, `rc.exe`, `mt.exe`)
- Optional: Ninja (faster single-config builds)
- Qt 6 will be pulled via vcpkg later; for now, tests can build without Qt.

Notes:
- The full app presets assume a local `vcpkg/` checkout at `${repo}/vcpkg`. We'll wire this in a later step; for now use the tests-only presets that do not require vcpkg/Qt.



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

