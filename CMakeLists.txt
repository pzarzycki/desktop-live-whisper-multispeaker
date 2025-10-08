cmake_minimum_required(VERSION 3.24)

project(desktop_live_whisper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to console-only build; UI can be enabled explicitly later
option(BUILD_APP "Build the full Qt application and libraries" OFF)

# Disable tests by default; enable when you want them
set(BUILD_TESTING OFF CACHE BOOL "Enable CTest and test targets")
include(CTest)

if(BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)

    add_library(core_lib
        src/core/logging.cpp
        src/core/config.cpp
    )
    target_include_directories(core_lib PUBLIC src)
    target_link_libraries(core_lib PUBLIC Qt6::Core)

    add_library(audio_windows STATIC
        src/audio/windows_wasapi.cpp
    )
    target_include_directories(audio_windows PUBLIC src)
    target_link_libraries(audio_windows PUBLIC core_lib Qt6::Core)

    add_library(asr_whisper STATIC
        src/asr/whisper_backend.cpp
    )
    target_include_directories(asr_whisper PUBLIC src)
    target_link_libraries(asr_whisper PUBLIC core_lib Qt6::Core)
    # Whisper linking for UI build can be added when BUILD_APP is used

    # Diarization omitted in simplified build

    qt_standard_project_setup(REQUIRES 6.6)

    qt_add_executable(app_desktop_whisper
        src/ui/main.cpp
    )
    target_link_libraries(app_desktop_whisper PRIVATE
        Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
        core_lib audio_windows asr_whisper
    )
    if (WIN32 AND MSVC)
        target_link_libraries(app_desktop_whisper PRIVATE ole32 uuid)
    endif()

    qt_add_qml_module(app_desktop_whisper
        URI App
        VERSION 1.0
        QML_FILES src/ui/qml/Main.qml
    )

    install(TARGETS app_desktop_whisper)
endif()

# Console app path when not building Qt app
if(NOT BUILD_APP)
    # asr_whisper without Qt; optional whisper.cpp via third_party
    add_library(asr_whisper STATIC
        src/asr/whisper_backend.cpp
    )
    target_include_directories(asr_whisper PUBLIC src)
    # Try vcpkg-provided whisper-cpp first
    find_package(whisper-cpp CONFIG QUIET)
    if (whisper-cpp_FOUND)
        target_link_libraries(asr_whisper PUBLIC whisper-cpp::whisper-cpp)
        target_compile_definitions(asr_whisper PUBLIC WHISPER_BACKEND_AVAILABLE=1)
        message(STATUS "Using whisper-cpp from vcpkg")
    elseif (EXISTS "${CMAKE_SOURCE_DIR}/third_party/whisper.cpp/CMakeLists.txt")
        # Build third_party whisper and ggml as static to avoid runtime DLL issues
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libs" FORCE)
        
        # Enable OpenBLAS if available locally
        if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/openblas/bin/libopenblas.dll")
            set(GGML_BLAS ON CACHE BOOL "Enable BLAS" FORCE)
            set(BLA_VENDOR "OpenBLAS" CACHE STRING "BLAS vendor" FORCE)
            set(GGML_BLAS_VENDOR "OpenBLAS" CACHE STRING "GGML BLAS vendor" FORCE)
            # Use import library for DLL (.lib for MSVC, or .dll.a for MinGW)
            if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/openblas/libopenblas.lib")
                set(BLAS_LIBRARIES "${CMAKE_SOURCE_DIR}/third_party/openblas/libopenblas.lib" CACHE FILEPATH "BLAS library" FORCE)
            else()
                set(BLAS_LIBRARIES "${CMAKE_SOURCE_DIR}/third_party/openblas/lib/libopenblas.dll.a" CACHE FILEPATH "BLAS library" FORCE)
            endif()
            set(BLAS_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/third_party/openblas/include" CACHE PATH "BLAS include dir" FORCE)
            message(STATUS "Enabling BLAS with local OpenBLAS DLL at ${CMAKE_SOURCE_DIR}/third_party/openblas")
        endif()
        
        add_subdirectory(third_party/whisper.cpp EXCLUDE_FROM_ALL)
        target_link_libraries(asr_whisper PUBLIC whisper)
        target_compile_definitions(asr_whisper PUBLIC WHISPER_BACKEND_AVAILABLE=1)
        target_include_directories(asr_whisper PUBLIC third_party/whisper.cpp)
        message(STATUS "Using third_party/whisper.cpp")
    else()
        message(FATAL_ERROR "Whisper backend required but not found. Either install whisper-cpp via vcpkg or add the vendored submodule: git submodule add https://github.com/ggml-org/whisper.cpp third_party/whisper.cpp && git submodule update --init --recursive")
    endif()

    # Minimal WASAPI audio library for mic input and output (no Qt dependency)
    add_library(audio_windows STATIC
        src/audio/windows_wasapi.cpp
        src/audio/windows_wasapi_out.cpp
    )
    target_include_directories(audio_windows PUBLIC src)
    if (WIN32 AND MSVC)
        target_link_libraries(audio_windows PRIVATE ole32 uuid)
    endif()

    # Ensure output TU is compiled as C++
    set_source_files_properties(src/audio/windows_wasapi_out.cpp PROPERTIES LANGUAGE CXX)

    # Speaker diarization library
    add_library(diarization STATIC
        src/diar/speaker_cluster.cpp
        src/diar/onnx_embedder.cpp
        src/diar/mel_features.cpp
    )
    target_include_directories(diarization PUBLIC src)
    
    # Link ONNX Runtime (using prebuilt binaries from third_party)
    set(ONNXRUNTIME_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime")
    if (EXISTS "${ONNXRUNTIME_ROOT}/include")
        target_include_directories(diarization PRIVATE "${ONNXRUNTIME_ROOT}/include")
        target_link_libraries(diarization PUBLIC "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
        target_compile_definitions(diarization PUBLIC ONNX_AVAILABLE=1)
        message(STATUS "Using ONNX Runtime from ${ONNXRUNTIME_ROOT}")
        
        # Copy ONNX Runtime DLLs to output directory
        add_custom_command(TARGET diarization POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
                "$<TARGET_FILE_DIR:diarization>"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:diarization>"
        )
        
        # Copy OpenBLAS DLL if available (needed by whisper.cpp)
        if (EXISTS "${CMAKE_SOURCE_DIR}/third_party/openblas/bin/libopenblas.dll")
            add_custom_command(TARGET diarization POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${CMAKE_SOURCE_DIR}/third_party/openblas/bin/libopenblas.dll"
                    "$<TARGET_FILE_DIR:diarization>"
            )
        endif()
    else()
        message(FATAL_ERROR "ONNX Runtime not found. Run: ./scripts/download_onnxruntime.ps1")
    endif()

    add_executable(app_transcribe_file
        src/console/transcribe_file.cpp
        src/audio/file_capture.cpp
    )
    # Safeguard: ensure this TU is compiled as C++
    set_source_files_properties(src/console/transcribe_file.cpp PROPERTIES LANGUAGE CXX)
    target_compile_features(app_transcribe_file PRIVATE cxx_std_20)
    target_include_directories(app_transcribe_file PRIVATE src)
    target_link_libraries(app_transcribe_file PRIVATE asr_whisper audio_windows diarization)

    # Test application for word-level timestamps
    add_executable(test_word_timestamps
        apps/test_word_timestamps.cpp
        src/audio/file_capture.cpp
    )
    target_compile_features(test_word_timestamps PRIVATE cxx_std_20)
    target_include_directories(test_word_timestamps PRIVATE src)
    target_link_libraries(test_word_timestamps PRIVATE asr_whisper audio_windows)

    # Test application for word-level speaker mapping (Phase 3 Step 2)
    add_executable(test_word_speaker_mapping
        apps/test_word_speaker_mapping.cpp
        src/audio/file_capture.cpp
    )
    target_compile_features(test_word_speaker_mapping PRIVATE cxx_std_20)
    target_include_directories(test_word_speaker_mapping PRIVATE src)
    target_link_libraries(test_word_speaker_mapping PRIVATE asr_whisper audio_windows diarization)

    # Diagnostic tool for embedding quality analysis
    add_executable(test_embedding_quality
        apps/test_embedding_quality.cpp
        src/audio/file_capture.cpp
    )
    target_compile_features(test_embedding_quality PRIVATE cxx_std_20)
    target_include_directories(test_embedding_quality PRIVATE src)
    target_link_libraries(test_embedding_quality PRIVATE audio_windows diarization)
    
    # Word-level clustering test (the RIGHT approach!)
    add_executable(test_word_clustering
        apps/test_word_clustering.cpp
        src/audio/file_capture.cpp
    )
    target_compile_features(test_word_clustering PRIVATE cxx_std_20)
    target_include_directories(test_word_clustering PRIVATE src)
    target_link_libraries(test_word_clustering PRIVATE audio_windows asr_whisper diarization)
    
    # Word-level clustering V2 (clean sequential)
    add_executable(test_word_clustering_v2
        apps/test_word_clustering_v2.cpp
        src/audio/file_capture.cpp
    )
    target_compile_features(test_word_clustering_v2 PRIVATE cxx_std_20)
    target_include_directories(test_word_clustering_v2 PRIVATE src)
    target_link_libraries(test_word_clustering_v2 PRIVATE audio_windows asr_whisper diarization)
    
    # Boundary detection approach
    add_executable(test_boundary_detection
        apps/test_boundary_detection.cpp
        src/audio/file_capture.cpp
    )
    target_compile_features(test_boundary_detection PRIVATE cxx_std_20)
    target_include_directories(test_boundary_detection PRIVATE src)
    target_link_libraries(test_boundary_detection PRIVATE audio_windows asr_whisper diarization)
    
    # Segment-level speaker assignment (BEST approach!)
    add_executable(test_segment_speakers
        apps/test_segment_speakers.cpp
        src/audio/file_capture.cpp
    )
    target_compile_features(test_segment_speakers PRIVATE cxx_std_20)
    target_include_directories(test_segment_speakers PRIVATE src)
    target_link_libraries(test_segment_speakers PRIVATE audio_windows asr_whisper diarization)
    
    # Frame-level voting within segments
    add_executable(test_frame_voting
        apps/test_frame_voting.cpp
        src/audio/file_capture.cpp
    )
    target_compile_features(test_frame_voting PRIVATE cxx_std_20)
    target_include_directories(test_frame_voting PRIVATE src)
    target_link_libraries(test_frame_voting PRIVATE audio_windows asr_whisper diarization)
endif()


# Tests can be enabled by setting -DBUILD_TESTING=ON; no test targets by default
