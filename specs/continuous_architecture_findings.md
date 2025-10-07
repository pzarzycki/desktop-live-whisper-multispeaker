# Continuous Streaming Architecture - Findings

## Implementation Summary

Implemented a **continuous streaming architecture** that eliminates fixed window boundaries:

### Architecture
- **Audio buffer**: Grows continuously, no fixed 1.5s windows
- **Speaker tracking**: Sliding 250ms frames analyzed continuously  
- **Segmentation**: Dynamic, triggered by:
  - Speaker change (after 3+ stable frames = 750ms)
  - Pause detection (energy < -50 dBFS)
  - Maximum length (3s to prevent too-long segments)
  - Minimum length (1.2s for Whisper compatibility)

### Key Components

1. **ContinuousSpeakerTracker** (`src/diar/speaker_cluster.hpp/cpp`)
   - Processes audio in sliding 250-300ms frames
   - Computes speaker embeddings per frame
   - Detects speaker changes and pauses
   - No fixed boundaries

2. **Speaker Embeddings** (35-dimensional)
   - 20 MFCCs (increased from 13)
   - 1 pitch (F0) via normalized autocorrelation
   - 3 formants (F1, F2, F3) from spectral peaks
   - 1 energy
   - 10 spectral variance features

3. **Processing Loop** (`src/console/transcribe_file.cpp`)
   - Accumulates audio continuously
   - Tracks speaker on every chunk
   - Transcribes when natural boundary detected
   - Respects Whisper's 1000ms minimum

## Test Results (Sean Carroll Podcast, 20s)

###Expected Output:
```
[S0] what to you is the most beautiful idea in physics?
[S1] conservation of momentum.
[S0] can you elaborate?
[S1] yeah! if you are aristotle, when aristotle wrote his book on physics...
```

### Actual Output (latest test):
```
[S0] what to you is the most beautiful idea in physics conservation of momentum can you elaborate yeah if you are aristotle when aristotle wrote his book on
[S1] it s actually made a falling very obvious point if i push the ball model...
```

### Observations

✅ **Successes:**
- No fixed window artifacts
- No Whisper warnings about too-short segments
- Flexible segmentation adapts to speech rhythm
- Architecture is cleaner and more intuitive

❌ **Problems:**
- **Speaker detection is unstable**: 9+ speaker switches in 10 seconds
- **Embeddings have high variance**: Similarity scores swing wildly (0.354 → 0.945)
- **Multiple utterances grouped together**: First segment contains 4 different utterances by 2 speakers
- **Verbose logs show**: Rapid S0→S1→S0→S1 switching every 750ms-1.5s

### Root Cause Analysis

**The speaker embeddings are not stable enough.** From verbose logs:
```
[Diar] Best: S0 (sim=0.354), Current: S0 (sim=0.354), frames=3
[Diar] Created new speaker S1 (bestSim=0.354 < threshold=0.700)  <- Low similarity creates S1
[Diar] Best: S1 (sim=0.818), Current: S1 (sim=0.818), frames=0
...
[Diar] Best: S0 (sim=0.945), Current: S1 (sim=0.596), frames=3  <- Very high S0 similarity, switches back
[Diar] --> Switched to S0
```

**The embeddings capture TOO MUCH content variation and not enough speaker identity.**

## Why Embeddings Are Unstable

1. **MFCCs are content-dependent**: Different phonemes produce different MFCCs even for same speaker
2. **Pitch varies with prosody**: Questions vs statements have different pitch contours
3. **Formant extraction is noisy**: Simple spectral peak finding is unreliable
4. **Short frames (250ms)**: Not enough data for stable speaker features
5. **No normalization for speaking style**: Emphasis, emotion, speed all affect features

## What's Needed

### Better Speaker Embeddings
Current approach is too simplistic. Need:

1. **Phoneme-invariant features**:
   - Average over longer time windows (1-2s minimum)
   - Focus on statistical distributions, not instantaneous values
   - Delta and delta-delta MFCCs for temporal dynamics

2. **Robust pitch/formant extraction**:
   - Better pitch tracking (YIN algorithm, RAPT)
   - LPC-based formant extraction instead of spectral peaks
   - Pitch range and variance, not just average

3. **Pre-trained speaker embeddings**:
   - Use existing models (x-vectors, d-vectors, ECAPA-TDNN)
   - Trained on thousands of speakers
   - Much more robust than hand-crafted features

### Alternative Approaches

1. **Longer analysis windows**: 1-2s instead of 250ms for more stable embeddings
2. **Speaker verification loss**: Train embeddings to maximize inter-speaker distance
3. **Post-processing**: Smooth speaker assignments with HMM or Viterbi decoding
4. **Hybrid**: Use Whisper's internal features (they're speaker-aware to some extent)

## Conclusion

The **continuous streaming architecture is correct** - it solves the fixed-window problem and allows flexible segmentation.

The **bottleneck is speaker embedding quality**. Hand-crafted acoustic features (MFCCs + pitch + formants) are insufficient for robust speaker discrimination in fast-paced, natural dialogue.

**Next steps:**
1. Increase frame size to 1-2s for more stable features
2. Add delta/delta-delta MFCCs
3. Better pitch/formant extraction algorithms
4. Consider integrating a pre-trained speaker embedding model (e.g., pyannote.audio, SpeechBrain)

## Performance

- Real-time factor: 0.986 (faster than real-time)
- Diarization overhead: 0.136s for 20s audio (0.7% of total time)
- Memory: Bounded (trim old audio after transcription)
- Latency: 1.2-3s depending on when boundary is detected
