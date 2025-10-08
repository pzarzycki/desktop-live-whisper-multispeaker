// Copyright (c) 2025 VAM Desktop Live Whisper
// Test application for TranscriptionController API

#include "app/transcription_controller.hpp"

#include <iostream>
#include <iomanip>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <map>

using namespace app;

// Global flag for Ctrl+C handling
std::atomic<bool> g_should_stop{false};

void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\n\nReceived Ctrl+C, stopping...\n";
        g_should_stop = true;
    }
}

// Color codes for terminal output
namespace color {
    const char* RESET = "\033[0m";
    const char* BLUE = "\033[34m";
    const char* RED = "\033[31m";
    const char* GREEN = "\033[32m";
    const char* YELLOW = "\033[33m";
    const char* CYAN = "\033[36m";
    const char* MAGENTA = "\033[35m";
}

int main(int argc, char** argv) {
    std::cout << "==========================================================\n";
    std::cout << "  TranscriptionController API Test\n";
    std::cout << "==========================================================\n\n";
    
    // Setup signal handler
    signal(SIGINT, signal_handler);
    
    // Create controller
    TranscriptionController controller;
    
    //==========================================================================
    // Test 1: Device Enumeration
    //==========================================================================
    
    std::cout << color::CYAN << "TEST 1: Audio Device Enumeration" << color::RESET << "\n";
    std::cout << "-----------------------------------------------------------\n";
    
    auto devices = controller.list_audio_devices();
    std::cout << "Found " << devices.size() << " audio device(s):\n\n";
    
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "  " << i << ". " << devices[i].name;
        if (devices[i].is_default) {
            std::cout << color::GREEN << " [DEFAULT]" << color::RESET;
        }
        std::cout << "\n      ID: " << devices[i].id << "\n";
    }
    
    std::cout << "\n";
    
    // Select device (use default for now)
    if (!devices.empty()) {
        std::cout << "Selecting device: " << devices[0].name << "\n";
        if (controller.select_audio_device(devices[0].id)) {
            std::cout << color::GREEN << "✓ Device selected" << color::RESET << "\n";
        } else {
            std::cerr << color::RED << "✗ Failed to select device" << color::RESET << "\n";
        }
    }
    
    std::cout << "\n";
    
    //==========================================================================
    // Test 2: Event Subscription
    //==========================================================================
    
    std::cout << color::CYAN << "TEST 2: Event Subscription" << color::RESET << "\n";
    std::cout << "-----------------------------------------------------------\n";
    
    // Subscribe to chunks
    controller.subscribe_to_chunks([](const TranscriptionChunk& chunk) {
        const char* speaker_color = (chunk.speaker_id == 0) ? color::BLUE : color::RED;
        
        std::cout << "\n" << speaker_color << "[S" << chunk.speaker_id << "] " 
                  << color::RESET << chunk.text;
        
        if (chunk.speaker_confidence < 0.7f) {
            std::cout << color::YELLOW << " (low conf: " 
                      << std::fixed << std::setprecision(2) 
                      << chunk.speaker_confidence << ")" << color::RESET;
        }
        
        if (chunk.is_finalized) {
            std::cout << " " << color::GREEN << "[FINAL]" << color::RESET;
        }
        
        std::cout.flush();
    });
    
    // Subscribe to reclassification
    controller.subscribe_to_reclassification([](const SpeakerReclassification& recl) {
        std::cout << "\n" << color::MAGENTA << ">>> RECLASSIFIED " 
                  << recl.chunk_ids.size() << " chunk(s): "
                  << "S" << recl.old_speaker_id << " → S" << recl.new_speaker_id
                  << " (" << recl.reason << ")" << color::RESET << "\n";
    });
    
    // Subscribe to status
    int last_chunks = 0;
    controller.subscribe_to_status([&last_chunks](const TranscriptionStatus& status) {
        std::string state_str;
        const char* state_color = color::RESET;
        
        switch (status.state) {
            case TranscriptionStatus::State::IDLE:
                state_str = "IDLE";
                state_color = color::RESET;
                break;
            case TranscriptionStatus::State::STARTING:
                state_str = "STARTING";
                state_color = color::YELLOW;
                break;
            case TranscriptionStatus::State::RUNNING:
                state_str = "RUNNING";
                state_color = color::GREEN;
                break;
            case TranscriptionStatus::State::PAUSED:
                state_str = "PAUSED";
                state_color = color::YELLOW;
                break;
            case TranscriptionStatus::State::STOPPING:
                state_str = "STOPPING";
                state_color = color::YELLOW;
                break;
            case TranscriptionStatus::State::ERROR:
                state_str = "ERROR";
                state_color = color::RED;
                break;
        }
        
        // Only print status changes, not every update
        if (status.chunks_emitted != last_chunks || 
            status.state == TranscriptionStatus::State::STARTING ||
            status.state == TranscriptionStatus::State::IDLE) {
            
            std::cout << "\n" << state_color << "[STATUS: " << state_str << "]" 
                      << color::RESET
                      << " Elapsed: " << (status.elapsed_ms / 1000) << "s"
                      << " | Chunks: " << status.chunks_emitted
                      << " | Recls: " << status.reclassifications_count
                      << " | RT: " << std::fixed << std::setprecision(2) 
                      << status.realtime_factor << "x";
            
            if (!status.current_device.empty()) {
                std::cout << " | Device: " << status.current_device;
            }
            
            std::cout << "\n";
            
            last_chunks = status.chunks_emitted;
        }
    });
    
    // Subscribe to errors
    controller.subscribe_to_errors([](const TranscriptionError& error) {
        const char* severity_color = color::RESET;
        std::string severity_str;
        
        switch (error.severity) {
            case TranscriptionError::Severity::WARNING:
                severity_color = color::YELLOW;
                severity_str = "WARNING";
                break;
            case TranscriptionError::Severity::ERROR:
                severity_color = color::RED;
                severity_str = "ERROR";
                break;
            case TranscriptionError::Severity::CRITICAL:
                severity_color = color::RED;
                severity_str = "CRITICAL";
                break;
        }
        
        std::cerr << "\n" << severity_color << "[" << severity_str << "] " 
                  << color::RESET << error.message << "\n";
        
        if (!error.details.empty()) {
            std::cerr << "  Details: " << error.details << "\n";
        }
    });
    
    std::cout << color::GREEN << "✓ Subscribed to all event types" << color::RESET << "\n\n";
    
    //==========================================================================
    // Test 3: Configuration and Start
    //==========================================================================
    
    std::cout << color::CYAN << "TEST 3: Start Transcription" << color::RESET << "\n";
    std::cout << "-----------------------------------------------------------\n";
    
    TranscriptionConfig config;
    config.whisper_model = "tiny.en";
    config.speaker_model = "campplus_voxceleb.onnx";
    config.max_speakers = 2;
    config.speaker_threshold = 0.35f;
    config.enable_reclassification = true;
    config.reclassification_window_ms = 5000;
    
    std::cout << "Configuration:\n";
    std::cout << "  Whisper model: " << config.whisper_model << "\n";
    std::cout << "  Speaker model: " << config.speaker_model << "\n";
    std::cout << "  Max speakers: " << config.max_speakers << "\n";
    std::cout << "  Speaker threshold: " << config.speaker_threshold << "\n";
    std::cout << "  Reclassification: " 
              << (config.enable_reclassification ? "enabled" : "disabled") << "\n";
    std::cout << "\n";
    
    std::cout << "Starting transcription...\n";
    
    if (controller.start_transcription(config)) {
        std::cout << color::GREEN << "✓ Transcription started!" << color::RESET << "\n";
        std::cout << "\nPress Ctrl+C to stop.\n";
        std::cout << "-----------------------------------------------------------\n\n";
    } else {
        std::cerr << color::RED << "✗ Failed to start transcription" << color::RESET << "\n";
        return 1;
    }
    
    //==========================================================================
    // Test 4: Monitor While Running
    //==========================================================================
    
    // Wait for user to stop (Ctrl+C) or run for max time
    const int MAX_RUN_SECONDS = 60;
    int elapsed = 0;
    
    while (!g_should_stop && elapsed < MAX_RUN_SECONDS) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        elapsed++;
        
        // Test pause/resume every 10 seconds
        if (elapsed % 20 == 10) {
            std::cout << "\n" << color::YELLOW << "[Testing pause...]" << color::RESET << "\n";
            controller.pause_transcription();
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout << color::YELLOW << "[Testing resume...]" << color::RESET << "\n";
            controller.resume_transcription();
        }
    }
    
    //==========================================================================
    // Test 5: Stop and Summary
    //==========================================================================
    
    std::cout << "\n\n" << color::CYAN << "TEST 5: Stop and Summary" << color::RESET << "\n";
    std::cout << "-----------------------------------------------------------\n";
    
    std::cout << "Stopping transcription...\n";
    controller.stop_transcription();
    
    // Get final status
    auto final_status = controller.get_status();
    std::cout << "\nFinal Status:\n";
    std::cout << "  Total chunks emitted: " << final_status.chunks_emitted << "\n";
    std::cout << "  Total reclassifications: " << final_status.reclassifications_count << "\n";
    std::cout << "  Elapsed time: " << (final_status.elapsed_ms / 1000) << " seconds\n";
    
    // Get all chunks
    auto all_chunks = controller.get_all_chunks();
    std::cout << "  Chunks in history: " << all_chunks.size() << "\n";
    
    if (!all_chunks.empty()) {
        std::cout << "\nSpeaker distribution:\n";
        
        std::map<int, int> speaker_counts;
        for (const auto& chunk : all_chunks) {
            speaker_counts[chunk.speaker_id]++;
        }
        
        for (const auto& [speaker_id, count] : speaker_counts) {
            const char* speaker_color = (speaker_id == 0) ? color::BLUE : color::RED;
            std::cout << "  " << speaker_color << "S" << speaker_id << color::RESET 
                      << ": " << count << " chunks ("
                      << std::fixed << std::setprecision(1)
                      << (100.0f * count / all_chunks.size()) << "%)\n";
        }
        
        // Show full transcript
        std::cout << "\n" << color::CYAN << "Full Transcript:" << color::RESET << "\n";
        std::cout << "-----------------------------------------------------------\n";
        
        int current_speaker = -999;
        for (const auto& chunk : all_chunks) {
            if (chunk.speaker_id != current_speaker) {
                const char* speaker_color = (chunk.speaker_id == 0) ? color::BLUE : color::RED;
                std::cout << "\n" << speaker_color << "[S" << chunk.speaker_id << "]" 
                          << color::RESET << " ";
                current_speaker = chunk.speaker_id;
            }
            std::cout << chunk.text << " ";
        }
        std::cout << "\n";
    }
    
    std::cout << "\n" << color::GREEN << "✓ All tests completed" << color::RESET << "\n";
    std::cout << "==========================================================\n";
    
    return 0;
}
