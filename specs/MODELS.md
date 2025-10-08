# Model Download Instructions

The following large files are **NOT included in git** and must be downloaded separately:

## Whisper Models

Download from: https://huggingface.co/ggerganov/whisper.cpp

Place in `models/` directory:

- **tiny.en** (recommended, 75 MB):
  ```bash
  # Download ggml-tiny.en.bin
  wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin -O models/ggml-tiny.en.bin
  ```

- **base.en** (better quality, 142 MB):
  ```bash
  # Download ggml-base.en-q5_1.bin
  wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en-q5_1.bin -O models/ggml-base.en-q5_1.bin
  ```

- **small.en** (offline processing, 465 MB):
  ```bash
  # Download small.en.bin (not recommended for real-time)
  wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin -O models/small.en.bin
  ```

## Speaker Embedding Models

### WeSpeaker ResNet34 (current)

Download from: https://github.com/wenet-e2e/wespeaker

```bash
# Download speaker_embedding.onnx (25 MB)
wget https://github.com/wenet-e2e/wespeaker/releases/download/v1.0.0/voxceleb_resnet34.onnx -O models/speaker_embedding.onnx
```

Or use the provided script:
```powershell
.\scripts\download_speaker_model.ps1
```

## Third-Party Libraries

### OpenBLAS (optional, for faster Whisper inference)

Download from: https://github.com/xianyi/OpenBLAS/releases

Place in `third_party/openblas/`:
- `lib/libopenblas.dll.a` (linking)
- `bin/libopenblas.dll` (runtime)
- `include/` (headers)

### ONNX Runtime

Download from: https://github.com/microsoft/onnxruntime/releases

Version: 1.20.1, Windows x64

Place in `third_party/onnxruntime/`:
- `lib/onnxruntime.lib` (linking)
- `bin/onnxruntime.dll` (runtime, 11.6 MB)
- `bin/onnxruntime_providers_shared.dll`
- `bin/libopenblas.dll`
- `include/onnxruntime/` (headers)

## Quick Setup

Use the provided script to download all required files:

```powershell
.\scripts\setup_models_and_libs.ps1
```

## Why Not in Git?

These files are excluded from git because:
- **Large size** (total ~900 MB would make repository huge)
- **GitHub limits** (100 MB per file hard limit)
- **Easy to download** (available from official sources)
- **Frequent updates** (models improve over time)
- **Optional** (different users may need different models)

## See Also

- `specs/architecture.md` - System architecture and requirements
- `specs/transcription.md` - Whisper model selection guide
- `specs/diarization.md` - Speaker embedding model details
