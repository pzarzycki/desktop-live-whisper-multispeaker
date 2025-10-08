"""
Search for and download Titanet Large model for speaker embedding.

This script checks multiple sources for pre-trained Titanet Large ONNX model:
1. Hugging Face Model Hub
2. NVIDIA NGC (NeMo checkpoints)
3. ONNX Model Zoo

If ONNX version not available, provides instructions for conversion from PyTorch.
"""

import os
import sys
from pathlib import Path

def check_huggingface():
    """Check Hugging Face for Titanet Large ONNX."""
    print("\n=== Checking Hugging Face Model Hub ===")
    
    try:
        # Try importing huggingface_hub
        from huggingface_hub import hf_hub_download, list_models
        
        # Search for Titanet models
        print("Searching for 'titanet' models...")
        models = list(list_models(search="titanet", limit=20))
        
        if not models:
            print("[X] No Titanet models found on Hugging Face")
            return None
            
        print(f"[OK] Found {len(models)} Titanet-related models:")
        for i, model in enumerate(models, 1):
            print(f"  {i}. {model.modelId}")
            
        # Look for ONNX versions
        onnx_models = [m for m in models if 'onnx' in m.modelId.lower()]
        if onnx_models:
            print(f"\n[OK] Found {len(onnx_models)} ONNX models:")
            for model in onnx_models:
                print(f"  - {model.modelId}")
            return onnx_models[0].modelId
        else:
            print("\n[!] No ONNX versions found, will need conversion")
            return None
            
    except ImportError:
        print("[!] huggingface_hub not installed")
        print("   Install: uv pip install huggingface-hub")
        return None
    except Exception as e:
        print(f"[X] Error: {e}")
        return None

def check_nemo_pretrained():
    """Check NVIDIA NeMo for Titanet Large."""
    print("\n=== Checking NVIDIA NeMo ===")
    
    try:
        import nemo.collections.asr as nemo_asr
        
        # List available models
        print("Available speaker verification models:")
        models = nemo_asr.models.EncDecSpeakerLabelModel.list_available_models()
        
        titanet_models = [m for m in models if 'titanet' in m.pretrained_model_name.lower()]
        
        if titanet_models:
            print(f"\n[OK] Found {len(titanet_models)} Titanet models:")
            for model in titanet_models:
                print(f"  - {model.pretrained_model_name}")
                print(f"    Size: {model.description}")
            return titanet_models[0].pretrained_model_name
        else:
            print("[X] No Titanet models found in NeMo")
            return None
            
    except ImportError:
        print("[!] NeMo not installed (requires CUDA, large install)")
        print("   Install: pip install nemo_toolkit[asr]")
        return None
    except Exception as e:
        print(f"[X] Error: {e}")
        return None

def search_alternative_sources():
    """Provide alternative sources for Titanet Large."""
    print("\n=== Alternative Sources ===")
    
    print("\n1. NVIDIA NGC Catalog:")
    print("   https://catalog.ngc.nvidia.com/models")
    print("   Search for: titanet_large")
    
    print("\n2. SpeechBrain (ECAPA-TDNN alternative):")
    print("   https://huggingface.co/speechbrain")
    print("   Similar performance: 0.69% EER")
    
    print("\n3. Manual conversion from PyTorch:")
    print("   - Download NeMo checkpoint")
    print("   - Export to ONNX using torch.onnx.export()")
    print("   - See: convert_nemo_to_onnx.py (to be created)")

def download_model(model_id, output_path="models/titanet_large.onnx"):
    """Download model from Hugging Face."""
    try:
        from huggingface_hub import hf_hub_download
        
        print(f"\n=== Downloading {model_id} ===")
        
        # Try to find .onnx file
        # Note: This is a guess, actual filename may vary
        downloaded_path = hf_hub_download(
            repo_id=model_id,
            filename="model.onnx",
            cache_dir="models/cache"
        )
        
        print(f"✅ Downloaded to: {downloaded_path}")
        
        # Copy to models/ directory
        import shutil
        os.makedirs("models", exist_ok=True)
        shutil.copy(downloaded_path, output_path)
        print(f"[OK] Copied to: {output_path}")
        
        return output_path
        
    except Exception as e:
        print(f"[X] Download failed: {e}")
        return None

def main():
    print("=" * 60)
    print("Titanet Large Model Search")
    print("=" * 60)
    
    # Check Hugging Face
    hf_model = check_huggingface()
    
    # Check NeMo (probably not installed without CUDA)
    nemo_model = check_nemo_pretrained()
    
    # Show alternatives
    search_alternative_sources()
    
    # Decision
    print("\n" + "=" * 60)
    print("DECISION:")
    print("=" * 60)
    
    if hf_model:
        print(f"\n[OK] Found ONNX model: {hf_model}")
        print("   Attempting download...")
        downloaded = download_model(hf_model)
        if downloaded:
            print(f"\n[SUCCESS] Model ready at: {downloaded}")
        else:
            print("\n[!] Download failed, manual download needed")
    else:
        print("\n[!] No pre-converted ONNX model found")
        print("\nRECOMMENDATION:")
        print("  Option A: Try ECAPA-TDNN from SpeechBrain (similar performance)")
        print("  Option B: Convert NeMo Titanet to ONNX (requires PyTorch + NeMo)")
        print("  Option C: Use hybrid approach (WeSpeaker + hand-crafted)")
        
        print("\nNext steps:")
        print("  1. Check if ECAPA-TDNN ONNX available on Hugging Face")
        print("  2. If not, create conversion script for NeMo -> ONNX")
        print("  3. Or proceed with hybrid approach")

if __name__ == "__main__":
    main()
