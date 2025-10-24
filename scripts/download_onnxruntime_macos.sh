#!/bin/bash
# Download ONNX Runtime prebuilt binaries for macOS

set -e

VERSION="1.20.1"
URL="https://github.com/microsoft/onnxruntime/releases/download/v${VERSION}/onnxruntime-osx-universal2-${VERSION}.tgz"
OUTPUT="third_party/onnxruntime-osx-universal2-${VERSION}.tgz"
EXTRACT_DIR="third_party/onnxruntime-macos"

echo "Downloading ONNX Runtime v${VERSION} for macOS..."
mkdir -p third_party

# Download
curl -L -o "${OUTPUT}" "${URL}"

echo "Extracting..."
mkdir -p third_party/onnxruntime-temp
tar -xzf "${OUTPUT}" -C third_party/onnxruntime-temp

# Move files to final location
if [ -d "${EXTRACT_DIR}" ]; then
    rm -rf "${EXTRACT_DIR}"
fi
mv "third_party/onnxruntime-temp/onnxruntime-osx-universal2-${VERSION}" "${EXTRACT_DIR}"

# Cleanup
rm -rf third_party/onnxruntime-temp
rm "${OUTPUT}"

echo "✅ ONNX Runtime installed to ${EXTRACT_DIR}"
echo ""
echo "Contents:"
ls -la "${EXTRACT_DIR}"
