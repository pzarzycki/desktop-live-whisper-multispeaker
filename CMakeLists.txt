cmake_minimum_required(VERSION 3.24)

project(desktop_live_whisper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to console-only build; UI can be enabled explicitly later
option(BUILD_APP "Build the full Qt application and libraries" OFF)
option(USE_THIRD_PARTY_WHISPER "Link vendored third_party/whisper.cpp" OFF)

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
    elseif (USE_THIRD_PARTY_WHISPER AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/whisper.cpp/CMakeLists.txt")
        # Build third_party whisper and ggml as static to avoid runtime DLL issues
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libs" FORCE)
        add_subdirectory(third_party/whisper.cpp EXCLUDE_FROM_ALL)
        target_link_libraries(asr_whisper PUBLIC whisper)
        target_compile_definitions(asr_whisper PUBLIC WHISPER_BACKEND_AVAILABLE=1)
        target_include_directories(asr_whisper PUBLIC third_party/whisper.cpp)
        message(STATUS "Using third_party/whisper.cpp")
    else()
        message(STATUS "Whisper backend not available (no vcpkg whisper-cpp and no third_party/whisper.cpp). Building no-op backend.")
    endif()

    add_executable(app_transcribe_file src/console/transcribe_file.cpp)
    target_include_directories(app_transcribe_file PRIVATE src)
    target_link_libraries(app_transcribe_file PRIVATE asr_whisper)
endif()


# Tests can be enabled by setting -DBUILD_TESTING=ON; no test targets by default
