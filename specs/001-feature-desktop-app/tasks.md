# Tasks: Desktop live transcription with speaker diarization and color‑coded UI

**Input**: Design documents from `C:/PRJ/VAM/desktop-live-whisper-multiplatform/specs/001-feature-desktop-app/`  
**Prerequisites**: spec.md (required), plan.md

## Execution Flow (main)

```text
1. Load plan.md from feature directory
   → Extract: tech stack, libraries, structure
2. Load optional design documents (if present): data-model.md, contracts/, research.md, quickstart.md
3. Generate tasks by category (Setup, Tests, Core, Integration, Polish)
4. Apply task rules (TDD, parallelizable [P] when different files)
5. Number tasks sequentially (T001, T002...)
6. Provide dependency graph and parallel examples
7. Return: SUCCESS (tasks ready for execution)
```

## Path Conventions

- Source: `src/` at repository root
- Desktop app (Qt6): `src/audio/`, `src/asr/`, `src/ui/`, `src/core/`
- Tests: `tests/build/`, `tests/integration/`, `tests/unit/`, `tests/assets/`

## Phase 3.1: Setup (C++/Qt6)

- [ ] T001 Create CMake project at repo root with C++20, MSVC v143, CMake ≥3.24 in `CMakeLists.txt`
  - Root targets: `app_desktop_whisper`, `asr_whisper`, `audio_windows`, `core_lib`, `ui_qml`
  - Enable `/permissive-`, `/std:c++20`, warnings-as-errors in CI config block
- [ ] T002 Integrate vcpkg manifest at `vcpkg.json` with deps: `qtbase`, `qtdeclarative`, `onnxruntime`, `fmt`
- [ ] T003 [P] Add whisper.cpp as git submodule under `third_party/whisper.cpp/` and wire CMake target `whisper` (static)
- [ ] T004 [P] Configure clang-format (LLVM style, project file at `.clang-format`) and optional clang-tidy preset
- [ ] T005 Add CMake presets for Windows dev in `CMakePresets.json` (Debug, Release) using vcpkg toolchain
- [ ] T006 Create initial source tree:
  - `src/audio/windows_wasapi.cpp`, `src/audio/windows_wasapi.hpp`
  - `src/core/ring_buffer.hpp`, `src/core/logging.cpp`, `src/core/logging.hpp`, `src/core/config.cpp`, `src/core/config.hpp`
  - `src/asr/whisper_backend.cpp`, `src/asr/whisper_backend.hpp`
  - `src/asr/diarization_backend.cpp`, `src/asr/diarization_backend.hpp`
  - `src/ui/main.cpp`, `src/ui/qml/Main.qml`
- [ ] T007 Create test scaffolding folders and a small WAV asset placeholder: `tests/build/`, `tests/integration/`, `tests/unit/`, `tests/assets/sample.wav` (1–2s 16 kHz mono speech)

## Phase 3.2: Tests First (TDD) ⚠️ MUST COMPLETE BEFORE 3.3

CRITICAL: These tests MUST be written and MUST FAIL before ANY implementation

- [ ] T008 [P] Build verification test in `tests/build/build_verification.cmake`
  - Configure, build, and link all targets; verify Qt, whisper.cpp, onnxruntime linkage present
- [ ] T009 [P] Smoke test: capture 1s mic audio and pass through mock/tiny path in `tests/integration/smoke_mic_to_mock.cpp`
  - Bypass ASR; validate capture → buffer → sink pipeline works and returns non-empty PCM frames
- [ ] T010 [P] Integration test: WAV → transcript in `tests/integration/wav_to_transcript.cpp`
  - Load `tests/assets/sample.wav` → run through Whisper small/base (configurable) → expect non-empty transcript text
- [ ] T011 [P] Integration test: diarization segmentation in `tests/integration/diarization_segments.cpp`
  - Using a two-speaker test WAV (placeholder) validate ≥2 distinct speaker segments are produced with labels `Speaker N`

## Phase 3.3: Core Implementation (ONLY after tests are failing)

- [ ] T012 [P] Implement ring buffer (SPSC lock-free) `src/core/ring_buffer.hpp`
- [ ] T013 [P] Implement Windows WASAPI shared capture (default input) `src/audio/windows_wasapi.cpp`
  - Callback pushes 16 kHz mono frames into ring buffer; no blocking allocations in callback
- [ ] T014 [P] Implement Whisper backend wrapper `src/asr/whisper_backend.cpp`
  - Load small (default) or base models (GGUF); streaming partial results callback API
- [ ] T015 [P] Implement diarization backend `src/asr/diarization_backend.cpp`
  - Local embeddings (e.g., ECAPA on ONNX Runtime) + incremental clustering API returning `Speaker N` labels
- [ ] T016 [P] Implement logging (low overhead) `src/core/logging.cpp` and config `src/core/config.cpp`
  - Centralize feature flags: model choice (small/base), HUD on/off, device selection (future)
- [ ] T017 Wire pipeline in app entry: `src/ui/main.cpp`
  - Initialize audio capture → ring buffer → whisper backend; attach diarization to segment stream
- [ ] T018 Create QML UI with live transcript `src/ui/qml/Main.qml`
  - Display color-coded `Speaker N` segments; auto-scroll newest line; adjustable font size

## Phase 3.4: Integration

- [ ] T019 Connect UI to backends via Qt signals/slots or QAbstractListModel
- [ ] T020 Performance HUD (optional): buffer fill %, chunk/inference times (toggle via config)
- [ ] T021 Model management: document manual placement of GGUF models and onnx model file(s) for diarization
- [ ] T022 Error handling: device missing/denied; show clear message; stable stop/start behavior

## Phase 3.5: Polish

- [ ] T023 [P] Unit tests for ring buffer and resampler in `tests/unit/test_ring_buffer.cpp`
- [ ] T024 Performance tests: p95 end-to-end latency budget checks in `tests/integration/perf_latency.cpp`
- [ ] T025 [P] Write quickstart at `specs/001-feature-desktop-app/quickstart.md` (Windows-only v1)
  - Steps: obtain models, build with CMake, run, toggle Listening, known limitations (session-only)
- [ ] T026 Remove duplication and dead code across modules
- [ ] T027 Run manual-testing checklist `specs/001-feature-desktop-app/manual-testing.md` (create with basic flows)

## Dependencies

- Setup (T001–T007) before tests (T008–T011)
- Tests (T008–T011) before implementation (T012–T018)
- T012 blocks T013, T017
- T013 blocks T017
- T014 blocks T017, T019
- T015 blocks T019
- Implementation before integration (T019–T022) and polish (T023–T027)

## Parallel Example

```text
# Launch tests-first together once setup is done:
Task: "Build verification in tests/build/build_verification.cmake"
Task: "Smoke mic→mock in tests/integration/smoke_mic_to_mock.cpp"
Task: "WAV→transcript in tests/integration/wav_to_transcript.cpp"
Task: "Diarization segments in tests/integration/diarization_segments.cpp"
```

## Notes

- [P] tasks = different files, no shared state
- Verify tests fail before implementing (TDD)
- Windows-first; macOS is deferred per constitution
- Local-only processing in v1; no persistence/export in v1
