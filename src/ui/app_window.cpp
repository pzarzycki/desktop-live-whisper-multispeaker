// Copyright (c) 2025 VAM Desktop Live Whisper
// Main application window implementation

#include "app_window.hpp"
#include "audio/audio_input_device.hpp"
#include "imgui.h"
#include <sstream>
#include <iomanip>
#include <iostream>

// Speaker colors (Blue and Red)
static const ImVec4 SPEAKER_0_COLOR = ImVec4(0.29f, 0.62f, 1.0f, 1.0f);  // #4A9EFF
static const ImVec4 SPEAKER_1_COLOR = ImVec4(1.0f, 0.42f, 0.42f, 1.0f);  // #FF6B6B
static const ImVec4 SPEAKER_2_COLOR = ImVec4(0.31f, 0.80f, 0.77f, 1.0f); // #4ECDC4
static const ImVec4 SPEAKER_3_COLOR = ImVec4(1.0f, 0.90f, 0.43f, 1.0f);  // #FFE66D

AppWindow::AppWindow() {
    // Create controller
    controller_ = std::make_unique<core::TranscriptionController>();
}

AppWindow::~AppWindow() {
    if (is_recording_) {
        controller_->stop();
    }
}

void AppWindow::Render() {
    RenderMainWindow();
    
    // Render settings window if open
    if (show_settings_) {
        RenderSettingsWindow();
    }
}

void AppWindow::RenderMainWindow() {
    // Configure window to fill the entire viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    
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
    
    RenderStatusBar();
    
    ImGui::End();
}

void AppWindow::RenderControlPanel() {
    ImGui::Text("Control");
    ImGui::Separator();
    
    // Start/Stop button (large, colored)
    ImVec2 button_size(200, 50);
    
    // Push rounded corner style for the main button
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    
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
    ImGui::PopStyleVar(1);  // Pop the FrameRounding style
    
    // Push rounded corner style for the other buttons
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
    
    // Clear button
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        OnClearClicked();
    }
    
    // Settings button
    ImGui::SameLine();
    if (ImGui::Button("Settings...")) {
        show_settings_ = true;
    }
    
    ImGui::PopStyleVar(1);  // Pop the FrameRounding style for other buttons
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

void AppWindow::RenderSettingsWindow() {
    // Settings window (closeable, resizable)
    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Settings", &show_settings_, ImGuiWindowFlags_None)) {
        ImGui::TextWrapped("Configure transcription settings. Changes take effect when you start recording.");
        ImGui::Separator();
        ImGui::Spacing();
        
        ImGui::BeginDisabled(is_recording_);
        
        // Audio Source
        ImGui::SeparatorText("Audio Source");
        ImGui::Checkbox("Use Synthetic Audio (for testing)", &use_synthetic_audio_);
        
        ImGui::BeginDisabled(!use_synthetic_audio_);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##AudioFile", audio_file_path_, sizeof(audio_file_path_));
        ImGui::TextDisabled("Path to .wav file (16kHz mono)");
        ImGui::EndDisabled();
        
        ImGui::Spacing();
        
        // Whisper Model
        ImGui::SeparatorText("Whisper Model");
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##Model", whisper_model_, sizeof(whisper_model_));
        ImGui::TextDisabled("Model: tiny.en, base.en, small.en, medium.en, large");
        
        ImGui::Spacing();
        
        // Speaker Diarization
        ImGui::SeparatorText("Speaker Diarization");
        ImGui::SliderInt("Max Speakers", &max_speakers_, 1, 5);
        ImGui::TextDisabled("Maximum number of speakers to detect");
        
        ImGui::SliderFloat("Speaker Threshold", &speaker_threshold_, 0.0f, 1.0f, "%.3f");
        ImGui::TextDisabled("Lower = more sensitive (may split speakers), Higher = less sensitive");
        
        ImGui::EndDisabled();
        
        ImGui::Spacing();
        ImGui::Separator();
        
        // Push rounded corner style for the close button
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        
        // Close button
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            show_settings_ = false;
        }
        
        ImGui::PopStyleVar(1);  // Pop the FrameRounding style
    }
    ImGui::End();
}

void AppWindow::RenderStatusBar() {
    ImGui::Separator();
    ImGui::Text("%s", status_text_.c_str());
}

void AppWindow::OnStartStopClicked() {
    if (is_recording_) {
        // Stop recording
        controller_->stop();
        audio_device_.reset();
        is_recording_ = false;
        status_text_ = "Stopped";
    } else {
        // Configure controller
        core::TranscriptionController::Config config;
        config.model_path = whisper_model_;  // Just the model name, not the path
        config.language = "en";
        config.n_threads = 0;  // Auto
        config.buffer_duration_s = 3;  // 3s buffer
        config.overlap_duration_s = 1;  // 1s overlap
        config.enable_diarization = true;
        config.max_speakers = max_speakers_;
        config.speaker_threshold = speaker_threshold_;
        
        // Set up callbacks
        config.on_segment = [this](const core::TranscriptionSegment& seg) {
            TranscriptChunk chunk;
            chunk.id = seg.start_ms;  // Use timestamp as ID
            chunk.text = seg.text;
            chunk.speaker_id = seg.speaker_id;
            chunk.timestamp_ms = seg.start_ms;
            chunk.confidence = 1.0f;  // Controller doesn't expose confidence yet
            OnChunkReceived(chunk);
        };
        
        config.on_status = [this](const std::string& msg, bool is_error) {
            if (is_error) {
                std::cerr << "[ERROR] " << msg << "\n";
                status_text_ = "Error: " + msg;
            } else {
                status_text_ = msg;
            }
        };
        
        // Initialize controller
        std::cout << "[GUI] Initializing controller with model: " << config.model_path << "\n";
        std::cout << "[GUI] Max speakers: " << config.max_speakers << ", threshold: " << config.speaker_threshold << "\n";
        if (!controller_->initialize(config)) {
            std::cout << "[GUI ERROR] Controller initialization failed!\n";
            status_text_ = "Failed to initialize controller";
            return;
        }
        std::cout << "[GUI] Controller initialized successfully\n";
        
        // Create audio device
        std::cout << "[GUI] Creating audio device (synthetic=" << use_synthetic_audio_ << ")\n";
        if (use_synthetic_audio_) {
            audio_device_ = audio::AudioInputFactory::create_device("synthetic");
        } else {
            // TODO: Add real microphone support
            audio_device_ = audio::AudioInputFactory::create_device("platform");
        }
        
        if (!audio_device_) {
            std::cout << "[GUI ERROR] Failed to create audio device!\n";
            status_text_ = "Failed to create audio device";
            return;
        }
        std::cout << "[GUI] Audio device created\n";
        
        // Configure audio device
        audio::AudioInputConfig audio_config;
        if (use_synthetic_audio_) {
            audio_config.device_id = "synthetic";
            audio_config.synthetic_file_path = audio_file_path_;
            audio_config.synthetic_playback = true;
            audio_config.synthetic_loop = false;
            std::cout << "[GUI] Using synthetic audio file: " << audio_file_path_ << "\n";
        } else {
            audio_config.device_id = "";  // Default device
            std::cout << "[GUI] Using platform microphone\n";
        }
        audio_config.buffer_size_ms = 100;
        
        // Audio callback - feed to controller
        bool init_ok = audio_device_->initialize(
            audio_config,
            [this](const int16_t* samples, size_t sample_count, int sample_rate, int channels) {
                controller_->add_audio(samples, sample_count, sample_rate);
            },
            [this](const std::string& error, bool is_fatal) {
                std::cerr << "[AUDIO ERROR] " << error << "\n";
                if (is_fatal) {
                    status_text_ = "Audio error: " + error;
                }
            }
        );
        
        if (!init_ok) {
            status_text_ = "Failed to initialize audio";
            return;
        }
        
        // Start everything
        if (!controller_->start()) {
            status_text_ = "Failed to start controller";
            return;
        }
        
        if (!audio_device_->start()) {
            status_text_ = "Failed to start audio";
            controller_->stop();
            return;
        }
        
        is_recording_ = true;
        status_text_ = "Recording...";
    }
}

void AppWindow::OnClearClicked() {
    transcript_chunks_.clear();
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
