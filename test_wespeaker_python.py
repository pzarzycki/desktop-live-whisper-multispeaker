"""
Test ONNX speaker embedding model with proper Fbank feature extraction.
This proves the concept before implementing in C++.
"""

import numpy as np
import onnxruntime as ort
import librosa
import soundfile as sf

def extract_fbank_features(audio_path, n_mels=80, n_fft=400, hop_length=160):
    """
    Extract Fbank (mel filterbank) features matching WeSpeaker's expected input.
    
    Parameters:
    - audio_path: Path to audio file
    - n_mels: Number of mel bins (80 for WeSpeaker)
    - n_fft: FFT size (400 samples at 16kHz = 25ms)
    - hop_length: Hop size (160 samples at 16kHz = 10ms)
    
    Returns:
    - features: np.array of shape [1, time_steps, 80]
    """
    # Load audio at 16kHz (WeSpeaker expects 16kHz)
    audio, sr = librosa.load(audio_path, sr=16000, mono=True)
    
    print(f"Audio loaded: {len(audio)} samples, {sr} Hz, {len(audio)/sr:.2f} seconds")
    
    # Extract mel spectrogram (Fbank energies)
    mel_spec = librosa.feature.melspectrogram(
        y=audio,
        sr=sr,
        n_fft=n_fft,
        hop_length=hop_length,
        n_mels=n_mels,
        fmin=0,
        fmax=sr/2,
        power=2.0  # Power spectrum
    )
    
    # Convert to log scale (dB)
    log_mel = librosa.power_to_db(mel_spec, ref=np.max)
    
    print(f"Mel spectrogram shape: {mel_spec.shape} (mels, time_frames)")
    print(f"Log-mel range: [{log_mel.min():.2f}, {log_mel.max():.2f}] dB")
    
    # Transpose to [time, features] and add batch dimension
    features = np.transpose(log_mel).astype(np.float32)
    features = np.expand_dims(features, axis=0)  # Shape: [1, T, 80]
    
    print(f"Final feature shape: {features.shape} (batch, time, mels)")
    
    return features

def test_wespeaker_model(model_path, audio_path):
    """
    Test WeSpeaker ONNX model with proper feature extraction.
    """
    print("=" * 60)
    print("Testing WeSpeaker ONNX Model")
    print("=" * 60)
    
    # Load ONNX model
    print(f"\nLoading model: {model_path}")
    sess = ort.InferenceSession(model_path)
    
    # Print model info
    input_info = sess.get_inputs()[0]
    output_info = sess.get_outputs()[0]
    print(f"Input: {input_info.name}, shape={input_info.shape}, type={input_info.type}")
    print(f"Output: {output_info.name}, shape={output_info.shape}, type={output_info.type}")
    
    # Extract features
    print(f"\nExtracting features from: {audio_path}")
    features = extract_fbank_features(audio_path)
    
    # Run inference
    print("\nRunning ONNX inference...")
    try:
        outputs = sess.run(None, {input_info.name: features})
        embedding = outputs[0][0]  # Shape: [256]
        
        print(f"✅ Inference successful!")
        print(f"Embedding shape: {embedding.shape}")
        print(f"Embedding L2 norm: {np.linalg.norm(embedding):.6f}")
        print(f"Embedding mean: {embedding.mean():.6f}")
        print(f"Embedding std: {embedding.std():.6f}")
        print(f"Embedding range: [{embedding.min():.6f}, {embedding.max():.6f}]")
        
        # Show first 10 dimensions
        print(f"\nFirst 10 dimensions: {embedding[:10]}")
        
        return embedding
        
    except Exception as e:
        print(f"❌ Inference failed: {e}")
        return None

def compare_embeddings(model_path, audio1_path, audio2_path):
    """
    Compare embeddings from two audio files (cosine similarity).
    """
    print("\n" + "=" * 60)
    print("Comparing Two Audio Samples")
    print("=" * 60)
    
    features1 = extract_fbank_features(audio1_path)
    features2 = extract_fbank_features(audio2_path)
    
    sess = ort.InferenceSession(model_path)
    input_name = sess.get_inputs()[0].name
    
    emb1 = sess.run(None, {input_name: features1})[0][0]
    emb2 = sess.run(None, {input_name: features2})[0][0]
    
    # Cosine similarity
    similarity = np.dot(emb1, emb2) / (np.linalg.norm(emb1) * np.linalg.norm(emb2))
    
    print(f"\nCosine similarity: {similarity:.4f}")
    print(f"  > 0.7: Same speaker (high confidence)")
    print(f"  0.5-0.7: Likely same speaker")
    print(f"  0.3-0.5: Uncertain")
    print(f"  < 0.3: Different speakers")
    
    return similarity

if __name__ == "__main__":
    model_path = "models/speaker_embedding.onnx"
    
    # Test with Sean Carroll podcast
    audio_path = "test_data/Sean_Carroll_podcast.wav"
    
    print("Testing with Sean Carroll podcast...")
    embedding = test_wespeaker_model(model_path, audio_path)
    
    if embedding is not None:
        print("\n" + "=" * 60)
        print("SUCCESS! Feature extraction approach works!")
        print("=" * 60)
        print("\nNext steps:")
        print("1. Implement Fbank extraction in C++")
        print("2. Integrate with OnnxSpeakerEmbedder::preprocess_audio()")
        print("3. Test with full diarization pipeline")
