#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Download required models for desktop-live-whisper
.DESCRIPTION
    Downloads Whisper models and speaker embedding models to the models/ directory.
    Models are large files (75-465 MB each) and are not included in git.
.PARAMETER WhisperModel
    Which Whisper model to download: tiny, base, small (default: tiny)
.PARAMETER SpeakerModel
    Which speaker embedding model to download: wespeaker, campplus (default: wespeaker)
.EXAMPLE
    .\scripts\download_models.ps1
    Downloads tiny.en Whisper model and WeSpeaker ResNet34
.EXAMPLE
    .\scripts\download_models.ps1 -WhisperModel base
    Downloads base.en Whisper model
#>

param(
    [ValidateSet("tiny", "base", "small")]
    [string]$WhisperModel = "tiny",
    
    [ValidateSet("wespeaker", "campplus")]
    [string]$SpeakerModel = "wespeaker"
)

# Color output helpers
function Write-Success { Write-Host $args -ForegroundColor Green }
function Write-Info { Write-Host $args -ForegroundColor Cyan }
function Write-Warning { Write-Host $args -ForegroundColor Yellow }
function Write-Error { Write-Host $args -ForegroundColor Red }

Write-Host "`n============================================================" -ForegroundColor Cyan
Write-Host "Model Download Script" -ForegroundColor Cyan
Write-Host "============================================================`n" -ForegroundColor Cyan

# Create models directory
$modelsDir = Join-Path $PSScriptRoot ".." "models"
if (-not (Test-Path $modelsDir)) {
    Write-Info "Creating models directory..."
    New-Item -ItemType Directory -Path $modelsDir | Out-Null
}

# Whisper model URLs and info
$whisperModels = @{
    "tiny" = @{
        url = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin"
        filename = "ggml-tiny.en.bin"
        size = "75 MB"
        description = "Fastest, good quality, recommended for real-time"
    }
    "base" = @{
        url = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en-q5_1.bin"
        filename = "ggml-base.en-q5_1.bin"
        size = "57 MB"
        description = "Better quality, slower than tiny"
    }
    "small" = @{
        url = "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin"
        filename = "small.en.bin"
        size = "465 MB"
        description = "Best quality, not real-time capable on CPU"
    }
}

# Speaker model URLs and info
$speakerModels = @{
    "wespeaker" = @{
        url = "https://huggingface.co/Wespeaker/wespeaker-voxceleb-resnet34/resolve/main/voxceleb_resnet34.onnx"
        filename = "speaker_embedding.onnx"
        size = "25 MB"
        description = "WeSpeaker ResNet34 (current)"
    }
    "campplus" = @{
        url = "https://huggingface.co/Wespeaker/wespeaker-voxceleb-campplus/resolve/main/campplus_voxceleb.onnx"
        filename = "campplus_voxceleb.onnx"
        size = "7 MB"
        description = "CAMPlus (experimental, may be better)"
    }
}

# Function to download file with progress
function Download-File {
    param(
        [string]$Url,
        [string]$OutputPath,
        [string]$Description
    )
    
    $outputFile = Split-Path $OutputPath -Leaf
    
    # Check if file already exists
    if (Test-Path $OutputPath) {
        $fileSize = (Get-Item $OutputPath).Length / 1MB
        Write-Warning "  File already exists: $outputFile ($([math]::Round($fileSize, 2)) MB)"
        $response = Read-Host "  Overwrite? (y/N)"
        if ($response -ne "y" -and $response -ne "Y") {
            Write-Info "  Skipped."
            return $true
        }
    }
    
    Write-Info "  Downloading: $Description"
    Write-Info "  From: $Url"
    Write-Info "  To: $OutputPath"
    
    try {
        # Use .NET WebClient for progress
        $webClient = New-Object System.Net.WebClient
        
        # Progress event handler
        $onProgress = {
            param($sender, $e)
            $percent = $e.ProgressPercentage
            $received = $e.BytesReceived / 1MB
            $total = $e.TotalBytesToReceive / 1MB
            Write-Progress -Activity "Downloading $outputFile" `
                -Status "$([math]::Round($received, 2)) MB / $([math]::Round($total, 2)) MB" `
                -PercentComplete $percent
        }
        
        Register-ObjectEvent -InputObject $webClient -EventName DownloadProgressChanged `
            -SourceIdentifier WebClient.DownloadProgressChanged -Action $onProgress | Out-Null
        
        # Download file
        $webClient.DownloadFileAsync($Url, $OutputPath)
        
        # Wait for download to complete
        while ($webClient.IsBusy) {
            Start-Sleep -Milliseconds 100
        }
        
        Write-Progress -Activity "Downloading $outputFile" -Completed
        Unregister-Event -SourceIdentifier WebClient.DownloadProgressChanged
        $webClient.Dispose()
        
        Write-Success "  ✓ Downloaded successfully"
        return $true
        
    } catch {
        Write-Error "  ✗ Download failed: $_"
        return $false
    }
}

# Download Whisper model
Write-Host "`n--- Whisper Model ---" -ForegroundColor Yellow
$whisperInfo = $whisperModels[$WhisperModel]
Write-Info "Model: $($whisperInfo.filename)"
Write-Info "Size: $($whisperInfo.size)"
Write-Info "Description: $($whisperInfo.description)"

$whisperPath = Join-Path $modelsDir $whisperInfo.filename
$whisperSuccess = Download-File -Url $whisperInfo.url -OutputPath $whisperPath `
    -Description "Whisper $WhisperModel model"

# Download Speaker model
Write-Host "`n--- Speaker Embedding Model ---" -ForegroundColor Yellow
$speakerInfo = $speakerModels[$SpeakerModel]
Write-Info "Model: $($speakerInfo.filename)"
Write-Info "Size: $($speakerInfo.size)"
Write-Info "Description: $($speakerInfo.description)"

$speakerPath = Join-Path $modelsDir $speakerInfo.filename
$speakerSuccess = Download-File -Url $speakerInfo.url -OutputPath $speakerPath `
    -Description "Speaker $SpeakerModel model"

# Summary
Write-Host "`n============================================================" -ForegroundColor Cyan
Write-Host "Download Summary" -ForegroundColor Cyan
Write-Host "============================================================`n" -ForegroundColor Cyan

if ($whisperSuccess) {
    Write-Success "✓ Whisper model: $($whisperInfo.filename)"
} else {
    Write-Error "✗ Whisper model: FAILED"
}

if ($speakerSuccess) {
    Write-Success "✓ Speaker model: $($speakerInfo.filename)"
} else {
    Write-Error "✗ Speaker model: FAILED"
}

if ($whisperSuccess -and $speakerSuccess) {
    Write-Host "`n" -NoNewline
    Write-Success "All models downloaded successfully!"
    Write-Host "`nNext steps:" -ForegroundColor Yellow
    Write-Host "  1. Build the project: cmake --build --preset build-tests-only-release"
    Write-Host "  2. Run: .\build\tests-only-release\app_transcribe_file.exe <audio.wav>"
    Write-Host "`n"
    exit 0
} else {
    Write-Host "`n" -NoNewline
    Write-Error "Some downloads failed. Please try again or download manually."
    Write-Host "See MODELS.md for manual download instructions.`n"
    exit 1
}
