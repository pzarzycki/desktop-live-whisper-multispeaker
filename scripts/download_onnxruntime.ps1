# Download ONNX Runtime prebuilt binaries for Windows
$version = "1.20.1"
$url = "https://github.com/microsoft/onnxruntime/releases/download/v$version/onnxruntime-win-x64-$version.zip"
$output = "third_party/onnxruntime-win-x64-$version.zip"
$extract_dir = "third_party/onnxruntime"

Write-Host "Downloading ONNX Runtime v$version..."
New-Item -ItemType Directory -Force -Path "third_party" | Out-Null

# Download
Invoke-WebRequest -Uri $url -OutFile $output -UseBasicParsing

Write-Host "Extracting..."
Expand-Archive -Path $output -DestinationPath "third_party/onnxruntime-temp" -Force

# Move files to final location
if (Test-Path $extract_dir) {
    Remove-Item -Recurse -Force $extract_dir
}
Move-Item "third_party/onnxruntime-temp/onnxruntime-win-x64-$version" $extract_dir

# Cleanup
Remove-Item -Recurse -Force "third_party/onnxruntime-temp"
Remove-Item $output

Write-Host "✅ ONNX Runtime installed to $extract_dir"
Write-Host ""
Write-Host "Contents:"
Get-ChildItem $extract_dir
