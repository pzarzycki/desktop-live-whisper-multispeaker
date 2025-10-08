# Dear ImGui Integration Guide

## What is Dear ImGui?

Dear ImGui is an immediate-mode GUI library for C++:
- **License:** MIT (fully commercial-friendly, no restrictions!)
- **Size:** ~30 KB compiled
- **Style:** Modern, customizable, game-like UI
- **Platforms:** Windows, macOS, Linux
- **Rendering:** DirectX 11/12, OpenGL, Metal, Vulkan

## Installation (No Package Manager Needed!)

ImGui is distributed as source files - just copy into project.

### Method 1: Git Submodule (Recommended)

```powershell
# Add ImGui as submodule
git submodule add https://github.com/ocornut/imgui.git third_party/imgui
git submodule update --init --recursive
```

### Method 2: Direct Download

1. Download: https://github.com/ocornut/imgui/archive/refs/heads/master.zip
2. Extract to `third_party/imgui/`

**Files we need:**
```
third_party/imgui/
├── imgui.h
├── imgui.cpp
├── imgui_draw.cpp
├── imgui_tables.cpp
├── imgui_widgets.cpp
├── imgui_demo.cpp (optional, for examples)
├── backends/
│   ├── imgui_impl_win32.h/.cpp      (Windows)
│   ├── imgui_impl_dx11.h/.cpp       (DirectX 11 rendering)
│   ├── imgui_impl_osx.h/.mm         (macOS)
│   └── imgui_impl_metal.h/.mm       (Metal rendering)
```

**Total size:** ~500 KB source, compiles to ~30 KB

## CMake Configuration

```cmake
# Add ImGui library
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
)

target_include_directories(imgui PUBLIC third_party/imgui)

# Windows backend
if(WIN32)
    target_sources(imgui PRIVATE
        third_party/imgui/backends/imgui_impl_win32.cpp
        third_party/imgui/backends/imgui_impl_dx11.cpp
    )
    target_link_libraries(imgui PUBLIC d3d11 dxgi)
endif()

# macOS backend
if(APPLE)
    target_sources(imgui PRIVATE
        third_party/imgui/backends/imgui_impl_osx.mm
        third_party/imgui/backends/imgui_impl_metal.mm
    )
    target_link_libraries(imgui PUBLIC "-framework Metal" "-framework MetalKit" "-framework Cocoa")
endif()

# Desktop app using ImGui
add_executable(app_desktop_whisper
    src/ui/main_imgui.cpp
    src/ui/app_window.cpp
)

target_link_libraries(app_desktop_whisper PRIVATE
    imgui
    app_controller
    audio_windows
    asr_whisper
)
```

## Advantages Over Qt

### Licensing
- ✅ **MIT License** - use commercially without restrictions
- ✅ No LGPL obligations
- ✅ No per-developer fees
- ✅ No source disclosure required

### Size
- ✅ **30 KB** (vs Qt's 20 MB DLLs)
- ✅ Static linking - single .exe
- ✅ No runtime dependencies

### Development
- ✅ Simpler API (immediate mode)
- ✅ No separate UI description language (no QML)
- ✅ All C++ (no meta-object compiler)
- ✅ Fast compilation

### Deployment
- ✅ Single executable
- ✅ No DLL deployment
- ✅ Smaller installers (~100 MB vs ~120 MB)

## Basic ImGui Code Structure

```cpp
// main_imgui.cpp
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

int main() {
    // Create window (Win32/DirectX)
    CreateWindowAndDevice();
    
    // Setup ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    
    // Setup backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, device_context);
    
    // Main loop
    while (!done) {
        // Start frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        
        // Draw UI
        DrawMainWindow();
        
        // Render
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        swap_chain->Present(1, 0);
    }
    
    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void DrawMainWindow() {
    ImGui::Begin("Desktop Live Whisper");
    
    // Start/Stop button
    if (ImGui::Button(is_recording ? "STOP RECORDING" : "START RECORDING", 
                     ImVec2(200, 50))) {
        ToggleRecording();
    }
    
    // Transcript
    ImGui::BeginChild("Transcript", ImVec2(0, -100));
    for (const auto& chunk : chunks) {
        ImGui::PushStyleColor(ImGuiCol_Text, 
            chunk.speaker_id == 0 ? BLUE : RED);
        ImGui::TextWrapped("%s", chunk.text.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    
    // Settings
    ImGui::Checkbox("Synthetic Audio", &use_synthetic);
    ImGui::InputText("Audio File", audio_file, 256);
    
    ImGui::End();
}
```

## Comparison: QML vs ImGui

**QML (Declarative):**
```qml
Button {
    text: bridge.isRecording ? "STOP" : "START"
    onClicked: bridge.startRecording()
}
```

**ImGui (Immediate Mode):**
```cpp
if (ImGui::Button(is_recording ? "STOP" : "START")) {
    StartRecording();
}
```

**ImGui is simpler** - no separate language, no bindings needed!

## Next Steps

1. Add ImGui as submodule
2. Update CMakeLists.txt
3. Replace `src/ui/main.cpp` with ImGui version
4. Remove Qt-specific files (main.qml, transcription_bridge)
5. Implement `AppWindow` class with ImGui
6. Test on Windows
7. Add macOS support

**Estimated time:** 1-2 days (vs 3-5 days for Qt setup)

## References

- Official repo: https://github.com/ocornut/imgui
- Documentation: https://github.com/ocornut/imgui/wiki
- Examples: https://github.com/ocornut/imgui/tree/master/examples
- Demo: Run `imgui_demo.cpp` to see all widgets
