// Copyright (c) 2025 VAM Desktop Live Whisper
// Main application window implementation

#include "app_window.hpp"
#include "imgui.h"
#include <sstream>
#include <iomanip>

// Speaker colors (Blue and Red)
static const ImVec4 SPEAKER_0_COLOR = ImVec4(0.29f, 0.62f, 1.0f, 1.0f);  // #4A9EFF
static const ImVec4 SPEAKER_1_COLOR = ImVec4(1.0f, 0.42f, 0.42f, 1.0f);  // #FF6B6B
static const ImVec4 SPEAKER_2_COLOR = ImVec4(0.31f, 0.80f, 0.77f, 1.0f); // #4ECDC4
static const ImVec4 SPEAKER_3_COLOR = ImVec4(1.0f, 0.90f, 0.43f, 1.0f);  // #FFE66D

AppWindow::AppWindow() 
    : controller_(std::make_unique<TranscriptionController>()) {
    
    // Subscribe to controller events
    controller_->subscribe_to_chunks([this](const TranscriptionChunk& chunk) {
        TranscriptChunk ui_chunk;
        ui_chunk.id = chunk.id;
        ui_chunk.text = chunk.text;
        ui_chunk.speaker_id = chunk.speaker_id;
        ui_chunk.timestamp_ms = chunk.timestamp_ms;
        ui_chunk.confidence = chunk.speaker_confidence;
        OnChunkReceived(ui_chunk);
    });

    controller_->subscribe_to_reclassification(
        [this](const SpeakerReclassification& recl) {
            OnSpeakerReclassified(recl.chunk_ids, 
                                  recl.old_speaker_id, 
                                  recl.new_speaker_id);
        });

    controller_->subscribe_to_status([this](const TranscriptionStatus& status) {
        OnStatusChanged(status.elapsed_ms, 
                        status.chunks_emitted, 
                        status.reclassifications_count);
    });
}

AppWindow::~AppWindow() {
    if (is_recording_) {
        controller_->stop_transcription();
    }
}

void AppWindow::Render() {
    RenderMainWindow();
}

void AppWindow::RenderMainWindow() {
    // Configure window
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | 
                                     ImGuiWindowFlags_NoCollapse |
                                     ImGuiWindowFlags_NoResize | 
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus;
    
    ImGui::Begin("Desktop Live Whisper", nullptr, window_flags);
    
    // Title
    ImGui::SetWindowFontScale(1.2f);
    ImGui::Text("Desktop Live Whisper");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
    ImGui::Spacing();
    
    RenderControlPanel();
    ImGui::Spacing();
    
    RenderTranscriptView();
    ImGui::Spacing();
    
    RenderSettingsPanel();
    ImGui::Spacing();
    
    RenderStatusBar();
    
    ImGui::End();
}

void AppWindow::RenderControlPanel() {
    ImGui::Text("Control");
    ImGui::Separator();
    
    // Start/Stop button (large, colored)
    ImVec2 button_size(200, 50);
    
    if (is_recording_) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.47f, 0.8f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.1f, 0.57f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.37f, 0.7f, 1.0f));
    }
    
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button_size.x) * 0.5f);
    if (ImGui::Button(is_recording_ ? "STOP RECORDING" : "START RECORDING", button_size)) {
        OnStartStopClicked();
    }
    
    ImGui::PopStyleColor(3);
    
    // Clear button
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        OnClearClicked();
    }
}

void AppWindow::RenderTranscriptView() {
    ImGui::Text("Transcript");
    ImGui::Separator();
    
    // Scrollable region
    ImGui::BeginChild("TranscriptScroll", ImVec2(0, -250), true);
    
    if (transcript_chunks_.empty()) {
        ImGui::TextDisabled("Press START RECORDING to begin...");
    } else {
        for (const auto& chunk : transcript_chunks_) {
            // Speaker indicator
            ImVec4 color = GetSpeakerColor(chunk.speaker_id);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Text("[S%d]", chunk.speaker_id);
            ImGui::PopStyleColor();
            
            // Transcript text
            ImGui::SameLine();
            ImGui::TextWrapped("%s", chunk.text.c_str());
            
            // Timestamp and confidence (small, gray)
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::Text("  %s", FormatTime(chunk.timestamp_ms).c_str());
            if (chunk.confidence < 0.7f) {
                ImGui::SameLine();
                ImGui::Text("(low confidence: %.2f)", chunk.confidence);
            }
            ImGui::PopStyleColor();
            
            ImGui::Spacing();
        }
    }
    
    // Auto-scroll to bottom
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    
    ImGui::EndChild();
}

void AppWindow::RenderSettingsPanel() {
    ImGui::Text("Settings");
    ImGui::Separator();
    
    ImGui::BeginDisabled(is_recording_);
    
    ImGui::Checkbox("Synthetic Audio", &use_synthetic_audio_);
    
    ImGui::BeginDisabled(!use_synthetic_audio_);
    ImGui::InputText("Audio File", audio_file_path_, sizeof(audio_file_path_));
    ImGui::EndDisabled();
    
    ImGui::InputText("Model", whisper_model_, sizeof(whisper_model_));
    ImGui::SliderInt("Max Speakers", &max_speakers_, 1, 5);
    ImGui::SliderFloat("Speaker Threshold", &speaker_threshold_, 0.0f, 1.0f);
    
    ImGui::EndDisabled();
}

void AppWindow::RenderStatusBar() {
    ImGui::Separator();
    ImGui::Text("%s", status_text_.c_str());
}

void AppWindow::OnStartStopClicked() {
    if (is_recording_) {
        controller_->stop_transcription();
        is_recording_ = false;
        status_text_ = "Stopped";
    } else {
        TranscriptionConfig config;
        config.whisper_model = whisper_model_;
        config.max_speakers = max_speakers_;
        config.speaker_threshold = speaker_threshold_;
        config.enable_reclassification = true;
        
        // TODO: Add synthetic audio support to config
        
        if (controller_->start_transcription(config)) {
            is_recording_ = true;
            status_text_ = "Recording...";
        } else {
            status_text_ = "Failed to start recording";
        }
    }
}

void AppWindow::OnClearClicked() {
    transcript_chunks_.clear();
    controller_->clear_history();
    chunk_count_ = 0;
    reclassification_count_ = 0;
    elapsed_ms_ = 0;
    status_text_ = "Cleared";
}

void AppWindow::OnChunkReceived(const TranscriptChunk& chunk) {
    transcript_chunks_.push_back(chunk);
}

void AppWindow::OnSpeakerReclassified(const std::vector<uint64_t>& chunk_ids,
                                      int old_speaker, int new_speaker) {
    // Update chunks in transcript
    for (auto& chunk : transcript_chunks_) {
        for (uint64_t id : chunk_ids) {
            if (chunk.id == id) {
                chunk.speaker_id = new_speaker;
                break;
            }
        }
    }
}

void AppWindow::OnStatusChanged(int64_t elapsed, int chunks, int recls) {
    elapsed_ms_ = elapsed;
    chunk_count_ = chunks;
    reclassification_count_ = recls;
    
    std::ostringstream oss;
    oss << "Elapsed: " << FormatTime(elapsed) << " | "
        << "Chunks: " << chunks << " | "
        << "Reclassifications: " << recls;
    status_text_ = oss.str();
}

ImVec4 AppWindow::GetSpeakerColor(int speaker_id) const {
    switch (speaker_id) {
        case 0: return SPEAKER_0_COLOR;
        case 1: return SPEAKER_1_COLOR;
        case 2: return SPEAKER_2_COLOR;
        case 3: return SPEAKER_3_COLOR;
        default: return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    }
}

std::string AppWindow::FormatTime(int64_t ms) const {
    int seconds = static_cast<int>(ms / 1000);
    int minutes = seconds / 60;
    seconds = seconds % 60;
    
    std::ostringstream oss;
    oss << minutes << ":" << std::setfill('0') << std::setw(2) << seconds;
    return oss.str();
}
