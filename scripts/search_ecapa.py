"""
Search for ECAPA-TDNN ONNX models from SpeechBrain.

ECAPA-TDNN has similar performance to Titanet Large (0.69% EER vs 0.66%),
and SpeechBrain community might have ONNX versions available.
"""

from huggingface_hub import list_models

def search_ecapa_onnx():
    print("=" * 60)
    print("Searching for ECAPA-TDNN ONNX models...")
    print("=" * 60)
    
    # Search for ECAPA models
    print("\n1. Searching for 'ecapa' models...")
    ecapa_models = list(list_models(search="ecapa", limit=30))
    print(f"   Found {len(ecapa_models)} ECAPA-related models\n")
    
    # Filter for ONNX
    onnx_models = [m for m in ecapa_models if 'onnx' in m.modelId.lower()]
    
    if onnx_models:
        print(f"[OK] Found {len(onnx_models)} ECAPA ONNX models:")
        for model in onnx_models:
            print(f"  - {model.modelId}")
        return onnx_models
    
    # Search for SpeechBrain + ONNX
    print("\n2. Searching for 'speechbrain onnx' models...")
    sb_onnx = list(list_models(search="speechbrain onnx", limit=30))
    print(f"   Found {len(sb_onnx)} SpeechBrain ONNX models\n")
    
    speaker_models = [m for m in sb_onnx if 'speaker' in m.modelId.lower() or 'ecapa' in m.modelId.lower()]
    
    if speaker_models:
        print(f"[OK] Found {len(speaker_models)} speaker-related ONNX models:")
        for model in speaker_models:
            print(f"  - {model.modelId}")
        return speaker_models
    
    # Show top ECAPA models (might need conversion)
    print("\n3. Top ECAPA models (may need ONNX conversion):")
    for i, model in enumerate(ecapa_models[:10], 1):
        print(f"  {i}. {model.modelId}")
    
    return []

def search_wespeaker_alternatives():
    """Search for other WeSpeaker models that might work better."""
    print("\n" + "=" * 60)
    print("Searching for alternative WeSpeaker models...")
    print("=" * 60)
    
    models = list(list_models(search="wespeaker", limit=20))
    print(f"\nFound {len(models)} WeSpeaker models:")
    
    for model in models:
        print(f"  - {model.modelId}")
    
    # Check for ONNX versions
    onnx_models = [m for m in models if 'onnx' in m.modelId.lower()]
    if onnx_models:
        print(f"\n[OK] Found {len(onnx_models)} WeSpeaker ONNX models:")
        for model in onnx_models:
            print(f"  - {model.modelId}")
        return onnx_models
    
    return []

def main():
    # Search ECAPA-TDNN
    ecapa_results = search_ecapa_onnx()
    
    # Search alternative WeSpeaker
    wespeaker_results = search_wespeaker_alternatives()
    
    # Decision
    print("\n" + "=" * 60)
    print("DECISION:")
    print("=" * 60)
    
    if ecapa_results:
        print(f"\n[OK] Found {len(ecapa_results)} ECAPA ONNX models")
        print("\nRECOMMENDED:")
        print(f"  1. {ecapa_results[0].modelId}")
        print("\nNext steps:")
        print("  1. Download model")
        print("  2. Test with Python (verify input/output format)")
        print("  3. Integrate into C++ if compatible")
    elif wespeaker_results:
        print(f"\n[OK] Found {len(wespeaker_results)} alternative WeSpeaker ONNX models")
        print("\nRECOMMENDED:")
        print(f"  1. {wespeaker_results[0].modelId}")
        print("\nNote: Might have same VoxCeleb training issue")
    else:
        print("\n[!] No pre-converted ONNX models found")
        print("\nOPTIONS:")
        print("  A. Convert Titanet Large from NeMo to ONNX")
        print("     - Requires: PyTorch, NeMo toolkit")
        print("     - Time: 2-3 hours")
        print("     - Best accuracy potential (0.66% EER)")
        print()
        print("  B. Convert ECAPA-TDNN from SpeechBrain to ONNX")
        print("     - Requires: PyTorch, SpeechBrain")
        print("     - Time: 1-2 hours")
        print("     - Good accuracy (0.69% EER)")
        print()
        print("  C. Hybrid approach (WeSpeaker + hand-crafted)")
        print("     - No new dependencies")
        print("     - Time: 1 hour")
        print("     - Unknown accuracy improvement")
        print()
        print("RECOMMENDATION: Try option C first (fastest), then B if needed")

if __name__ == "__main__":
    main()
