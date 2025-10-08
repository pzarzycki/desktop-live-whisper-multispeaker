// Copyright (c) 2025 VAM Desktop Live Whisper
// TranscriptionBridge - Qt/QML bridge to TranscriptionController

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

// Forward declarations
namespace app {
    class TranscriptionController;
    struct TranscriptionChunk;
    struct SpeakerReclassification;
    struct TranscriptionStatus;
    struct TranscriptionError;
}

class TranscriptionBridge : public QObject {
    Q_OBJECT
    
    // Properties exposed to QML
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY isRecordingChanged)
    Q_PROPERTY(bool useSyntheticAudio READ useSyntheticAudio WRITE setUseSyntheticAudio NOTIFY useSyntheticAudioChanged)
    Q_PROPERTY(QString syntheticAudioFile READ syntheticAudioFile WRITE setSyntheticAudioFile NOTIFY syntheticAudioFileChanged)
    Q_PROPERTY(bool playbackSynthetic READ playbackSynthetic WRITE setPlaybackSynthetic NOTIFY playbackSyntheticChanged)
    Q_PROPERTY(QString whisperModel READ whisperModel WRITE setWhisperModel NOTIFY whisperModelChanged)
    Q_PROPERTY(int maxSpeakers READ maxSpeakers WRITE setMaxSpeakers NOTIFY maxSpeakersChanged)
    Q_PROPERTY(float speakerThreshold READ speakerThreshold WRITE setSpeakerThreshold NOTIFY speakerThresholdChanged)
    
public:
    explicit TranscriptionBridge(QObject* parent = nullptr);
    ~TranscriptionBridge();
    
    // Property getters
    bool isRecording() const { return is_recording_; }
    bool useSyntheticAudio() const { return use_synthetic_audio_; }
    QString syntheticAudioFile() const { return synthetic_audio_file_; }
    bool playbackSynthetic() const { return playback_synthetic_; }
    QString whisperModel() const { return whisper_model_; }
    int maxSpeakers() const { return max_speakers_; }
    float speakerThreshold() const { return speaker_threshold_; }
    
    // Property setters
    void setUseSyntheticAudio(bool value);
    void setSyntheticAudioFile(const QString& path);
    void setPlaybackSynthetic(bool value);
    void setWhisperModel(const QString& model);
    void setMaxSpeakers(int value);
    void setSpeakerThreshold(float value);
    
public slots:
    // Transcription control
    void startRecording();
    void stopRecording();
    void pauseRecording();
    void resumeRecording();
    void clearTranscript();
    
    // Device management
    QStringList listAudioDevices();
    void selectAudioDevice(const QString& deviceId);
    QString getSelectedDevice() const;
    
signals:
    // Property change notifications
    void isRecordingChanged();
    void useSyntheticAudioChanged();
    void syntheticAudioFileChanged();
    void playbackSyntheticChanged();
    void whisperModelChanged();
    void maxSpeakersChanged();
    void speakerThresholdChanged();
    
    // Transcription events (forwarded from controller)
    void chunkReceived(
        quint64 id,
        QString text,
        int speakerId,
        qint64 timestampMs,
        qint64 durationMs,
        float confidence,
        bool isFinalized
    );
    
    void speakerReclassified(
        QVector<quint64> chunkIds,
        int oldSpeakerId,
        int newSpeakerId,
        QString reason
    );
    
    void statusChanged(
        int state,
        qint64 elapsedMs,
        int chunksEmitted,
        int reclassificationsCount,
        float realtimeFactor
    );
    
    void errorOccurred(
        int severity,
        QString message,
        QString details
    );
    
private:
    // Callback handlers (convert C++ events to Qt signals)
    void onChunkReceived(const app::TranscriptionChunk& chunk);
    void onSpeakerReclassified(const app::SpeakerReclassification& recl);
    void onStatusChanged(const app::TranscriptionStatus& status);
    void onErrorOccurred(const app::TranscriptionError& error);
    
    // State
    std::unique_ptr<app::TranscriptionController> controller_;
    bool is_recording_ = false;
    
    // Configuration
    bool use_synthetic_audio_ = true;  // Default to synthetic for testing
    QString synthetic_audio_file_ = "output/whisper_input_16k.wav";
    bool playback_synthetic_ = true;
    QString whisper_model_ = "tiny.en";
    int max_speakers_ = 2;
    float speaker_threshold_ = 0.35f;
};
