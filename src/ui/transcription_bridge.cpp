// Copyright (c) 2025 VAM Desktop Live Whisper
// TranscriptionBridge - Implementation

#include "ui/transcription_bridge.hpp"
#include "app/transcription_controller.hpp"

#include <QDebug>
#include <QMetaObject>

using namespace app;

TranscriptionBridge::TranscriptionBridge(QObject* parent)
    : QObject(parent)
    , controller_(std::make_unique<TranscriptionController>())
{
    // Subscribe to controller events
    controller_->subscribe_to_chunks([this](const TranscriptionChunk& chunk) {
        // Marshal to Qt main thread
        QMetaObject::invokeMethod(this, [this, chunk]() {
            onChunkReceived(chunk);
        }, Qt::QueuedConnection);
    });
    
    controller_->subscribe_to_reclassification([this](const SpeakerReclassification& recl) {
        QMetaObject::invokeMethod(this, [this, recl]() {
            onSpeakerReclassified(recl);
        }, Qt::QueuedConnection);
    });
    
    controller_->subscribe_to_status([this](const TranscriptionStatus& status) {
        QMetaObject::invokeMethod(this, [this, status]() {
            onStatusChanged(status);
        }, Qt::QueuedConnection);
    });
    
    controller_->subscribe_to_errors([this](const TranscriptionError& error) {
        QMetaObject::invokeMethod(this, [this, error]() {
            onErrorOccurred(error);
        }, Qt::QueuedConnection);
    });
}

TranscriptionBridge::~TranscriptionBridge() {
    if (is_recording_) {
        controller_->stop_transcription();
    }
}

//==============================================================================
// Property Setters
//==============================================================================

void TranscriptionBridge::setUseSyntheticAudio(bool value) {
    if (use_synthetic_audio_ != value) {
        use_synthetic_audio_ = value;
        emit useSyntheticAudioChanged();
    }
}

void TranscriptionBridge::setSyntheticAudioFile(const QString& path) {
    if (synthetic_audio_file_ != path) {
        synthetic_audio_file_ = path;
        emit syntheticAudioFileChanged();
    }
}

void TranscriptionBridge::setPlaybackSynthetic(bool value) {
    if (playback_synthetic_ != value) {
        playback_synthetic_ = value;
        emit playbackSyntheticChanged();
    }
}

void TranscriptionBridge::setWhisperModel(const QString& model) {
    if (whisper_model_ != model) {
        whisper_model_ = model;
        emit whisperModelChanged();
    }
}

void TranscriptionBridge::setMaxSpeakers(int value) {
    if (max_speakers_ != value) {
        max_speakers_ = value;
        controller_->set_max_speakers(value);
        emit maxSpeakersChanged();
    }
}

void TranscriptionBridge::setSpeakerThreshold(float value) {
    if (speaker_threshold_ != value) {
        speaker_threshold_ = value;
        emit speakerThresholdChanged();
    }
}

//==============================================================================
// Transcription Control
//==============================================================================

void TranscriptionBridge::startRecording() {
    if (is_recording_) {
        qWarning() << "Already recording!";
        return;
    }
    
    // Configure controller
    TranscriptionConfig config;
    config.whisper_model = whisper_model_.toStdString();
    config.max_speakers = max_speakers_;
    config.speaker_threshold = speaker_threshold_;
    config.enable_reclassification = true;
    
    // TODO: Add synthetic audio support to config
    // config.use_synthetic_audio = use_synthetic_audio_;
    // config.synthetic_audio_file = synthetic_audio_file_.toStdString();
    // config.playback_synthetic = playback_synthetic_;
    
    // Start transcription
    if (controller_->start_transcription(config)) {
        is_recording_ = true;
        emit isRecordingChanged();
        qDebug() << "Recording started";
    } else {
        qWarning() << "Failed to start recording!";
        emit errorOccurred(
            static_cast<int>(TranscriptionError::Severity::ERROR),
            "Failed to start recording",
            "Check models are loaded and audio device is available"
        );
    }
}

void TranscriptionBridge::stopRecording() {
    if (!is_recording_) {
        qWarning() << "Not recording!";
        return;
    }
    
    controller_->stop_transcription();
    is_recording_ = false;
    emit isRecordingChanged();
    qDebug() << "Recording stopped";
}

void TranscriptionBridge::pauseRecording() {
    if (!is_recording_) {
        qWarning() << "Not recording!";
        return;
    }
    
    if (controller_->pause_transcription()) {
        qDebug() << "Recording paused";
    }
}

void TranscriptionBridge::resumeRecording() {
    if (!is_recording_) {
        qWarning() << "Not recording!";
        return;
    }
    
    if (controller_->resume_transcription()) {
        qDebug() << "Recording resumed";
    }
}

void TranscriptionBridge::clearTranscript() {
    controller_->clear_history();
    qDebug() << "Transcript cleared";
}

//==============================================================================
// Device Management
//==============================================================================

QStringList TranscriptionBridge::listAudioDevices() {
    QStringList devices;
    
    auto device_list = controller_->list_audio_devices();
    for (const auto& dev : device_list) {
        QString device_str = QString::fromStdString(dev.name);
        if (dev.is_default) {
            device_str += " [DEFAULT]";
        }
        devices.append(device_str);
    }
    
    return devices;
}

void TranscriptionBridge::selectAudioDevice(const QString& deviceId) {
    if (controller_->select_audio_device(deviceId.toStdString())) {
        qDebug() << "Selected audio device:" << deviceId;
    } else {
        qWarning() << "Failed to select audio device:" << deviceId;
    }
}

QString TranscriptionBridge::getSelectedDevice() const {
    return QString::fromStdString(controller_->get_selected_device());
}

//==============================================================================
// Event Handlers (Convert C++ -> Qt Signals)
//==============================================================================

void TranscriptionBridge::onChunkReceived(const TranscriptionChunk& chunk) {
    emit chunkReceived(
        chunk.id,
        QString::fromStdString(chunk.text),
        chunk.speaker_id,
        chunk.timestamp_ms,
        chunk.duration_ms,
        chunk.speaker_confidence,
        chunk.is_finalized
    );
}

void TranscriptionBridge::onSpeakerReclassified(const SpeakerReclassification& recl) {
    QVector<quint64> chunk_ids;
    chunk_ids.reserve(recl.chunk_ids.size());
    for (uint64_t id : recl.chunk_ids) {
        chunk_ids.append(id);
    }
    
    emit speakerReclassified(
        chunk_ids,
        recl.old_speaker_id,
        recl.new_speaker_id,
        QString::fromStdString(recl.reason)
    );
}

void TranscriptionBridge::onStatusChanged(const TranscriptionStatus& status) {
    emit statusChanged(
        static_cast<int>(status.state),
        status.elapsed_ms,
        status.chunks_emitted,
        status.reclassifications_count,
        status.realtime_factor
    );
}

void TranscriptionBridge::onErrorOccurred(const TranscriptionError& error) {
    emit errorOccurred(
        static_cast<int>(error.severity),
        QString::fromStdString(error.message),
        QString::fromStdString(error.details)
    );
}
