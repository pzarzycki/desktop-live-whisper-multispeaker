# Desktop Live Whisper (Multiplatform) Constitution
<!--
Sync Impact Report
- Version change: N/A → 1.0.0
- Modified principles: N/A (initial adoption)
- Added sections: Core Principles; Technology & Architecture Constraints; Development Workflow & Quality Gates; Governance
- Removed sections: None
- Templates requiring updates:
	✅ .specify/templates/plan-template.md (path + version reference)
	✅ .specify/templates/spec-template.md (reviewed, no changes needed)
	✅ .specify/templates/tasks-template.md (C++/Qt-focused examples)
	✅ .github/prompts/constitution.prompt.md (reviewed, no agent-specific coupling issues)
- Follow-up TODOs: None
-->

## Core Principles

### I. Real-time Audio First (NON-NEGOTIABLE)

MUST capture and process live microphone audio with low end-to-end latency suitable for
continuous transcription.

- Capture buffers MUST be small enough for near-real-time updates (target ≤ 50 ms per chunk).
- Audio frames MUST stream incrementally into the Whisper backend (no full-file prerequisite).
- The pipeline MUST tolerate device changes, sample-rate drift, dropouts, and buffer underruns;
  recovery MUST be automatic without crashes.
- Target interactive latency: p95 partial transcript update ≤ 300 ms on a typical Windows dev
  machine; document actual measurements.

Rationale: The primary user value is live UX—any feature that harms real-time performance is a
regression.

### II. Ship Vertical Slices Fast

Bias to working end-to-end prototypes. New capabilities MUST land as vertical slices (Mic → ASR → UI)
within short cycles.

- Each feature MUST produce a usable vertical slice within two working days where feasible.
- Shortcuts MAY be used behind feature flags and MUST be documented with a TODO in the plan.
- Keep pull requests small (prefer ≤ 300 lines changed) or split by slice.

Rationale: Speed to feedback is critical for audio UX and model tuning.

### III. Pragmatic Tests Where It Counts

Favor a small, targeted test suite over exhaustive coverage.

- REQUIRED minimum for every feature: (1) build verification, (2) a smoke test that captures 1s of
  audio and passes it through a mock or tiny model path, and (3) an integration test covering the
  inference loop with a canned WAV.
- Unit tests SHOULD cover hot paths (buffers, resamplers, threading invariants) when feasible.
- CI MUST run on Windows. macOS checks MAY be deferred until the secondary platform phase.
- Tests MUST be fast (aim < 60s for the suite) and reliable.

Rationale: We protect user-visible behavior and performance without slowing iteration.

### IV. Multiplatform by Design, Windows First in Practice

Design for portability, deliver on Windows first.

- UI MUST use Qt 6; platform code MUST be isolated behind thin adapters (e.g., `platform/windows`,
  `platform/macos`).
- No feature may block Windows delivery; macOS parity is tracked and delivered subsequently.
- Platform conditionals MUST be confined to well-defined boundaries (audio I/O, device discovery,
  file pickers, system permissions).

Rationale: Windows is the primary development platform; clean boundaries make later macOS work
predictable.

### V. Simplicity and Performance Discipline

Prefer a small toolkit, measure before optimizing, and keep the runtime lean.

- Language: C++20; Build: CMake; UI: Qt 6; Package management: vcpkg (Windows).
- Prefer local inference backends (e.g., whisper.cpp with GGML/GGUF models); cloud calls require an
  explicit product need and a toggle.
- Logging MUST be lightweight and structured; avoid noisy logs in the real-time path. Use a ring
  buffer or sampling if needed.
- Feature flags and configuration MUST be centralized and discoverable.

Rationale: A smaller system is easier to ship fast and keep responsive.

## Technology & Architecture Constraints

- Language & Build: C++20, CMake ≥ 3.24. Use MSVC v143 on Windows; Clang or AppleClang on macOS.
- UI: Qt 6.6+ (Widgets or QML—choose per feature, but keep one across the app unless justified).
- Audio I/O: Windows = WASAPI; macOS = CoreAudio (secondary phase). Keep an abstraction to hide
  backend differences.
- Model Runtime: Prefer whisper.cpp (static or submodule) with GGUF models. Support streaming
  inference. Persist model selection and basic settings.
- Concurrency: Audio callback path MUST be real-time safe (no blocking allocations or disk I/O).
  Use lock-free queues/ring buffers between audio and inference/UI threads.
- Observability: Structured logs; optional on-screen perf HUD (buffer fill %, chunk times, ASR
  latency). Logging overhead in the callback path must be negligible.
- Packaging & Assets: Manage model files with Git LFS or runtime download; never commit large
  binaries directly. Config lives in a simple human-readable file.
- Security & Privacy: Mic access prompts follow OS guidance. Audio stays local unless a user opts in
  to a remote service.

## Development Workflow & Quality Gates

- Branching: `[###-feature-name]`. Small, focused PRs.
- Formatting & Style: clang-format; consider clang-tidy for critical paths. Enforce in CI.
- CI (Windows primary):
  - Configure, build, and run the smoke + integration tests.
  - Run a micro-benchmark on the inference loop; fail PR if a defined regression budget is exceeded
    without an explicit waiver.
- Minimal Required Tests per feature:
  1) Build verification
  2) Smoke test: mic capture → mock/tiny ASR
  3) Integration: canned WAV → transcription
- Performance Budget: Document p95 end-to-end latency and keep a running history.
- Release & Models: Tag app releases with SemVer. Model downloads/versioning tracked in release
  notes.

## Governance

- Authority: This Constitution supersedes other process docs for this repository.
- Constitution Check: All plans MUST include a Constitution Check section that enumerates known
  risks against these principles (e.g., Windows-first, latency budgets, test minimums, Qt 6 usage).
  Violations require a justification in “Complexity Tracking.”
- Amendments: Propose via PR that updates this file and any affected templates. Include rationale
  and migration/cleanup steps. Approval by code owners required.
- Versioning Policy (for this document): Semantic Versioning
  - MAJOR: Backward-incompatible governance changes (e.g., removing or redefining principles)
  - MINOR: New principles/sections or materially expanded guidance
  - PATCH: Editorial clarifications and non-semantic tweaks
- Compliance Reviews: Reviewers MUST verify Constitution Check, required tests, and performance
  notes before merging. Exceptions MUST be explicitly documented and time-boxed.

**Version**: 1.0.0 | **Ratified**: 2025-10-03 | **Last Amended**: 2025-10-03
