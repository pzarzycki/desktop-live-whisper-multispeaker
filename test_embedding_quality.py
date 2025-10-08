"""
Quick test to verify mel feature extraction produces reasonable embeddings
"""
import numpy as np
import onnxruntime as ort
import librosa

# Test 1: Extract features using our Python implementation (reference)
audio, sr = librosa.load('test_data/Sean_Carroll_podcast.wav', sr=16000, mono=True, duration=2.0)
print(f"Audio: {len(audio)} samples, {sr} Hz")

# Extract mel features
mel_spec = librosa.feature.melspectrogram(
    y=audio, sr=sr, n_fft=400, hop_length=160, n_mels=80, fmin=0, fmax=8000
)
log_mel = librosa.power_to_db(mel_spec, ref=np.max)
features = np.transpose(log_mel).astype(np.float32)
features = np.expand_dims(features, 0)

print(f"Features shape: {features.shape}")
print(f"Feature range: [{features.min():.2f}, {features.max():.2f}] dB")

# Run ONNX model
sess = ort.InferenceSession('models/speaker_embedding.onnx')
output = sess.run(None, {'feats': features})
embedding = output[0][0]

print(f"\nEmbedding shape: {embedding.shape}")
print(f"Embedding L2 norm: {np.linalg.norm(embedding):.4f}")
print(f"Embedding mean: {embedding.mean():.4f}")
print(f"Embedding std: {embedding.std():.4f}")
print(f"First 10 dims: {embedding[:10]}")

# Test 2: Extract from two different speakers (if alternating)
print("\n" + "="*60)
print("Testing speaker similarity")
print("="*60)

# Get two segments
audio1, _ = librosa.load('test_data/Sean_Carroll_podcast.wav', sr=16000, mono=True, offset=1.0, duration=1.0)
audio2, _ = librosa.load('test_data/Sean_Carroll_podcast.wav', sr=16000, mono=True, offset=10.0, duration=1.0)

def get_embedding(audio):
    mel = librosa.feature.melspectrogram(y=audio, sr=16000, n_fft=400, hop_length=160, n_mels=80, fmin=0, fmax=8000)
    log_mel = librosa.power_to_db(mel, ref=np.max)
    feat = np.transpose(log_mel).astype(np.float32)[np.newaxis, :]
    return sess.run(None, {'feats': feat})[0][0]

emb1 = get_embedding(audio1)
emb2 = get_embedding(audio2)

similarity = np.dot(emb1, emb2) / (np.linalg.norm(emb1) * np.linalg.norm(emb2))
print(f"Segment 1 (t=1s) vs Segment 2 (t=10s)")
print(f"Cosine similarity: {similarity:.4f}")
print(f"  Expected: < 0.7 if different speakers, > 0.7 if same speaker")
