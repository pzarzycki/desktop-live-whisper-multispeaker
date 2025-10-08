#!/bin/bash
#
# Download required models for desktop-live-whisper
# 
# Downloads Whisper models and speaker embedding models to the models/ directory.
# Models are large files (75-465 MB each) and are not included in git.
#
# Usage:
#   ./scripts/download_models.sh [whisper_model] [speaker_model]
#
# Arguments:
#   whisper_model: tiny, base, or small (default: tiny)
#   speaker_model: wespeaker or campplus (default: wespeaker)
#
# Examples:
#   ./scripts/download_models.sh
#   ./scripts/download_models.sh base
#   ./scripts/download_models.sh tiny campplus
#

set -e  # Exit on error

# Color output helpers
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

function print_success() { echo -e "${GREEN}$1${NC}"; }
function print_info() { echo -e "${CYAN}$1${NC}"; }
function print_warning() { echo -e "${YELLOW}$1${NC}"; }
function print_error() { echo -e "${RED}$1${NC}"; }

echo -e "${CYAN}"
echo "============================================================"
echo "Model Download Script"
echo "============================================================"
echo -e "${NC}"

# Get script directory and project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
MODELS_DIR="$PROJECT_ROOT/models"

# Parse arguments
WHISPER_MODEL="${1:-tiny}"
SPEAKER_MODEL="${2:-wespeaker}"

# Validate arguments
if [[ ! "$WHISPER_MODEL" =~ ^(tiny|base|small)$ ]]; then
    print_error "Invalid whisper model: $WHISPER_MODEL"
    echo "Valid options: tiny, base, small"
    exit 1
fi

if [[ ! "$SPEAKER_MODEL" =~ ^(wespeaker|campplus)$ ]]; then
    print_error "Invalid speaker model: $SPEAKER_MODEL"
    echo "Valid options: wespeaker, campplus"
    exit 1
fi

# Create models directory
if [ ! -d "$MODELS_DIR" ]; then
    print_info "Creating models directory..."
    mkdir -p "$MODELS_DIR"
fi

# Whisper model configurations
declare -A WHISPER_URLS=(
    ["tiny"]="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin"
    ["base"]="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en-q5_1.bin"
    ["small"]="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.en.bin"
)

declare -A WHISPER_FILENAMES=(
    ["tiny"]="ggml-tiny.en.bin"
    ["base"]="ggml-base.en-q5_1.bin"
    ["small"]="small.en.bin"
)

declare -A WHISPER_SIZES=(
    ["tiny"]="75 MB"
    ["base"]="57 MB"
    ["small"]="465 MB"
)

declare -A WHISPER_DESCRIPTIONS=(
    ["tiny"]="Fastest, good quality, recommended for real-time"
    ["base"]="Better quality, slower than tiny"
    ["small"]="Best quality, not real-time capable on CPU"
)

# Speaker model configurations
declare -A SPEAKER_URLS=(
    ["wespeaker"]="https://huggingface.co/Wespeaker/wespeaker-voxceleb-resnet34/resolve/main/voxceleb_resnet34.onnx"
    ["campplus"]="https://huggingface.co/Wespeaker/wespeaker-voxceleb-campplus/resolve/main/campplus_voxceleb.onnx"
)

declare -A SPEAKER_FILENAMES=(
    ["wespeaker"]="speaker_embedding.onnx"
    ["campplus"]="campplus_voxceleb.onnx"
)

declare -A SPEAKER_SIZES=(
    ["wespeaker"]="25 MB"
    ["campplus"]="7 MB"
)

declare -A SPEAKER_DESCRIPTIONS=(
    ["wespeaker"]="WeSpeaker ResNet34 (current)"
    ["campplus"]="CAMPlus (experimental, may be better)"
)

# Function to download file
download_file() {
    local url="$1"
    local output_path="$2"
    local description="$3"
    local filename=$(basename "$output_path")
    
    # Check if file already exists
    if [ -f "$output_path" ]; then
        local file_size_mb=$(du -m "$output_path" | cut -f1)
        print_warning "  File already exists: $filename (${file_size_mb} MB)"
        read -p "  Overwrite? (y/N) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            print_info "  Skipped."
            return 0
        fi
    fi
    
    print_info "  Downloading: $description"
    print_info "  From: $url"
    print_info "  To: $output_path"
    
    # Check if wget or curl is available
    if command -v wget &> /dev/null; then
        wget --progress=bar:force -O "$output_path" "$url" 2>&1 | \
            grep --line-buffered "%" | \
            sed -u -e "s,\.,,g" | \
            awk '{printf("\r  Progress: %s", $2); fflush()}'
        echo ""
    elif command -v curl &> /dev/null; then
        curl -# -L -o "$output_path" "$url"
    else
        print_error "  ✗ Neither wget nor curl found. Please install one of them."
        return 1
    fi
    
    if [ $? -eq 0 ]; then
        print_success "  ✓ Downloaded successfully"
        return 0
    else
        print_error "  ✗ Download failed"
        return 1
    fi
}

# Download Whisper model
echo ""
print_warning "--- Whisper Model ---"
WHISPER_URL="${WHISPER_URLS[$WHISPER_MODEL]}"
WHISPER_FILENAME="${WHISPER_FILENAMES[$WHISPER_MODEL]}"
WHISPER_SIZE="${WHISPER_SIZES[$WHISPER_MODEL]}"
WHISPER_DESC="${WHISPER_DESCRIPTIONS[$WHISPER_MODEL]}"

print_info "Model: $WHISPER_FILENAME"
print_info "Size: $WHISPER_SIZE"
print_info "Description: $WHISPER_DESC"

WHISPER_PATH="$MODELS_DIR/$WHISPER_FILENAME"
if download_file "$WHISPER_URL" "$WHISPER_PATH" "Whisper $WHISPER_MODEL model"; then
    WHISPER_SUCCESS=true
else
    WHISPER_SUCCESS=false
fi

# Download Speaker model
echo ""
print_warning "--- Speaker Embedding Model ---"
SPEAKER_URL="${SPEAKER_URLS[$SPEAKER_MODEL]}"
SPEAKER_FILENAME="${SPEAKER_FILENAMES[$SPEAKER_MODEL]}"
SPEAKER_SIZE="${SPEAKER_SIZES[$SPEAKER_MODEL]}"
SPEAKER_DESC="${SPEAKER_DESCRIPTIONS[$SPEAKER_MODEL]}"

print_info "Model: $SPEAKER_FILENAME"
print_info "Size: $SPEAKER_SIZE"
print_info "Description: $SPEAKER_DESC"

SPEAKER_PATH="$MODELS_DIR/$SPEAKER_FILENAME"
if download_file "$SPEAKER_URL" "$SPEAKER_PATH" "Speaker $SPEAKER_MODEL model"; then
    SPEAKER_SUCCESS=true
else
    SPEAKER_SUCCESS=false
fi

# Summary
echo ""
echo -e "${CYAN}"
echo "============================================================"
echo "Download Summary"
echo "============================================================"
echo -e "${NC}"

if [ "$WHISPER_SUCCESS" = true ]; then
    print_success "✓ Whisper model: $WHISPER_FILENAME"
else
    print_error "✗ Whisper model: FAILED"
fi

if [ "$SPEAKER_SUCCESS" = true ]; then
    print_success "✓ Speaker model: $SPEAKER_FILENAME"
else
    print_error "✗ Speaker model: FAILED"
fi

if [ "$WHISPER_SUCCESS" = true ] && [ "$SPEAKER_SUCCESS" = true ]; then
    echo ""
    print_success "All models downloaded successfully!"
    echo ""
    print_warning "Next steps:"
    echo "  1. Build the project: cmake --build --preset build-tests-only-release"
    echo "  2. Run: ./build/tests-only-release/app_transcribe_file <audio.wav>"
    echo ""
    exit 0
else
    echo ""
    print_error "Some downloads failed. Please try again or download manually."
    echo "See MODELS.md for manual download instructions."
    echo ""
    exit 1
fi
