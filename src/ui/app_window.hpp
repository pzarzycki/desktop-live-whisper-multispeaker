// Copyright (c) 2025 VAM Desktop Live Whisper
// Main application window using Dear ImGui

#pragma once

#include "core/transcription_controller.hpp"
#include "audio/audio_input_device.hpp"
#include <vector>
#include <string>
#include <memory>
#include <cstdint>

// Forward declaration for ImGui type
struct ImVec4;

class AppWindow {
public:
    AppWindow();
    ~AppWindow();

    // Main rendering function
    void Render();

    // Check if window should close
    bool ShouldClose() const { return should_close_; }

private:
    // UI state
    bool is_recording_ = false;
    bool show_settings_ = false;  // Settings window visibility
    bool use_synthetic_audio_ = true;
    char audio_file_path_[256] = "test_data/Sean_Carroll_podcast_16k.wav";
    char whisper_model_[64] = "tiny.en";
    int max_speakers_ = 2;
    float speaker_threshold_ = 0.35f;
    bool should_close_ = false;

    // Transcript data
    struct TranscriptChunk {
        uint64_t id;
        std::string text;
        int speaker_id;
        int64_t timestamp_ms;
        float confidence;
    };
    std::vector<TranscriptChunk> transcript_chunks_;

    // Status
    std::string status_text_ = "Ready";
    int64_t elapsed_ms_ = 0;
    int chunk_count_ = 0;
    int reclassification_count_ = 0;

    // Controller and audio device
    std::unique_ptr<core::TranscriptionController> controller_;
    std::unique_ptr<audio::IAudioInputDevice> audio_device_;

    // UI rendering functions
    void RenderMainWindow();
    void RenderControlPanel();
    void RenderTranscriptView();
    void RenderSettingsWindow();  // Separate settings window
    void RenderStatusBar();

    // Event handlers
    void OnStartStopClicked();
    void OnClearClicked();
    void OnChunkReceived(const TranscriptChunk& chunk);
    void OnSpeakerReclassified(const std::vector<uint64_t>& chunk_ids, 
                                int old_speaker, int new_speaker);
    void OnStatusChanged(int64_t elapsed, int chunks, int recls);

    // Helpers
    ImVec4 GetSpeakerColor(int speaker_id) const;
    std::string FormatTime(int64_t ms) const;
};
