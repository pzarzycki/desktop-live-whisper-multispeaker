# Qt6 Installation Guide

This project requires Qt6 with QML/Quick support for the GUI application.

## Option 1: Chocolatey (Windows - Recommended for Development)

**Minimum Required Packages:**
- Qt6 Base (Core, Gui)
- Qt6 Declarative (Qml, Quick)
- CMake (if not already installed)

### Installation Commands

```powershell
# Install Qt6 base and declarative modules
# This uses aqt (Another Qt Installer) under the hood
choco install -y qt6-base-dev --params "'/modules:qtdeclarative qtcharts'"

# Verify installation
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
qmake --version
```

**Installation Details:**
- Install location: `C:\Qt\6.4.2\mingw_64\`
- Size: ~2-3 GB
- Time: 10-20 minutes (depends on internet speed)
- Compiler: MinGW 11.2.0 (bundled)

**Note:** This installs Qt with MinGW compiler. Our project uses MSVC, so we'll need to configure CMake accordingly (see below).

### Alternative: Install with MSVC Support

If you want Qt built with MSVC (matches our project's compiler):

```powershell
# Install aqt manually for more control
choco install -y aqt

# Install Qt 6.4.2 with MSVC 2019 64-bit
aqt install-qt windows desktop 6.4.2 win64_msvc2019_64 -m qtdeclarative qtcharts

# This installs to: C:\Qt\6.4.2\msvc2019_64\
```

## Option 2: Qt Online Installer (Cross-Platform)

**Best for:** First-time setup, multiple Qt versions, or if Chocolatey fails

### Download and Install

1. **Download Installer:**
   - https://www.qt.io/download-qt-installer
   - Requires free Qt Account (sign up during installation)

2. **Run Installer:**
   ```
   Windows: qt-online-installer-windows-x64-*.exe
   macOS: qt-online-installer-macOS-*.dmg
   ```

3. **Select Components:**
   - ✅ Qt 6.8.0 (or latest LTS)
     - ✅ MSVC 2022 64-bit (Windows)
     - ✅ macOS (on Mac)
   - ✅ Qt Quick
   - ✅ Qt Quick Controls
   - ❌ Qt Creator (optional - we use VS Code)
   - ❌ Qt Design Studio (not needed)
   - ❌ Sources (not needed)

4. **Installation Path:**
   - Windows: `C:\Qt\6.8.0`
   - macOS: `/Users/yourname/Qt/6.8.0`

**Size:** ~3-5 GB (but includes everything)
**Time:** 20-40 minutes

## Option 3: vcpkg (For CI/CD and Reproducible Builds)

**Not recommended for initial setup** - compiles from source (~1-2 hours first time)

See `vcpkg_setup.md` for instructions (coming later).

## Configuring CMake to Find Qt6

After installation, CMake needs to know where Qt6 is located.

### Method 1: Environment Variable (Recommended)

```powershell
# Windows (PowerShell) - Chocolatey installation
[System.Environment]::SetEnvironmentVariable("Qt6_DIR", "C:\Qt\6.4.2\mingw_64", "User")

# Windows (PowerShell) - Qt Online Installer with MSVC
[System.Environment]::SetEnvironmentVariable("Qt6_DIR", "C:\Qt\6.8.0\msvc2022_64", "User")

# Restart terminal to pick up new environment variable
```

```bash
# macOS (Homebrew)
export Qt6_DIR="/opt/homebrew/opt/qt@6/lib/cmake/Qt6"

# macOS (Qt Online Installer)
export Qt6_DIR="$HOME/Qt/6.8.0/macos/lib/cmake/Qt6"

# Add to ~/.zshrc or ~/.bashrc for persistence
```

### Method 2: CMake Preset (Project-Specific)

Update `CMakePresets.json` to add Qt path:

```json
{
  "name": "windows-debug-qt",
  "inherits": "windows-debug",
  "cacheVariables": {
    "CMAKE_PREFIX_PATH": "C:/Qt/6.8.0/msvc2022_64"
  }
}
```

### Method 3: Command Line (One-Time)

```powershell
# Configure with explicit Qt path
cmake --preset windows-debug -DCMAKE_PREFIX_PATH=C:/Qt/6.8.0/msvc2022_64
```

## Verifying Installation

### Check Qt Installation

```powershell
# Windows
qmake --version
# Expected: qmake version 3.1, Using Qt version 6.x.x

# Check CMake can find Qt
cmake -P - <<EOF
find_package(Qt6 COMPONENTS Core Gui Qml Quick)
if(Qt6_FOUND)
  message("Qt6 found: ${Qt6_DIR}")
else()
  message("Qt6 NOT found")
endif()
EOF
```

### Build Test

```powershell
# Configure with Qt enabled
cmake --preset windows-debug

# Build the Qt app
cmake --build --preset build-debug --target app_desktop_whisper

# Run the app
.\build\windows-debug\app_desktop_whisper.exe
```

## Troubleshooting

### "Could not find Qt6" Error

**Solution 1:** Set `CMAKE_PREFIX_PATH`
```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.0\msvc2022_64"
cmake --preset windows-debug
```

**Solution 2:** Set `Qt6_DIR` to CMake config location
```powershell
$env:Qt6_DIR = "C:\Qt\6.8.0\msvc2022_64\lib\cmake\Qt6"
cmake --preset windows-debug
```

### MinGW vs MSVC Mismatch

If you installed Qt with MinGW but want to use MSVC:

**Option A:** Reinstall Qt with MSVC compiler (using aqt or online installer)

**Option B:** Use MinGW for the whole project:
```powershell
cmake -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/Qt/6.4.2/mingw_64 -DBUILD_APP=ON
```

### Multiple Qt Versions Installed

Specify exact version:
```powershell
cmake --preset windows-debug -DQt6_DIR=C:/Qt/6.8.0/msvc2022_64/lib/cmake/Qt6
```

## Recommended Setup for This Project

**For Windows Development:**
1. Use **Qt Online Installer**
2. Install to `C:\Qt\6.8.0`
3. Select **MSVC 2022 64-bit** (matches our project)
4. Set environment variable `Qt6_DIR=C:\Qt\6.8.0\msvc2022_64`
5. Build with: `cmake --preset windows-debug`

**For macOS Development:**
1. Use **Qt Online Installer** or `brew install qt@6`
2. Set environment variable in `~/.zshrc`
3. Build with: `cmake --preset macos-debug` (preset to be added)

**Size Requirements:**
- Qt SDK: ~3-5 GB (one-time)
- Build output: ~500 MB
- Models: ~75 MB (tiny.en)

**Time Investment:**
- Installation: 20-40 minutes
- First build: 5-10 minutes
- Subsequent builds: <1 minute

## Next Steps

After Qt is installed:
1. Verify with `qmake --version`
2. Build the app: `cmake --preset windows-debug && cmake --build --preset build-debug`
3. Run: `.\build\windows-debug\app_desktop_whisper.exe`
4. See GUI with START button and transcript view!

## For Production Deployment

Users do NOT need Qt installed! See `docs/deployment.md` for bundling Qt DLLs (~20 MB) with your application.
