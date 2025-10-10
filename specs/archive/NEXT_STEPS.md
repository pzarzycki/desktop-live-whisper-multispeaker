# Next Steps: Connect Controller to Transcription Engine

## Current Status (2025-10-08)

### ✅ Completed

**Platform Separation:**
- Windows entry point: `src/ui/main_windows.cpp` (DirectX 11)
- macOS entry point: `src/ui/main_macos.mm` (Metal) - ready but untested
- Platform-independent UI: `src/ui/app_window.hpp/cpp`
- CMake properly selects platform-specific code
- DPI-aware rendering with TrueType fonts (Segoe UI on Windows)

**GUI Improvements:**
- Settings moved to separate window (cleaner main UI)
- Settings button in control panel
- Dark theme with proper DPI scaling
- Speaker colors configured

**Controller Framework:**
- `TranscriptionController` class exists in `src/app/transcription_controller.cpp`
- Event subscription system working (chunks, reclassification, status, errors)
- Threading framework in place
- **BUT**: `processing_loop()` is just a stub - doesn't do actual transcription yet

### ⏳ What's Missing

The controller starts a background thread but doesn't do anything. The `processing_loop()` method needs to be implemented with the actual transcription pipeline.

## Implementation Plan: Wire Controller to Engine

### Current Architecture (What Works)

We have working test programs that demonstrate each piece:

1. **`test_word_timestamps.cpp`** - Whisper transcription
   - Loads Whisper model
   - Transcribes audio
   - Returns word-level timestamps
   - **Location:** `apps/test_word_timestamps.cpp`

2. **`test_frame_voting.cpp`** - Speaker diarization  
   - Loads speaker embedding model (WeSpeaker ResNet34)
   - Extracts embeddings for audio frames
   - Does frame voting to assign speakers
   - **Location:** `apps/test_frame_voting.cpp`

3. **Windows WASAPI audio capture**
   - Captures from microphone
   - Resamples to 16kHz mono
   - **Location:** `src/audio/windows_wasapi.cpp`

### What Needs To Happen

**Implement `TranscriptionControllerImpl::processing_loop()` in `src/app/transcription_controller.cpp` (line 406)**

Current stub:
```cpp
void TranscriptionControllerImpl::processing_loop() {
    // TODO: Implement actual processing
    // For now, just emit status periodically
    
    std::cout << "Processing thread started\n";
    
    // ... just sleeps in a loop doing nothing
}
```

Needs to become:
```cpp
void TranscriptionControllerImpl::processing_loop() {
    // 1. Initialize components
    auto whisper = load_whisper_model(config_.whisper_model);
    auto speaker_model = load_speaker_model(config_.speaker_model);
    auto audio_capture = create_audio_capture(selected_device_id_);
    
    // 2. Start audio capture
    audio_capture->start();
    
    // 3. Main processing loop
    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // 4. Get audio chunk (e.g., 3 seconds)
        auto audio_chunk = audio_capture->get_buffer();
        
        // 5. Transcribe with Whisper
        auto whisper_result = whisper->transcribe(audio_chunk);
        
        // 6. Extract speaker embeddings for frames
        auto embeddings = extract_embeddings(audio_chunk, speaker_model);
        
        // 7. Do frame voting to assign speaker
        int speaker_id = vote_on_speaker(embeddings, config_.speaker_threshold);
        
        // 8. Create and emit chunk
        TranscriptionChunk chunk;
        chunk.id = next_chunk_id_++;
        chunk.text = whisper_result.text;
        chunk.speaker_id = speaker_id;
        chunk.timestamp_ms = get_elapsed_ms();
        chunk.speaker_confidence = calculate_confidence(embeddings);
        emit_chunk(chunk);
        
        // 9. Check for reclassification opportunities
        check_for_reclassification();
        
        // 10. Update status
        emit_status(build_status());
    }
    
    audio_capture->stop();
}
```

### Step-by-Step Implementation

#### Step 1: Copy Audio Capture (30 min)

**From:** `apps/test_word_timestamps.cpp` lines 30-60 (audio loading)  
**To:** Create `src/audio/audio_buffer.hpp/cpp` 

Or use existing WASAPI for live capture:
- Already have `src/audio/windows_wasapi.cpp`
- Need to integrate with controller

#### Step 2: Copy Whisper Integration (1 hour)

**From:** `apps/test_word_timestamps.cpp` lines 80-150 (Whisper calls)  
**To:** Create `src/asr/whisper_transcriber.hpp/cpp`

Key code to copy:
```cpp
// Load model
auto whisper_context = whisper_init_from_file(model_path);

// Prepare parameters
whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
params.print_progress = false;
params.print_timestamps = true;
params.token_timestamps = true;
params.language = "en";

// Transcribe
whisper_full(whisper_context, params, audio_data.data(), audio_data.size());

// Extract results
const int n_segments = whisper_full_n_segments(whisper_context);
for (int i = 0; i < n_segments; ++i) {
    const char* text = whisper_full_get_segment_text(whisper_context, i);
    // ...
}
```

#### Step 3: Copy Speaker Embedding (1 hour)

**From:** `apps/test_frame_voting.cpp` lines 100-250 (embedding extraction)  
**To:** Create `src/diarization/speaker_embedder.hpp/cpp`

Key code to copy:
```cpp
// Load ONNX model
Ort::Session session(env, model_path, session_options);

// Extract mel features
std::vector<float> mel_features = extract_mel_features(audio, sample_rate);

// Prepare input tensor
std::vector<int64_t> input_shape = {1, num_frames, 80};
auto input_tensor = Ort::Value::CreateTensor<float>(..., mel_features.data(), ...);

// Run inference
auto output_tensors = session.Run(..., {&input_tensor}, ...);

// Get embeddings
float* embeddings = output_tensors[0].GetTensorMutableData<float>();
```

#### Step 4: Copy Frame Voting (1 hour)

**From:** `apps/test_frame_voting.cpp` lines 300-400 (voting logic)  
**To:** Create `src/diarization/speaker_classifier.hpp/cpp`

Key code to copy:
```cpp
float cosine_similarity(const std::vector<float>& a, const std::vector<float>& b);

int classify_speaker(const std::vector<float>& embedding, 
                     const std::vector<SpeakerProfile>& profiles,
                     float threshold) {
    float best_score = -1.0f;
    int best_speaker = -1;
    
    for (int i = 0; i < profiles.size(); ++i) {
        float score = cosine_similarity(embedding, profiles[i].centroid);
        if (score > best_score) {
            best_score = score;
            best_speaker = i;
        }
    }
    
    if (best_score < threshold) {
        // Create new speaker
        return create_new_speaker(embedding);
    }
    
    return best_speaker;
}
```

#### Step 5: Wire It All Together (2 hours)

Update `processing_loop()` to use all the pieces:

```cpp
void TranscriptionControllerImpl::processing_loop() {
    // Initialize
    WhisperTranscriber whisper(config_.whisper_model);
    SpeakerEmbedder embedder(config_.speaker_model);
    SpeakerClassifier classifier(config_.max_speakers, config_.speaker_threshold);
    
    // For synthetic audio testing
    if (config_.use_synthetic_audio) {
        auto audio = load_wav_file(config_.synthetic_audio_file);
        process_audio_buffer(audio, whisper, embedder, classifier);
        return;
    }
    
    // For live microphone (future)
    #ifdef _WIN32
    WindowsWASAPI audio_capture(selected_device_id_);
    audio_capture.start();
    
    while (running_) {
        auto buffer = audio_capture.get_buffer(3000); // 3 seconds
        process_audio_buffer(buffer, whisper, embedder, classifier);
    }
    #endif
}

void TranscriptionControllerImpl::process_audio_buffer(
    const std::vector<float>& audio,
    WhisperTranscriber& whisper,
    SpeakerEmbedder& embedder,
    SpeakerClassifier& classifier) {
    
    // Transcribe
    auto segments = whisper.transcribe(audio);
    
    // Get speaker embeddings
    auto embeddings = embedder.extract(audio);
    
    // Classify speaker
    int speaker_id = classifier.classify(embeddings);
    
    // Emit chunks
    for (const auto& segment : segments) {
        TranscriptionChunk chunk;
        chunk.id = next_chunk_id_++;
        chunk.text = segment.text;
        chunk.speaker_id = speaker_id;
        chunk.timestamp_ms = get_elapsed_ms() + segment.start_ms;
        chunk.duration_ms = segment.end_ms - segment.start_ms;
        chunk.speaker_confidence = classifier.get_confidence();
        emit_chunk(chunk);
    }
}
```

### Testing Plan

#### Phase 1: Synthetic Audio (Test with `test_data/Sean_Carroll_podcast.wav`)

1. Set "Use Synthetic Audio" in settings
2. Point to test .wav file
3. Click "START RECORDING"
4. Should see transcripts appearing in real-time
5. Should see speaker colors (S0/S1) correctly assigned

#### Phase 2: Live Microphone

1. Uncheck "Use Synthetic Audio"
2. Click "START RECORDING"
3. Speak into microphone
4. Should see live transcription

### Estimated Time

- Step 1 (Audio): 30 min
- Step 2 (Whisper): 1 hour
- Step 3 (Embeddings): 1 hour  
- Step 4 (Voting): 1 hour
- Step 5 (Integration): 2 hours
- **Total: ~6 hours**

### CMake Updates Needed

Add the new libraries to `CMakeLists.txt`:

```cmake
# Diarization library
add_library(diarization STATIC
    src/diarization/speaker_embedder.cpp
    src/diarization/speaker_classifier.cpp
    src/diarization/mel_features.cpp  # Copy from test_frame_voting
)
target_link_libraries(diarization PUBLIC onnxruntime)

# Update app_controller to link diarization
target_link_libraries(app_controller PUBLIC 
    audio_windows 
    asr_whisper 
    diarization
)
```

### Success Criteria

✅ Click START → See "Recording..." status  
✅ Transcripts appear in real-time in GUI  
✅ Speaker colors (blue/red) change correctly  
✅ Status bar shows elapsed time, chunk count  
✅ Confidence values display  
✅ Reclassification events work  
✅ STOP button stops transcription cleanly  

### Files to Reference

**Working examples:**
- `apps/test_word_timestamps.cpp` - Whisper integration
- `apps/test_frame_voting.cpp` - Speaker diarization
- `src/audio/windows_wasapi.cpp` - Audio capture

**To be implemented:**
- `src/app/transcription_controller.cpp` line 406 - `processing_loop()`
- `src/diarization/*.cpp` - Speaker embedding/classification (copy from test files)
- `src/asr/whisper_transcriber.cpp` - Whisper wrapper (copy from test files)

## Quick Start (For Next Session)

1. Create `src/diarization` directory structure
2. Copy mel features code from `test_frame_voting.cpp`
3. Copy speaker embedding code
4. Copy frame voting logic
5. Create Whisper wrapper in `src/asr`
6. Wire it all into `processing_loop()`
7. Update CMakeLists.txt
8. Test with Sean Carroll podcast!

That's it! The GUI is ready, the controller framework is ready, just need to fill in the actual transcription logic by copying from the working test files.
