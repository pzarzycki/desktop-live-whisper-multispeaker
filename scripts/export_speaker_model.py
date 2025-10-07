#!/usr/bin/env python3
"""
Export SpeechBrain ECAPA-TDNN speaker embedding model to ONNX format.

Requirements:
    pip install torch torchaudio speechbrain onnx
"""

import torch
import torch.nn as nn
import sys
import os

def export_ecapa_to_onnx(output_path="models/speaker_embedding.onnx"):
    """Export ECAPA-TDNN model to ONNX format."""
    
    print("Loading SpeechBrain ECAPA-TDNN model...")
    try:
        from speechbrain.pretrained import EncoderClassifier
    except ImportError:
        print("ERROR: SpeechBrain not installed. Run: pip install speechbrain")
        return False
    
    # Load pretrained model
    classifier = EncoderClassifier.from_hparams(
        source="speechbrain/spkrec-ecapa-voxceleb",
        savedir="models/ecapa-tdnn-cache"
    )
    
    # Get the encoder (embedding extractor) module
    encoder = classifier.mods['embedding_model']
    encoder.eval()
    
    print(f"Model loaded. Embedding dim: {encoder.out_features if hasattr(encoder, 'out_features') else 'unknown'}")
    
    # Create dummy input: 1 second of audio at 16kHz
    sample_rate = 16000
    duration = 1.0
    dummy_input = torch.randn(1, int(sample_rate * duration))
    
    print(f"Dummy input shape: {dummy_input.shape}")
    
    # Test forward pass
    with torch.no_grad():
        try:
            # SpeechBrain ECAPA expects (batch, time)
            embedding = encoder(dummy_input)
            print(f"Output embedding shape: {embedding.shape}")
        except Exception as e:
            print(f"ERROR during forward pass: {e}")
            return False
    
    # Ensure output directory exists
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    
    # Export to ONNX
    print(f"Exporting to {output_path}...")
    try:
        torch.onnx.export(
            encoder,
            dummy_input,
            output_path,
            input_names=['audio'],
            output_names=['embedding'],
            dynamic_axes={
                'audio': {0: 'batch', 1: 'time'},
                'embedding': {0: 'batch'}
            },
            opset_version=14,
            do_constant_folding=True,
            export_params=True,
        )
        print(f"✅ Model exported successfully to {output_path}")
        
        # Verify the exported model
        import onnx
        onnx_model = onnx.load(output_path)
        onnx.checker.check_model(onnx_model)
        print("✅ ONNX model verified")
        
        # Print model info
        print("\nModel Information:")
        print(f"  Inputs: {[inp.name for inp in onnx_model.graph.input]}")
        print(f"  Outputs: {[out.name for out in onnx_model.graph.output]}")
        
        return True
        
    except Exception as e:
        print(f"ERROR during export: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    output_path = sys.argv[1] if len(sys.argv) > 1 else "models/speaker_embedding.onnx"
    success = export_ecapa_to_onnx(output_path)
    sys.exit(0 if success else 1)
