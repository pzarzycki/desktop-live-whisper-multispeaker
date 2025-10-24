cmake_minimum_required(VERSION 3.24)
project(desktop_live_whisper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# THIRD-PARTY DEPENDENCIES
# =============================================================================

# Whisper.cpp for speech-to-text
if (NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/whisper.cpp/CMakeLists.txt")
    message(FATAL_ERROR "Whisper.cpp not found. Run: git submodule update --init --recursive")
endif()

# Enable OpenBLAS if available (Windows only for now)
if (WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/openblas/bin/libopenblas.dll")
    set(GGML_BLAS ON CACHE BOOL "Enable BLAS" FORCE)
    set(BLA_VENDOR "OpenBLAS" CACHE STRING "BLAS vendor" FORCE)
    set(GGML_BLAS_VENDOR "OpenBLAS" CACHE STRING "GGML BLAS vendor" FORCE)
    if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/openblas/libopenblas.lib")
        set(BLAS_LIBRARIES "${CMAKE_SOURCE_DIR}/third_party/openblas/libopenblas.lib" CACHE FILEPATH "BLAS library" FORCE)
    endif()
    set(BLAS_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/third_party/openblas/include" CACHE PATH "BLAS include dir" FORCE)
    message(STATUS "Using OpenBLAS from third_party/openblas")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libs" FORCE)
add_subdirectory(third_party/whisper.cpp EXCLUDE_FROM_ALL)

# ONNX Runtime for speaker embeddings
if(WIN32)
    set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-windows")
elseif(APPLE)
    set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-macos")
else()
    set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
endif()
if (NOT EXISTS "${ONNXRUNTIME_ROOT}/include")
    if(WIN32)
        message(FATAL_ERROR "ONNX Runtime not found. Run: .\\scripts\\download_onnxruntime.ps1")
    elseif(APPLE)
        message(FATAL_ERROR "ONNX Runtime not found. Run: ./scripts/download_onnxruntime_macos.sh")
    else()
        message(FATAL_ERROR "ONNX Runtime not found. Please download manually for your platform.")
    endif()
endif()

# Dear ImGui for GUI
add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
)
target_include_directories(imgui PUBLIC third_party/imgui)

if(WIN32)
    target_sources(imgui PRIVATE
        third_party/imgui/backends/imgui_impl_win32.cpp
        third_party/imgui/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui PUBLIC third_party/imgui/backends)
    target_link_libraries(imgui PUBLIC d3d11 dxgi d3dcompiler)
elseif(APPLE)
    target_sources(imgui PRIVATE
        third_party/imgui/backends/imgui_impl_osx.mm
        third_party/imgui/backends/imgui_impl_metal.mm
    )
    target_include_directories(imgui PUBLIC third_party/imgui/backends)
    target_link_libraries(imgui PUBLIC 
        "-framework Metal" "-framework MetalKit" 
        "-framework Cocoa" "-framework QuartzCore"
    )
endif()

# =============================================================================
# AUDIO LIBRARY (Platform-specific audio I/O)
# =============================================================================

add_library(audio STATIC
    src/audio/file_capture.cpp
    src/audio/drwav_resample.cpp
    src/audio/audio_input_device.cpp
    src/audio/audio_input_device_synthetic.cpp
)

if(WIN32)
    target_sources(audio PRIVATE
        src/audio/win/windows_wasapi.cpp
        src/audio/win/windows_wasapi_out.cpp
        src/audio/win/audio_input_device_windows.cpp
    )
    target_link_libraries(audio PRIVATE ole32 uuid)
elseif(APPLE)
    target_sources(audio PRIVATE
        src/audio/mac/audio_input_device_macos.mm
        src/audio/mac/coreaudio_output.mm
    )
    target_link_libraries(audio PRIVATE 
        "-framework CoreAudio" 
        "-framework AudioToolbox"
    )
endif()

target_include_directories(audio PUBLIC src)

# =============================================================================
# ASR LIBRARY (Whisper backend)
# =============================================================================

add_library(asr STATIC
    src/asr/whisper_backend.cpp
)
target_include_directories(asr PUBLIC src)
target_link_libraries(asr PUBLIC whisper)
target_compile_definitions(asr PUBLIC WHISPER_BACKEND_AVAILABLE=1)

# =============================================================================
# DIARIZATION LIBRARY (Speaker identification)
# =============================================================================

add_library(diarization STATIC
    src/diar/speaker_cluster.cpp
    src/diar/onnx_embedder.cpp
    src/diar/mel_features.cpp
)
target_include_directories(diarization PUBLIC src)
target_include_directories(diarization PRIVATE "${ONNXRUNTIME_ROOT}/include")

if(WIN32)
    target_link_libraries(diarization PUBLIC "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
elseif(APPLE)
    target_link_libraries(diarization PUBLIC "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib")
endif()

target_compile_definitions(diarization PUBLIC ONNX_AVAILABLE=1)

# Copy ONNX Runtime DLLs to output directory
if(WIN32)
    add_custom_command(TARGET diarization POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
            "$<TARGET_FILE_DIR:diarization>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll"
            "$<TARGET_FILE_DIR:diarization>"
    )
elseif(APPLE)
    # On macOS, copy dylib and update rpath
    add_custom_command(TARGET diarization POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ONNXRUNTIME_ROOT}/lib/libonnxruntime.1.20.1.dylib"
            "$<TARGET_FILE_DIR:diarization>"
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            "libonnxruntime.1.20.1.dylib"
            "$<TARGET_FILE_DIR:diarization>/libonnxruntime.dylib"
    )
endif()

# Copy OpenBLAS DLL if available
if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/openblas/bin/libopenblas.dll")
    add_custom_command(TARGET diarization POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/third_party/openblas/bin/libopenblas.dll"
            "$<TARGET_FILE_DIR:diarization>"
    )
endif()

# =============================================================================
# CORE LIBRARY (TranscriptionController - Application logic)
# =============================================================================

add_library(core STATIC
    src/core/logging.cpp
    src/core/config.cpp
    src/core/transcription_controller.cpp
)
target_include_directories(core PUBLIC src)
target_link_libraries(core PUBLIC asr diarization audio)

# =============================================================================
# DESKTOP APPLICATION (GUI)
# =============================================================================

if(WIN32)
    add_executable(app_desktop_whisper WIN32
        src/ui/main_windows.cpp
        src/ui/app_window.cpp
    )
    target_link_libraries(app_desktop_whisper PRIVATE
        imgui core
        ole32 uuid Shcore
    )
elseif(APPLE)
    add_executable(app_desktop_whisper MACOSX_BUNDLE
        src/ui/main_macos.mm
        src/ui/app_window.cpp
    )
    target_link_libraries(app_desktop_whisper PRIVATE
        imgui core audio asr diarization
        "-framework Cocoa" "-framework Metal" 
        "-framework MetalKit" "-framework QuartzCore"
        "-framework GameController"
    )
    set_target_properties(app_desktop_whisper PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Desktop Live Whisper"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.vam.desktoplivewhisper"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
endif()

if(APPLE)
    install(TARGETS app_desktop_whisper BUNDLE DESTINATION .)
else()
    install(TARGETS app_desktop_whisper)
endif()

# =============================================================================
# TEST PROGRAMS (Optional - only for debugging)
# =============================================================================

option(BUILD_TESTS "Build test programs" ON)

if(BUILD_TESTS)
    # Test: TranscriptionController integration (PRIMARY TEST)
    # This is the main test that validates the complete system:
    # - Uses TranscriptionController (core/transcription_controller.cpp)
    # - Tests audio input (synthetic file playback with speaker output)
    # - Validates Whisper transcription + speaker diarization
    # - Platform: Windows & macOS fully supported
    add_executable(test_transcription
        apps/test_transcription_controller.cpp
    )
    target_compile_features(test_transcription PRIVATE cxx_std_20)
    target_include_directories(test_transcription PRIVATE src)
    target_link_libraries(test_transcription PRIVATE core audio asr diarization)
    
    # Test: Audio device enumeration and basic capture
    # Useful for debugging audio input issues
    add_executable(test_audio_device
        apps/test_audio_device.cpp
    )
    target_compile_features(test_audio_device PRIVATE cxx_std_20)
    target_include_directories(test_audio_device PRIVATE src)
    target_link_libraries(test_audio_device PRIVATE audio)
    
    # macOS needs CoreFoundation for CFString operations
    if(APPLE)
        target_link_libraries(test_audio_device PRIVATE "-framework CoreFoundation")
    endif()
    
    # NOTE: test_controller_api is DISABLED (uses deprecated app/transcription_controller.hpp)
    # The current production API is core/transcription_controller.hpp (tested by test_transcription)
    # Uncomment below if you need to build the legacy test:
    #
    # add_executable(test_controller_api
    #     apps/test_controller_api.cpp
    # )
    # target_compile_features(test_controller_api PRIVATE cxx_std_20)
    # target_include_directories(test_controller_api PRIVATE src)
    # target_link_libraries(test_controller_api PRIVATE core audio asr diarization)
endif()

# =============================================================================
# SUMMARY
# =============================================================================

message(STATUS "")
message(STATUS "===============================================")
message(STATUS "Desktop Live Whisper - Configuration Summary")
message(STATUS "===============================================")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "Whisper: third_party/whisper.cpp")
message(STATUS "ONNX Runtime: ${ONNXRUNTIME_ROOT}")
if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/openblas/bin/libopenblas.dll")
    message(STATUS "OpenBLAS: ENABLED (third_party/openblas)")
else()
    message(STATUS "OpenBLAS: DISABLED")
endif()
message(STATUS "===============================================")
message(STATUS "")
