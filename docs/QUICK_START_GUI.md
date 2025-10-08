# Quick Start: Install Qt6 and Test GUI

## Step 1: Download and Install Qt

### 1.1 Download Qt Online Installer

**Visit:** https://www.qt.io/download-qt-installer

- Click "Download the Qt Online Installer"
- Save `qt-unified-windows-x64-online.exe` to your Downloads folder
- No account needed for download

### 1.2 Install Qt (Choose A or B)

#### Option A: Automated Installation (Recommended)

Run from PowerShell in your Downloads folder:

```powershell
# Install Qt 6.8.0 with MSVC 2022 (no GUI prompts)
.\qt-unified-windows-x64-online.exe --root C:\Qt `
  --accept-licenses `
  --default-answer `
  --confirm-command `
  install qt.qt6.680.win64_msvc2022_64 qt.qt6.680.addons.qtdeclarative
```

**What this installs:**
- Qt 6.8.0 with MSVC 2022 64-bit (matches our project's compiler!)
- Location: `C:\Qt\6.8.0\msvc2022_64\`
- Size: ~3-4 GB
- Time: 15-30 minutes

#### Option B: Interactive GUI Installation

1. Double-click `qt-unified-windows-x64-online.exe`
2. Create/login to Qt Account (free)
3. Select Components:
   - ✅ Qt 6.8.0 → MSVC 2022 64-bit
   - ✅ Qt Quick (under Additional Libraries)
4. Install location: `C:\Qt` (default is fine)
5. Click Install and wait

**After installation, close and reopen your terminal!**

## Step 2: Configure CMake to Find Qt6

```powershell
# Set environment variable (permanent)
[System.Environment]::SetEnvironmentVariable("CMAKE_PREFIX_PATH", "C:\Qt\6.8.0\msvc2022_64", "User")

# Restart PowerShell to pick up the change
# Or for current session only:
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.0\msvc2022_64"
```

## Step 3: Verify Qt Installation

```powershell
# Check Qt is accessible
C:\Qt\6.8.0\msvc2022_64\bin\qmake.exe --version
# Should show: Using Qt version 6.8.0
```

## Step 4: Build the GUI Application

```powershell
# Navigate to project root
cd C:\PRJ\VAM\desktop-live-whisper-multiplatform

# Activate VS Build Tools environment (if not already done)
. .\Enter-VSDev.ps1

# Configure with existing preset (now with Qt available)
cmake --preset windows-debug

# Build
cmake --build --preset build-debug --target app_desktop_whisper

# Run!
.\build\windows-debug\app_desktop_whisper.exe
```

## Step 5: Test the GUI

**What you should see:**
1. Window opens with dark theme (1200x800)
2. "START RECORDING" button at top (blue)
3. Empty transcript area with "Press START RECORDING to begin..."
4. Settings panel at bottom with:
   - Synthetic Audio checkbox (checked)
   - Audio file path: `output/whisper_input_16k.wav`
   - Model: tiny.en
   - Max Speakers: 2

**What to test:**
1. Click START RECORDING
   - Button should turn red and say "STOP RECORDING"
   - Status bar should update with elapsed time
2. Click STOP RECORDING
   - Button should turn blue again
3. Try changing settings (only works when stopped)
4. Check if window resizes properly

**Known Limitation:**
- No transcript will appear yet (controller not wired to engine)
- This is expected! We're testing the GUI structure only

## Troubleshooting

### "Could not find Qt6" Error

```powershell
# Double-check Qt path
dir C:\Qt\6.4.2\mingw_64\lib\cmake\Qt6

# If path is different, update CMAKE_PREFIX_PATH
$env:CMAKE_PREFIX_PATH = "C:\Qt\<your-version>\mingw_64"
```

### Build Errors

```powershell
# Clean and reconfigure
Remove-Item -Recurse -Force build-qt
cmake -B build-qt -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug -DBUILD_APP=ON -DCMAKE_PREFIX_PATH=C:/Qt/6.4.2/mingw_64
```

### Missing DLLs at Runtime

If app fails to start with DLL errors:

```powershell
# Add Qt bin directory to PATH temporarily
$env:PATH = "C:\Qt\6.4.2\mingw_64\bin;$env:PATH"

# Run again
.\build-qt\app_desktop_whisper.exe
```

## Alternative: Use Qt Online Installer (If Chocolatey Fails)

See full guide: [docs/qt_setup.md](../docs/qt_setup.md)

1. Download: https://www.qt.io/download-qt-installer
2. Install Qt 6.8.0 with MSVC 2022 64-bit
3. Location: `C:\Qt\6.8.0\msvc2022_64`
4. Update CMAKE_PREFIX_PATH to match
5. Use Ninja generator instead of MinGW

## Success Criteria

✅ Qt installed and qmake works
✅ CMake finds Qt6 (no "Could not find Qt6" error)
✅ app_desktop_whisper.exe builds successfully
✅ GUI window opens with dark theme
✅ START/STOP button works
✅ Settings can be changed
✅ No crashes

## Next Steps After Testing

Once GUI works:
1. Commit the working configuration
2. Document the Qt path in `.vscode/settings.json` or CMakePresets.json
3. Move to Phase 6: Wire controller to engine (see NEXT_AGENT_START_HERE.md)
4. Add synthetic audio support to test transcription

---

**Estimated Time:** 30-60 minutes (including Qt download)
