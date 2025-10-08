# Platform-Specific Code Organization

## Overview

This is a **MULTIPLATFORM** solution supporting Windows and macOS. Platform-specific code is strictly isolated.

## Directory Structure

```
src/
├── ui/
│   ├── app_window.hpp       # Platform-independent UI logic (✅ NO platform headers)
│   ├── app_window.cpp       # Platform-independent UI implementation
│   ├── main_windows.cpp     # Windows-specific entry point (DirectX 11)
│   └── main_macos.mm        # macOS-specific entry point (Metal)
├── app/                     # Platform-independent application logic
├── asr/                     # Platform-independent ASR
├── audio/
│   ├── windows_wasapi.cpp   # Windows audio capture
│   └── macos_coreaudio.mm   # macOS audio capture (TODO)
└── core/                    # Platform-independent utilities
```

## Platform Isolation Rules

### ✅ Platform-Independent Code (app_window.hpp/cpp)

**ALLOWED**:
- Standard C++ headers: `<vector>`, `<string>`, `<memory>`, `<cstdint>`
- ImGui headers: `imgui.h` (platform-agnostic)
- Application headers: `app/transcription_controller.hpp`

**FORBIDDEN**:
- ❌ `<windows.h>` or any Windows SDK headers
- ❌ `<Cocoa/Cocoa.h>` or any macOS frameworks
- ❌ Platform-specific types (HWND, NSWindow, etc.)

### 🪟 Windows-Specific Code (main_windows.cpp)

**Purpose**: Windows entry point with DirectX 11 rendering

**Headers Used**:
- `<windows.h>` - Win32 API
- `<ShellScalingApi.h>` - DPI awareness
- `<d3d11.h>` - DirectX 11
- `imgui_impl_win32.h` - ImGui Win32 backend
- `imgui_impl_dx11.h` - ImGui DirectX 11 renderer

**Features**:
- DPI-aware rendering (PROCESS_PER_MONITOR_DPI_AWARE)
- TrueType font loading (Segoe UI from C:\Windows\Fonts\)
- DirectX 11 swap chain and rendering
- Win32 message pump

### 🍎 macOS-Specific Code (main_macos.mm)

**Purpose**: macOS entry point with Metal rendering

**Headers Used**:
- `<Cocoa/Cocoa.h>` - Cocoa framework
- `<Metal/Metal.h>` - Metal API
- `<MetalKit/MetalKit.h>` - Metal view
- `imgui_impl_osx.h` - ImGui macOS backend
- `imgui_impl_metal.h` - ImGui Metal renderer

**Features**:
- Retina display support (backingScaleFactor)
- San Francisco system font
- Metal rendering pipeline
- NSApplication with app bundle

## CMake Configuration

### Windows Build

```cmake
if(WIN32)
    add_executable(app_desktop_whisper WIN32
        src/ui/main_windows.cpp
        src/ui/app_window.cpp
    )
    target_link_libraries(app_desktop_whisper PRIVATE
        imgui
        core_lib
        ole32 uuid Shcore  # Windows-specific libs
    )
endif()
```

### macOS Build

```cmake
if(APPLE)
    add_executable(app_desktop_whisper MACOSX_BUNDLE
        src/ui/main_macos.mm
        src/ui/app_window.cpp
    )
    target_link_libraries(app_desktop_whisper PRIVATE
        imgui
        core_lib
        "-framework Cocoa"
        "-framework Metal"
        "-framework MetalKit"
        "-framework QuartzCore"
    )
    set_target_properties(app_desktop_whisper PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Desktop Live Whisper"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.vam.desktoplivewhisper"
    )
endif()
```

## ImGui Backend Selection

### Windows
- **Window System**: Win32 (`imgui_impl_win32.cpp`)
- **Renderer**: DirectX 11 (`imgui_impl_dx11.cpp`)
- **Dependencies**: `d3d11.lib`, `dxgi.lib`, `d3dcompiler.lib`

### macOS
- **Window System**: OSX/Cocoa (`imgui_impl_osx.mm`)
- **Renderer**: Metal (`imgui_impl_metal.mm`)
- **Dependencies**: Cocoa, Metal, MetalKit, QuartzCore frameworks

## Font Loading

### Windows
```cpp
// Load TrueType font from Windows system fonts
io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", 16.0f * dpi_scale);
```

### macOS
```objc
// Use San Francisco system font (native look)
io.Fonts->AddFontDefault();  // ImGui will use system font on macOS
```

## DPI/Retina Handling

### Windows
```cpp
UINT dpi = GetDpiForWindow(hwnd);
float dpi_scale = dpi / 96.0f;  // 96 DPI is 100% scaling
```

### macOS
```objc
CGFloat framebufferScale = view.window.screen.backingScaleFactor;
io.DisplayFramebufferScale = ImVec2(framebufferScale, framebufferScale);
```

## Build Commands

### Windows
```powershell
. .\Enter-VSDev.ps1  # Load MSVC environment
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_APP=ON
cmake --build build --target app_desktop_whisper
.\build\app_desktop_whisper.exe
```

### macOS
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_APP=ON
cmake --build build --target app_desktop_whisper
open build/app_desktop_whisper.app
```

## Testing Cross-Platform Compatibility

### What to Check

1. **Compilation**: Both platforms compile without errors
2. **Headers**: No platform leakage in shared code
3. **UI Rendering**: ImGui displays correctly on both platforms
4. **Font Quality**: Sharp text on high-DPI displays
5. **Window Resize**: Smooth redraw without artifacts
6. **Performance**: 60 FPS on both platforms

### Known Issues

- ⚠️ **macOS not tested yet** - only Windows implementation validated
- ⚠️ **Audio**: Windows uses WASAPI, macOS needs CoreAudio implementation
- ⚠️ **File paths**: Need platform-agnostic path handling

## Future Work

### macOS Development

1. Create `audio/macos_coreaudio.mm` for microphone capture
2. Test Metal rendering on macOS
3. Validate font rendering on Retina displays
4. Test app bundle creation and signing
5. Implement macOS-specific audio file dialog

### Cross-Platform Audio

Create platform-independent audio interface:

```cpp
// src/audio/audio_interface.hpp
class IAudioCapture {
public:
    virtual bool Start() = 0;
    virtual bool Stop() = 0;
    virtual std::vector<float> GetBuffer() = 0;
};

// Windows: audio/windows_wasapi.cpp implements IAudioCapture
// macOS: audio/macos_coreaudio.mm implements IAudioCapture
```

## References

- [ImGui Backends](https://github.com/ocornut/imgui/tree/master/backends)
- [DirectX 11 Documentation](https://docs.microsoft.com/en-us/windows/win32/direct3d11/atoc-dx-graphics-direct3d-11)
- [Metal Documentation](https://developer.apple.com/metal/)
- [High DPI Support in ImGui](https://github.com/ocornut/imgui/issues/1676)
