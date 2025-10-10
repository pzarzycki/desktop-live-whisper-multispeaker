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

# Enable OpenBLAS if available
if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/openblas/bin/libopenblas.dll")
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
set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
if (NOT EXISTS "${ONNXRUNTIME_ROOT}/include")
    message(FATAL_ERROR "ONNX Runtime not found. Run: ./scripts/download_onnxruntime.ps1")
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
        src/audio/windows_wasapi.cpp
        src/audio/windows_wasapi_out.cpp
        src/audio/win/audio_input_device_windows.cpp
    )
    target_link_libraries(audio PRIVATE ole32 uuid)
elseif(APPLE)
    # TODO: Add macOS CoreAudio implementation
    target_sources(audio PRIVATE
        # src/audio/mac/audio_input_device_macos.cpp
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
target_link_libraries(diarization PUBLIC "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
target_compile_definitions(diarization PUBLIC ONNX_AVAILABLE=1)

# Copy ONNX Runtime DLLs to output directory
add_custom_command(TARGET diarization POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
        "$<TARGET_FILE_DIR:diarization>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll"
        "$<TARGET_FILE_DIR:diarization>"
)

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
        imgui core
        "-framework Cocoa" "-framework Metal" 
        "-framework MetalKit" "-framework QuartzCore"
    )
    set_target_properties(app_desktop_whisper PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "Desktop Live Whisper"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.vam.desktoplivewhisper"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0"
    )
endif()

install(TARGETS app_desktop_whisper)

# =============================================================================
# TEST PROGRAMS (Optional - only for debugging)
# =============================================================================

option(BUILD_TESTS "Build test programs" ON)

if(BUILD_TESTS)
    # Test: Audio device playback
    add_executable(test_audio_device
        apps/test_audio_device.cpp
    )
    target_compile_features(test_audio_device PRIVATE cxx_std_20)
    target_include_directories(test_audio_device PRIVATE src)
    target_link_libraries(test_audio_device PRIVATE audio)
    
    # Test: TranscriptionController integration
    add_executable(test_transcription
        apps/test_transcription_controller.cpp
    )
    target_compile_features(test_transcription PRIVATE cxx_std_20)
    target_include_directories(test_transcription PRIVATE src)
    target_link_libraries(test_transcription PRIVATE core audio asr diarization)
    
    # Test: File transcription (old working reference)
    add_executable(test_transcribe_file
        src/console/transcribe_file.cpp
        src/audio/file_capture.cpp
    )
    set_source_files_properties(src/console/transcribe_file.cpp PROPERTIES LANGUAGE CXX)
    target_compile_features(test_transcribe_file PRIVATE cxx_std_20)
    target_include_directories(test_transcribe_file PRIVATE src)
    target_link_libraries(test_transcribe_file PRIVATE asr audio diarization)
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
