# Download WeSpeaker ResNet34 ONNX model for speaker embeddings
$url = "https://wespeaker-1256283475.cos.ap-shanghai.myqcloud.com/models/voxceleb/voxceleb_resnet34.onnx"
$output = "models/speaker_embedding.onnx"

Write-Host "Downloading WeSpeaker ResNet34 speaker embedding model..."
New-Item -ItemType Directory -Force -Path "models" | Out-Null

# Download
try {
    Invoke-WebRequest -Uri $url -OutFile $output -UseBasicParsing
    $size = (Get-Item $output).Length / 1MB
    Write-Host "✅ Model downloaded: $output ($([math]::Round($size, 2)) MB)"
} catch {
    Write-Host "❌ Download failed from primary source"
    Write-Host "Trying alternative GitHub release..."
    
    # Fallback: try GitHub releases
    $alt_url = "https://github.com/wenet-e2e/wespeaker/releases/download/v1.0.0/voxceleb_resnet34.onnx"
    try {
        Invoke-WebRequest -Uri $alt_url -OutFile $output -UseBasicParsing
        $size = (Get-Item $output).Length / 1MB
        Write-Host "✅ Model downloaded from GitHub: $output ($([math]::Round($size, 2)) MB)"
    } catch {
        Write-Host "❌ Both download sources failed. Please download manually:"
        Write-Host "   1. Visit: https://github.com/wenet-e2e/wespeaker"
        Write-Host "   2. Download voxceleb_resnet34.onnx"
        Write-Host "   3. Place in: models/speaker_embedding.onnx"
        exit 1
    }
}

Write-Host ""
Write-Host "Model Info:"
Write-Host "  - Type: WeSpeaker ResNet34"
Write-Host "  - Embedding dimension: 256"
Write-Host "  - Input: 16kHz mono audio (float32)"
Write-Host "  - Output: L2-normalized 256-dim vector"
Write-Host "  - Performance: ~2% EER on VoxCeleb"
Write-Host ""
Write-Host "Ready to test! Run: ./build/tests-only-release/app_transcribe_file.exe"
