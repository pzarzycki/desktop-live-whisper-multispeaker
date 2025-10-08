# Qt6 Installation - What You Need to Know

## TL;DR

**Download and run Qt's Official Online Installer**

1. **Download:** Visit https://www.qt.io/download-qt-installer
2. **Install:** Run `qt-unified-windows-x64-online.exe`
3. **Select:** Qt 6.8.0 → MSVC 2022 64-bit + Qt Quick
4. **Location:** `C:\Qt` (default is fine)

**Or use CLI for automation:**
```powershell
.\qt-unified-windows-x64-online.exe --root C:\Qt --accept-licenses --default-answer --confirm-command install qt.qt6.680.win64_msvc2022_64 qt.qt6.680.addons.qtdeclarative
```

**Why this approach:**
1. ✅ **Standard Windows practice** - user downloads from website, not scripted
2. ✅ **Official** Qt installer (maintained by Qt Company)
3. ✅ **MSVC 2022** binaries (matches our project - no compiler mismatch!)
4. ✅ **Latest** version (6.8.0)
5. ✅ **User chooses install location** (respects Windows conventions)

## Installation Methods (Official Documentation)

Qt officially provides **three ways** to install:

### 1. Qt Online Installer with GUI (Most Popular)
- Download from: https://www.qt.io/download-qt-installer
- Interactive wizard
- Requires Qt Account (free)
- Best for: First-time users, exploring Qt

### 2. Qt Online Installer with CLI (Recommended for Us!)
- Same installer, use command-line flags
- **Unattended** (no user interaction needed)
- **Automated** (perfect for scripts)
- Best for: Reproducible builds, automation

### 3. Build from Source
- Compile Qt yourself
- Takes 4-6 hours
- Only needed for custom Qt modifications
- **Not recommended** for this project

## Why NOT Chocolatey?

Chocolatey's `qt6-base-dev` package has issues:

1. **Old version**: 6.4.2 (vs current 6.8.0)
2. **MinGW only**: Doesn't provide MSVC builds by default
3. **Third-party**: Not officially maintained by Qt
4. **Limited modules**: Uses `aqt` underneath (another wrapper)
5. **Compiler mismatch**: Our project uses MSVC, Chocolatey gives MinGW

## What We're Installing

**Package:** `qt.qt6.680.win64_msvc2022_64`
- Qt 6.8.0
- Windows x64
- MSVC 2022 compiler (matches VS Build Tools)
- Full Qt Quick/QML support

**Add-on:** `qt.qt6.680.addons.qtdeclarative`
- QML runtime
- Qt Quick Controls
- Qt Quick layouts

**Total size:** ~3-4 GB (but users won't need this - they get ~20 MB of DLLs!)

## Installation Locations

```
C:\Qt\
├── 6.8.0\
│   └── msvc2022_64\          ← Our Qt installation
│       ├── bin\              ← qmake.exe, Qt tools
│       ├── lib\              ← Qt6Core.lib, etc.
│       ├── include\          ← Qt headers
│       ├── qml\              ← QML modules
│       └── lib\cmake\Qt6\    ← CMake config files
├── Tools\                    ← Qt Creator (optional)
└── MaintenanceTool.exe       ← Update/modify installation
```

## For Users (Deployment)

**Users DO NOT install Qt!**

When we distribute the app, we bundle only the DLLs:
- `Qt6Core.dll` (~6 MB)
- `Qt6Gui.dll` (~6 MB)  
- `Qt6Qml.dll` (~4 MB)
- `Qt6Quick.dll` (~4 MB)
- `platforms/qwindows.dll` (~2 MB)

**Total: ~20-25 MB** (vs 3-4 GB SDK)

Tool: `windeployqt.exe` (included with Qt) automatically copies needed DLLs.

## Next Steps

1. **Download installer:**
   ```powershell
   Invoke-WebRequest -Uri "https://download.qt.io/official_releases/online_installers/qt-unified-windows-x64-online.exe" -OutFile qt-installer.exe
   ```

2. **Run automated installation:**
   ```powershell
   .\qt-installer.exe --root C:\Qt `
     --accept-licenses --default-answer --confirm-command `
     install qt.qt6.680.win64_msvc2022_64 qt.qt6.680.addons.qtdeclarative
   ```

3. **Set CMAKE_PREFIX_PATH:**
   ```powershell
   [System.Environment]::SetEnvironmentVariable("CMAKE_PREFIX_PATH", "C:\Qt\6.8.0\msvc2022_64", "User")
   ```

4. **Build and test:**
   ```powershell
   cmake --preset windows-debug
   cmake --build --preset build-debug --target app_desktop_whisper
   .\build\windows-debug\app_desktop_whisper.exe
   ```

## Troubleshooting

**Q: Do I need a Qt Account?**
A: For CLI installation, no! The `--accept-licenses` flag bypasses the login.

**Q: Can I use the GUI installer instead?**
A: Yes! Just run `.\qt-installer.exe` without flags. More clicking, same result.

**Q: What about macOS?**
A: Same process, different installer:
```bash
./qt-unified-macOS-x64-online.dmg --root ~/Qt install qt.qt6.680.clang_64
```

**Q: How do I uninstall?**
A: Run `C:\Qt\MaintenanceTool.exe purge` or just delete `C:\Qt\` folder.

## References

- Official docs: https://doc.qt.io/qt-6/get-and-install-qt-cli.html
- Available packages: https://download.qt.io/online/qtsdkrepository/
- Qt 6 for Windows: https://doc.qt.io/qt-6/windows.html

---

**Bottom Line:** Use Qt's official installer with CLI flags for automated, reproducible installation with MSVC support. No third-party tools needed!
