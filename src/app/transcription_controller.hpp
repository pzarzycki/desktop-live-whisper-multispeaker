// Copyright (c) 2025 VAM Desktop Live Whisper
// Application API - Transcription Controller Interface
//
// Provides event-driven API for controlling real-time transcription
// with speaker diarization. Bridges low-level engine and high-level GUI.

#pragma once

#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace app {

// Forward declarations
class TranscriptionControllerImpl;

//==============================================================================
// Constants
//==============================================================================

constexpr int UNKNOWN_SPEAKER = -1;

//==============================================================================
// Configuration Structures
//==============================================================================

/// Configuration for transcription session
struct TranscriptionConfig {
    // Model Selection
    std::string whisper_model = "tiny.en";        ///< Whisper model: tiny, base, small, medium, large
    std::string speaker_model = "campplus_voxceleb.onnx";  ///< Speaker embedding model
    
    // Processing Parameters
    int max_speakers = 2;                         ///< Maximum number of speakers to detect
    float speaker_threshold = 0.35f;              ///< Similarity threshold for speaker assignment
    int vad_silence_duration_ms = 1000;          ///< Silence duration before finalizing segment
    
    // Real-time Behavior
    bool enable_partial_results = true;           ///< Send incomplete segments
    int chunk_duration_ms = 250;                  ///< How often to emit chunks (milliseconds)
    
    // Speaker Reclassification
    bool enable_reclassification = true;          ///< Enable retroactive speaker reassignment
    int reclassification_window_ms = 5000;       ///< How far back to reconsider (milliseconds)
};

/// Audio device information
struct AudioDevice {
    std::string id;                               ///< Platform-specific device ID
    std::string name;                             ///< Human-readable device name
    bool is_default;                              ///< Whether this is the system default device
};

//==============================================================================
// Event Structures
//==============================================================================

/// A piece of transcribed text with speaker identification
struct TranscriptionChunk {
    /// Word-level details (optional)
    struct Word {
        std::string text;                         ///< Word text
        int64_t t0_ms;                           ///< Word start time (ms from session start)
        int64_t t1_ms;                           ///< Word end time (ms from session start)
        float probability;                        ///< Whisper's confidence in this word
    };
    
    uint64_t id;                                  ///< Unique identifier for this chunk
    std::string text;                             ///< The transcribed text
    int speaker_id;                               ///< 0, 1, 2, ... or UNKNOWN_SPEAKER (-1)
    int64_t timestamp_ms;                        ///< When this was spoken (ms from session start)
    int64_t duration_ms;                         ///< How long this chunk spans (milliseconds)
    float speaker_confidence;                     ///< 0.0-1.0 confidence in speaker assignment
    bool is_finalized;                            ///< If true, won't be reclassified
    
    std::vector<Word> words;                      ///< Word-level breakdown (optional)
};

/// Speaker reclassification event for earlier chunks
struct SpeakerReclassification {
    std::vector<uint64_t> chunk_ids;              ///< Which chunks changed speaker
    int old_speaker_id;                           ///< What they were assigned to
    int new_speaker_id;                           ///< What they are now assigned to
    std::string reason;                           ///< Why: "better_context", "isolated_chunk", etc.
};

/// Transcription status information
struct TranscriptionStatus {
    /// Current state of transcription
    enum class State {
        IDLE,                                     ///< Not running
        STARTING,                                 ///< Initializing audio/models
        RUNNING,                                  ///< Actively transcribing
        PAUSED,                                   ///< Paused by user
        STOPPING,                                 ///< Shutting down
        ERROR                                     ///< Error occurred
    };
    
    State state;                                  ///< Current transcription state
    int64_t elapsed_ms;                          ///< Time since transcription started (milliseconds)
    int chunks_emitted;                           ///< Total chunks sent to callbacks
    int reclassifications_count;                  ///< Total reclassifications performed
    std::string current_device;                   ///< Name of current audio device
    
    // Performance metrics
    float realtime_factor;                        ///< <1.0 means faster than realtime
    int audio_buffer_ms;                          ///< How much audio buffered (milliseconds)
};

/// Error/warning event
struct TranscriptionError {
    /// Error severity level
    enum class Severity {
        WARNING,                                  ///< Non-fatal, can continue
        ERROR,                                    ///< Fatal, transcription stopped
        CRITICAL                                  ///< System-level issue
    };
    
    Severity severity;                            ///< Error severity
    std::string message;                          ///< Human-readable error message
    std::string details;                          ///< Technical details for debugging
    int64_t timestamp_ms;                        ///< When error occurred (ms from session start)
};

//==============================================================================
// Callback Types
//==============================================================================

using ChunkCallback = std::function<void(const TranscriptionChunk&)>;
using ReclassificationCallback = std::function<void(const SpeakerReclassification&)>;
using StatusCallback = std::function<void(const TranscriptionStatus&)>;
using ErrorCallback = std::function<void(const TranscriptionError&)>;

//==============================================================================
// Main Controller Class
//==============================================================================

/// Main controller for transcription with speaker diarization
///
/// This class provides a high-level, event-driven API for controlling
/// real-time transcription with speaker identification. It handles:
/// - Audio device management
/// - Transcription lifecycle (start/stop/pause)
/// - Event emission (chunks, reclassification, status, errors)
/// - Speaker management and reclassification
///
/// Thread Safety:
/// - All public methods are thread-safe
/// - Callbacks are invoked from internal processing thread
/// - GUI applications should marshal callbacks to UI thread
///
/// Example:
/// @code
/// TranscriptionController controller;
/// 
/// controller.subscribe_to_chunks([](const TranscriptionChunk& chunk) {
///     std::cout << "[S" << chunk.speaker_id << "] " << chunk.text << "\n";
/// });
/// 
/// TranscriptionConfig config;
/// controller.start_transcription(config);
/// // ... let it run ...
/// controller.stop_transcription();
/// @endcode
class TranscriptionController {
public:
    //==========================================================================
    // Lifecycle
    //==========================================================================
    
    /// Construct controller (loads models if specified in config)
    TranscriptionController();
    
    /// Destructor (stops transcription if running)
    ~TranscriptionController();
    
    // Non-copyable, non-movable
    TranscriptionController(const TranscriptionController&) = delete;
    TranscriptionController& operator=(const TranscriptionController&) = delete;
    TranscriptionController(TranscriptionController&&) = delete;
    TranscriptionController& operator=(TranscriptionController&&) = delete;
    
    //==========================================================================
    // Device Management
    //==========================================================================
    
    /// Get list of available audio input devices
    /// @return Vector of audio devices (always includes at least system default)
    std::vector<AudioDevice> list_audio_devices();
    
    /// Select audio input device
    /// @param device_id Device ID from list_audio_devices()
    /// @return true if device selected successfully, false on error
    /// @note Cannot change device while transcription is running
    bool select_audio_device(const std::string& device_id);
    
    /// Get currently selected device ID
    /// @return Device ID, or empty string if none selected
    std::string get_selected_device() const;
    
    //==========================================================================
    // Transcription Control
    //==========================================================================
    
    /// Start transcription session
    /// @param config Configuration for this session
    /// @return true if started successfully, false on error
    /// @note If already running, returns false
    bool start_transcription(const TranscriptionConfig& config);
    
    /// Stop transcription session
    /// @note Safe to call even if not running
    void stop_transcription();
    
    /// Pause transcription (stops audio capture but keeps models loaded)
    /// @return true if paused, false if not running
    bool pause_transcription();
    
    /// Resume paused transcription
    /// @return true if resumed, false if not paused
    bool resume_transcription();
    
    /// Check if transcription is currently running
    /// @return true if actively transcribing
    bool is_running() const;
    
    /// Get current transcription status
    /// @return Status structure with state and metrics
    TranscriptionStatus get_status() const;
    
    //==========================================================================
    // Event Subscription
    //==========================================================================
    
    /// Subscribe to transcription chunk events
    /// @param callback Function called for each chunk
    /// @note Callbacks are invoked from processing thread, not UI thread
    /// @note Multiple callbacks can be registered
    void subscribe_to_chunks(ChunkCallback callback);
    
    /// Subscribe to speaker reclassification events
    /// @param callback Function called when chunks are reclassified
    void subscribe_to_reclassification(ReclassificationCallback callback);
    
    /// Subscribe to status update events
    /// @param callback Function called on status changes
    void subscribe_to_status(StatusCallback callback);
    
    /// Subscribe to error/warning events
    /// @param callback Function called on errors
    void subscribe_to_errors(ErrorCallback callback);
    
    /// Clear all event subscriptions
    void clear_subscriptions();
    
    //==========================================================================
    // Speaker Management
    //==========================================================================
    
    /// Get number of speakers detected so far
    /// @return Number of distinct speakers (0 if not running)
    int get_speaker_count() const;
    
    /// Set maximum number of speakers to detect
    /// @param max_speakers Maximum speakers (1-10)
    /// @note Can be called while running, takes effect immediately
    void set_max_speakers(int max_speakers);
    
    /// Get maximum number of speakers
    /// @return Maximum speakers configured
    int get_max_speakers() const;
    
    //==========================================================================
    // Chunk History
    //==========================================================================
    
    /// Get all chunks emitted so far (for rebuilding display)
    /// @return Vector of all chunks in chronological order
    /// @note Returns copy, safe to use from any thread
    std::vector<TranscriptionChunk> get_all_chunks() const;
    
    /// Get chunk by ID
    /// @param id Chunk ID to retrieve
    /// @param[out] chunk Chunk data if found
    /// @return true if found, false if not
    bool get_chunk_by_id(uint64_t id, TranscriptionChunk& chunk) const;
    
    /// Clear chunk history (keeps session running)
    void clear_history();
    
    //==========================================================================
    // Configuration Access
    //==========================================================================
    
    /// Get current configuration
    /// @return Current transcription config (default if not running)
    TranscriptionConfig get_config() const;
    
    /// Update configuration (some settings require restart)
    /// @param config New configuration
    /// @return true if applied, false if requires restart
    bool update_config(const TranscriptionConfig& config);
    
private:
    std::unique_ptr<TranscriptionControllerImpl> impl_;  ///< PIMPL implementation
};

} // namespace app
