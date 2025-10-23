# Archived Tests

This folder contains experimental tests from earlier development phases. These are kept for historical reference but are not actively maintained or built.

## Diarization Research Tests (Phase 2-3, 2024)

These tests were used during speaker diarization algorithm development:

- **test_boundary_detection.cpp** - Experiments with pause detection and segment boundaries
- **test_frame_voting.cpp** - Frame-level speaker voting approach testing
- **test_segment_speakers.cpp** - Segment-level speaker assignment experiments
- **test_word_clustering.cpp** - Word-level speaker clustering (v1)
- **test_word_clustering_v2.cpp** - Word-level speaker clustering (v2, improved)
- **test_word_speaker_mapping.cpp** - Mapping words to speakers in transcription
- **test_word_timestamps.cpp** - Word-level timestamp extraction from Whisper
- **test_embedding_quality.cpp** - Speaker embedding quality analysis

**Outcome:** Research led to current production implementation using ONNX embeddings with frame-level analysis.

## Legacy Console App

- **transcribe_file.cpp** - Original monolithic console app (pre-controller architecture)
  - Combined audio capture, transcription, and diarization in one file
  - Windows-specific (WASAPI hardcoded)
  - Replaced by: `test_transcription` using `TranscriptionController`

## Current Active Tests

See `apps/` folder for active tests:
- **test_transcription** - Primary integration test (uses TranscriptionController)
- **test_audio_device** - Audio input debugging
- **test_controller_api** - Legacy API test (deprecated)
