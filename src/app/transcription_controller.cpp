// Copyright (c) 2025 VAM Desktop Live Whisper
// Application API - Transcription Controller Implementation

#include "app/transcription_controller.hpp"

#include <iostream>
#include <mutex>
#include <atomic>
#include <thread>
#include <deque>
#include <algorithm>

namespace app {

//==============================================================================
// Implementation Class (PIMPL Pattern)
//==============================================================================

class TranscriptionControllerImpl {
public:
    TranscriptionControllerImpl() = default;
    ~TranscriptionControllerImpl() {
        stop();
    }
    
    // Device Management
    std::vector<AudioDevice> list_audio_devices();
    bool select_audio_device(const std::string& device_id);
    std::string get_selected_device() const;
    
    // Transcription Control
    bool start(const TranscriptionConfig& config);
    void stop();
    bool pause();
    bool resume();
    bool is_running() const;
    TranscriptionStatus get_status() const;
    
    // Event Subscription
    void subscribe_to_chunks(ChunkCallback callback);
    void subscribe_to_reclassification(ReclassificationCallback callback);
    void subscribe_to_status(StatusCallback callback);
    void subscribe_to_errors(ErrorCallback callback);
    void clear_subscriptions();
    
    // Speaker Management
    int get_speaker_count() const;
    void set_max_speakers(int max_speakers);
    int get_max_speakers() const;
    
    // Chunk History
    std::vector<TranscriptionChunk> get_all_chunks() const;
    bool get_chunk_by_id(uint64_t id, TranscriptionChunk& chunk) const;
    void clear_history();
    
    // Configuration
    TranscriptionConfig get_config() const;
    bool update_config(const TranscriptionConfig& config);
    
private:
    // Internal state
    mutable std::mutex state_mutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    
    TranscriptionConfig config_;
    std::string selected_device_id_;
    
    // Chunk storage
    mutable std::mutex chunks_mutex_;
    std::deque<TranscriptionChunk> chunks_;
    uint64_t next_chunk_id_ = 1;
    int64_t session_start_time_ms_ = 0;
    
    // Callbacks
    mutable std::mutex callbacks_mutex_;
    std::vector<ChunkCallback> chunk_callbacks_;
    std::vector<ReclassificationCallback> reclassification_callbacks_;
    std::vector<StatusCallback> status_callbacks_;
    std::vector<ErrorCallback> error_callbacks_;
    
    // Speaker tracking
    std::atomic<int> speaker_count_{0};
    std::atomic<int> max_speakers_{2};
    
    // Statistics
    std::atomic<int> chunks_emitted_{0};
    std::atomic<int> reclassifications_count_{0};
    
    // Threading
    std::unique_ptr<std::thread> processing_thread_;
    
    // Internal methods
    void processing_loop();
    void emit_chunk(const TranscriptionChunk& chunk);
    void emit_reclassification(const SpeakerReclassification& recl);
    void emit_status(const TranscriptionStatus& status);
    void emit_error(const TranscriptionError& error);
    
    int64_t get_elapsed_ms() const;
};

//==============================================================================
// Device Management Implementation
//==============================================================================

std::vector<AudioDevice> TranscriptionControllerImpl::list_audio_devices() {
    std::vector<AudioDevice> devices;
    
    // TODO: Integrate with WASAPI device enumeration
    // For now, return dummy data
    AudioDevice default_device;
    default_device.id = "default";
    default_device.name = "System Default";
    default_device.is_default = true;
    devices.push_back(default_device);
    
    return devices;
}

bool TranscriptionControllerImpl::select_audio_device(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (running_) {
        std::cerr << "Cannot change device while transcription is running\n";
        return false;
    }
    
    // TODO: Validate device exists
    selected_device_id_ = device_id;
    return true;
}

std::string TranscriptionControllerImpl::get_selected_device() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return selected_device_id_;
}

//==============================================================================
// Transcription Control Implementation
//==============================================================================

bool TranscriptionControllerImpl::start(const TranscriptionConfig& config) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (running_) {
        std::cerr << "Transcription already running\n";
        return false;
    }
    
    config_ = config;
    max_speakers_ = config.max_speakers;
    
    // Reset state
    chunks_emitted_ = 0;
    reclassifications_count_ = 0;
    speaker_count_ = 0;
    next_chunk_id_ = 1;
    session_start_time_ms_ = 0;  // TODO: Set to actual time
    
    // Clear old chunks
    {
        std::lock_guard<std::mutex> chunks_lock(chunks_mutex_);
        chunks_.clear();
    }
    
    // Start processing thread
    running_ = true;
    paused_ = false;
    processing_thread_ = std::make_unique<std::thread>(&TranscriptionControllerImpl::processing_loop, this);
    
    // Emit starting status
    TranscriptionStatus status;
    status.state = TranscriptionStatus::State::STARTING;
    status.elapsed_ms = 0;
    status.chunks_emitted = 0;
    status.reclassifications_count = 0;
    status.current_device = selected_device_id_;
    status.realtime_factor = 0.0f;
    status.audio_buffer_ms = 0;
    emit_status(status);
    
    return true;
}

void TranscriptionControllerImpl::stop() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!running_) {
            return;
        }
        running_ = false;
    }
    
    // Wait for processing thread
    if (processing_thread_ && processing_thread_->joinable()) {
        processing_thread_->join();
    }
    processing_thread_.reset();
    
    // Emit stopped status
    TranscriptionStatus status;
    status.state = TranscriptionStatus::State::IDLE;
    status.elapsed_ms = get_elapsed_ms();
    status.chunks_emitted = chunks_emitted_;
    status.reclassifications_count = reclassifications_count_;
    status.current_device = selected_device_id_;
    status.realtime_factor = 0.0f;
    status.audio_buffer_ms = 0;
    emit_status(status);
}

bool TranscriptionControllerImpl::pause() {
    if (!running_ || paused_) {
        return false;
    }
    
    paused_ = true;
    
    TranscriptionStatus status;
    status.state = TranscriptionStatus::State::PAUSED;
    status.elapsed_ms = get_elapsed_ms();
    status.chunks_emitted = chunks_emitted_;
    status.reclassifications_count = reclassifications_count_;
    status.current_device = selected_device_id_;
    status.realtime_factor = 0.0f;
    status.audio_buffer_ms = 0;
    emit_status(status);
    
    return true;
}

bool TranscriptionControllerImpl::resume() {
    if (!running_ || !paused_) {
        return false;
    }
    
    paused_ = false;
    
    TranscriptionStatus status;
    status.state = TranscriptionStatus::State::RUNNING;
    status.elapsed_ms = get_elapsed_ms();
    status.chunks_emitted = chunks_emitted_;
    status.reclassifications_count = reclassifications_count_;
    status.current_device = selected_device_id_;
    status.realtime_factor = 0.0f;
    status.audio_buffer_ms = 0;
    emit_status(status);
    
    return true;
}

bool TranscriptionControllerImpl::is_running() const {
    return running_;
}

TranscriptionStatus TranscriptionControllerImpl::get_status() const {
    TranscriptionStatus status;
    
    if (running_) {
        status.state = paused_ ? TranscriptionStatus::State::PAUSED 
                                : TranscriptionStatus::State::RUNNING;
    } else {
        status.state = TranscriptionStatus::State::IDLE;
    }
    
    status.elapsed_ms = get_elapsed_ms();
    status.chunks_emitted = chunks_emitted_;
    status.reclassifications_count = reclassifications_count_;
    
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status.current_device = selected_device_id_;
    }
    
    status.realtime_factor = 0.0f;  // TODO: Calculate actual
    status.audio_buffer_ms = 0;     // TODO: Get from audio capture
    
    return status;
}

//==============================================================================
// Event Subscription Implementation
//==============================================================================

void TranscriptionControllerImpl::subscribe_to_chunks(ChunkCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    chunk_callbacks_.push_back(callback);
}

void TranscriptionControllerImpl::subscribe_to_reclassification(ReclassificationCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    reclassification_callbacks_.push_back(callback);
}

void TranscriptionControllerImpl::subscribe_to_status(StatusCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    status_callbacks_.push_back(callback);
}

void TranscriptionControllerImpl::subscribe_to_errors(ErrorCallback callback) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    error_callbacks_.push_back(callback);
}

void TranscriptionControllerImpl::clear_subscriptions() {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    chunk_callbacks_.clear();
    reclassification_callbacks_.clear();
    status_callbacks_.clear();
    error_callbacks_.clear();
}

//==============================================================================
// Speaker Management Implementation
//==============================================================================

int TranscriptionControllerImpl::get_speaker_count() const {
    return speaker_count_;
}

void TranscriptionControllerImpl::set_max_speakers(int max_speakers) {
    if (max_speakers < 1 || max_speakers > 10) {
        std::cerr << "Invalid max_speakers: " << max_speakers << " (must be 1-10)\n";
        return;
    }
    max_speakers_ = max_speakers;
    
    // Update config if already set
    std::lock_guard<std::mutex> lock(state_mutex_);
    config_.max_speakers = max_speakers;
}

int TranscriptionControllerImpl::get_max_speakers() const {
    return max_speakers_;
}

//==============================================================================
// Chunk History Implementation
//==============================================================================

std::vector<TranscriptionChunk> TranscriptionControllerImpl::get_all_chunks() const {
    std::lock_guard<std::mutex> lock(chunks_mutex_);
    return std::vector<TranscriptionChunk>(chunks_.begin(), chunks_.end());
}

bool TranscriptionControllerImpl::get_chunk_by_id(uint64_t id, TranscriptionChunk& chunk) const {
    std::lock_guard<std::mutex> lock(chunks_mutex_);
    
    auto it = std::find_if(chunks_.begin(), chunks_.end(),
                          [id](const TranscriptionChunk& c) { return c.id == id; });
    
    if (it != chunks_.end()) {
        chunk = *it;
        return true;
    }
    
    return false;
}

void TranscriptionControllerImpl::clear_history() {
    std::lock_guard<std::mutex> lock(chunks_mutex_);
    chunks_.clear();
}

//==============================================================================
// Configuration Implementation
//==============================================================================

TranscriptionConfig TranscriptionControllerImpl::get_config() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return config_;
}

bool TranscriptionControllerImpl::update_config(const TranscriptionConfig& config) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    if (running_) {
        // Some settings can be updated live
        config_.max_speakers = config.max_speakers;
        config_.speaker_threshold = config.speaker_threshold;
        config_.enable_reclassification = config.enable_reclassification;
        config_.reclassification_window_ms = config.reclassification_window_ms;
        
        max_speakers_ = config.max_speakers;
        
        // Model changes require restart
        if (config_.whisper_model != config.whisper_model ||
            config_.speaker_model != config.speaker_model) {
            return false;  // Requires restart
        }
        
        return true;  // Applied
    } else {
        // Not running, apply all changes
        config_ = config;
        max_speakers_ = config.max_speakers;
        return true;
    }
}

//==============================================================================
// Internal Methods Implementation
//==============================================================================

void TranscriptionControllerImpl::processing_loop() {
    // TODO: Implement actual processing
    // For now, just emit status periodically
    
    std::cout << "Processing thread started\n";
    
    // Emit running status
    TranscriptionStatus status;
    status.state = TranscriptionStatus::State::RUNNING;
    status.elapsed_ms = 0;
    status.chunks_emitted = 0;
    status.reclassifications_count = 0;
    status.current_device = selected_device_id_;
    status.realtime_factor = 0.0f;
    status.audio_buffer_ms = 0;
    emit_status(status);
    
    while (running_) {
        if (paused_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // TODO: 
        // 1. Capture audio from selected device
        // 2. Feed to Whisper for transcription
        // 3. Extract frames for speaker embedding
        // 4. Do frame voting within segments
        // 5. Emit chunks
        // 6. Check for reclassification opportunities
        // 7. Update status periodically
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "Processing thread stopped\n";
}

void TranscriptionControllerImpl::emit_chunk(const TranscriptionChunk& chunk) {
    // Store chunk
    {
        std::lock_guard<std::mutex> lock(chunks_mutex_);
        chunks_.push_back(chunk);
        
        // Keep last N chunks (prevent unbounded growth)
        const size_t MAX_CHUNKS = 10000;
        if (chunks_.size() > MAX_CHUNKS) {
            chunks_.pop_front();
        }
    }
    
    chunks_emitted_++;
    
    // Invoke callbacks
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    for (const auto& callback : chunk_callbacks_) {
        try {
            callback(chunk);
        } catch (const std::exception& e) {
            std::cerr << "Chunk callback exception: " << e.what() << "\n";
        }
    }
}

void TranscriptionControllerImpl::emit_reclassification(const SpeakerReclassification& recl) {
    // Update stored chunks
    {
        std::lock_guard<std::mutex> lock(chunks_mutex_);
        for (auto& chunk : chunks_) {
            if (std::find(recl.chunk_ids.begin(), recl.chunk_ids.end(), chunk.id) 
                != recl.chunk_ids.end()) {
                chunk.speaker_id = recl.new_speaker_id;
            }
        }
    }
    
    reclassifications_count_++;
    
    // Invoke callbacks
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    for (const auto& callback : reclassification_callbacks_) {
        try {
            callback(recl);
        } catch (const std::exception& e) {
            std::cerr << "Reclassification callback exception: " << e.what() << "\n";
        }
    }
}

void TranscriptionControllerImpl::emit_status(const TranscriptionStatus& status) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    for (const auto& callback : status_callbacks_) {
        try {
            callback(status);
        } catch (const std::exception& e) {
            std::cerr << "Status callback exception: " << e.what() << "\n";
        }
    }
}

void TranscriptionControllerImpl::emit_error(const TranscriptionError& error) {
    std::lock_guard<std::mutex> lock(callbacks_mutex_);
    for (const auto& callback : error_callbacks_) {
        try {
            callback(error);
        } catch (const std::exception& e) {
            std::cerr << "Error callback exception: " << e.what() << "\n";
        }
    }
}

int64_t TranscriptionControllerImpl::get_elapsed_ms() const {
    if (session_start_time_ms_ == 0) {
        return 0;
    }
    // TODO: Calculate actual elapsed time
    return 0;
}

//==============================================================================
// TranscriptionController Public Interface (Forwarding to PIMPL)
//==============================================================================

TranscriptionController::TranscriptionController()
    : impl_(std::make_unique<TranscriptionControllerImpl>()) {
}

TranscriptionController::~TranscriptionController() = default;

std::vector<AudioDevice> TranscriptionController::list_audio_devices() {
    return impl_->list_audio_devices();
}

bool TranscriptionController::select_audio_device(const std::string& device_id) {
    return impl_->select_audio_device(device_id);
}

std::string TranscriptionController::get_selected_device() const {
    return impl_->get_selected_device();
}

bool TranscriptionController::start_transcription(const TranscriptionConfig& config) {
    return impl_->start(config);
}

void TranscriptionController::stop_transcription() {
    impl_->stop();
}

bool TranscriptionController::pause_transcription() {
    return impl_->pause();
}

bool TranscriptionController::resume_transcription() {
    return impl_->resume();
}

bool TranscriptionController::is_running() const {
    return impl_->is_running();
}

TranscriptionStatus TranscriptionController::get_status() const {
    return impl_->get_status();
}

void TranscriptionController::subscribe_to_chunks(ChunkCallback callback) {
    impl_->subscribe_to_chunks(callback);
}

void TranscriptionController::subscribe_to_reclassification(ReclassificationCallback callback) {
    impl_->subscribe_to_reclassification(callback);
}

void TranscriptionController::subscribe_to_status(StatusCallback callback) {
    impl_->subscribe_to_status(callback);
}

void TranscriptionController::subscribe_to_errors(ErrorCallback callback) {
    impl_->subscribe_to_errors(callback);
}

void TranscriptionController::clear_subscriptions() {
    impl_->clear_subscriptions();
}

int TranscriptionController::get_speaker_count() const {
    return impl_->get_speaker_count();
}

void TranscriptionController::set_max_speakers(int max_speakers) {
    impl_->set_max_speakers(max_speakers);
}

int TranscriptionController::get_max_speakers() const {
    return impl_->get_max_speakers();
}

std::vector<TranscriptionChunk> TranscriptionController::get_all_chunks() const {
    return impl_->get_all_chunks();
}

bool TranscriptionController::get_chunk_by_id(uint64_t id, TranscriptionChunk& chunk) const {
    return impl_->get_chunk_by_id(id, chunk);
}

void TranscriptionController::clear_history() {
    impl_->clear_history();
}

TranscriptionConfig TranscriptionController::get_config() const {
    return impl_->get_config();
}

bool TranscriptionController::update_config(const TranscriptionConfig& config) {
    return impl_->update_config(config);
}

} // namespace app
