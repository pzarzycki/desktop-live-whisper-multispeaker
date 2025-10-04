# Tasks: [FEATURE NAME]

**Input**: Design documents from `/specs/[###-feature-name]/`
**Prerequisites**: plan.md (required), research.md, data-model.md, contracts/

## Execution Flow (main)
```
1. Load plan.md from feature directory
   → If not found: ERROR "No implementation plan found"
   → Extract: tech stack, libraries, structure
2. Load optional design documents:
   → data-model.md: Extract entities → model tasks
   → contracts/: Each file → contract test task
   → research.md: Extract decisions → setup tasks
3. Generate tasks by category:
   → Setup: project init, dependencies, linting
   → Tests: contract tests, integration tests
   → Core: models, services, CLI commands
   → Integration: DB, middleware, logging
   → Polish: unit tests, performance, docs
4. Apply task rules:
   → Different files = mark [P] for parallel
   → Same file = sequential (no [P])
   → Tests before implementation (TDD)
5. Number tasks sequentially (T001, T002...)
6. Generate dependency graph
7. Create parallel execution examples
8. Validate task completeness:
   → All contracts have tests?
   → All entities have models?
   → All endpoints implemented?
9. Return: SUCCESS (tasks ready for execution)
```

## Format: `[ID] [P?] Description`
- **[P]**: Can run in parallel (different files, no dependencies)
- Include exact file paths in descriptions

## Path Conventions

- **Single project**: `src/`, `tests/` at repository root
- **Desktop app (Qt6)**: `src/audio/`, `src/asr/`, `src/ui/`, `src/core/`
- Paths shown below assume single project - adjust based on plan.md structure

## Phase 3.1: Setup (C++/Qt6)

- [ ] T001 Create project structure per implementation plan
- [ ] T002 Initialize CMake project (C++20) and Qt6 dependencies
- [ ] T003 [P] Configure clang-format and optional clang-tidy

## Phase 3.2: Tests First (TDD) ⚠️ MUST COMPLETE BEFORE 3.3

CRITICAL: These tests MUST be written and MUST FAIL before ANY implementation

- [ ] T004 [P] Build verification test in tests/build/build_verification.cmake
- [ ] T005 [P] Smoke test: capture 1s mic audio and pass through mock/tiny path in tests/integration/smoke_mic_to_mock.cpp
- [ ] T006 [P] Integration test: canned WAV → inference loop → transcript in tests/integration/wav_to_transcript.cpp

## Phase 3.3: Core Implementation (ONLY after tests are failing)

- [ ] T008 [P] Audio device abstraction (WASAPI) in src/audio/windows_wasapi.cpp
- [ ] T009 [P] Lock-free ring buffer between audio and inference in src/core/ring_buffer.hpp
- [ ] T010 [P] Whisper backend integration (whisper.cpp) in src/asr/whisper_backend.cpp
- [ ] T011 UI: Qt6 main window with live transcript view in src/ui/main_window.cpp
- [ ] T012 Configuration and feature flags in src/core/config.cpp
- [ ] T013 Structured logging with low overhead in src/core/logging.cpp

## Phase 3.4: Integration

- [ ] T015 Wire audio → buffer → whisper backend → UI update loop
- [ ] T016 Performance HUD (buffer fill %, chunk/inference times)
- [ ] T017 Model management (download/select GGUF)
- [ ] T018 Error handling and recovery on device changes

## Phase 3.5: Polish

- [ ] T019 [P] Unit tests for ring buffer and resampler in tests/unit/test_ring_buffer.cpp
- [ ] T020 Performance tests: p95 latency budget checks
- [ ] T021 [P] Update docs/quickstart.md for model setup and hotkeys
- [ ] T022 Remove duplication and dead code
- [ ] T023 Run manual-testing.md

## Dependencies

- Tests (T004-T007) before implementation (T008-T014)
- T008 blocks T009, T015
- T016 blocks T018
- Implementation before polish (T019-T023)

## Parallel Example

```text
# Launch T004-T006 together:
Task: "Build verification in tests/build/build_verification.cmake"
Task: "Smoke mic→mock in tests/integration/smoke_mic_to_mock.cpp"
Task: "WAV→transcript in tests/integration/wav_to_transcript.cpp"
```

## Notes

- [P] tasks = different files, no dependencies
- Verify tests fail before implementing
- Commit after each task
- Avoid: vague tasks, same file conflicts

## Task Generation Rules

Applied during main() execution

1. **From Contracts**:
   - Each contract file → contract test task [P]
   - Each endpoint → implementation task
   
2. **From Data Model**:
   - Each entity → model creation task [P]
   - Relationships → service layer tasks
   
3. **From User Stories**:
   - Each story → integration test [P]
   - Quickstart scenarios → validation tasks

4. **Ordering**:
   - Setup → Tests → Models → Services → Endpoints → Polish
   - Dependencies block parallel execution

## Validation Checklist

GATE: Checked by main() before returning

- [ ] All contracts have corresponding tests
- [ ] All entities have model tasks
- [ ] All tests come before implementation
- [ ] Parallel tasks truly independent
- [ ] Each task specifies exact file path
- [ ] No task modifies same file as another [P] task
