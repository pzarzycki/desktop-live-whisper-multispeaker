// Copyright (c) 2025 VAM Desktop Live Whisper
// Main QML Window

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import App 1.0

ApplicationWindow {
    id: root
    visible: true
    width: 1200
    height: 800
    title: "Desktop Live Whisper"
    
    // Theme colors
    readonly property color backgroundColor: "#1E1E1E"
    readonly property color surfaceColor: "#2D2D2D"
    readonly property color textColor: "#E0E0E0"
    readonly property color accentColor: "#007ACC"
    readonly property color speaker0Color: "#4A9EFF"
    readonly property color speaker1Color: "#FF6B6B"
    
    color: backgroundColor
    
    // C++ backend bridge
    TranscriptionBridge {
        id: bridge
        
        onChunkReceived: (id, text, speakerId, timestampMs, durationMs, confidence, isFinalized) => {
            transcriptModel.append({
                "id": id,
                "text": text,
                "speakerId": speakerId,
                "timestampMs": timestampMs,
                "durationMs": durationMs,
                "confidence": confidence,
                "isFinalized": isFinalized
            });
            
            // Auto-scroll to bottom
            transcriptView.positionViewAtEnd();
        }
        
        onSpeakerReclassified: (chunkIds, oldSpeaker, newSpeaker, reason) => {
            console.log("Reclassified:", chunkIds.length, "chunks:", oldSpeaker, "->", newSpeaker, "(", reason, ")");
            
            // Update chunks in model
            for (var i = 0; i < transcriptModel.count; i++) {
                var chunk = transcriptModel.get(i);
                if (chunkIds.indexOf(chunk.id) !== -1) {
                    chunk.speakerId = newSpeaker;
                }
            }
        }
        
        onStatusChanged: (state, elapsedMs, chunks, recls, rtf) => {
            statusText.text = "Elapsed: " + Math.floor(elapsedMs / 1000) + "s | " +
                             "Chunks: " + chunks + " | " +
                             "Recls: " + recls + " | " +
                             "RT: " + rtf.toFixed(2) + "x";
        }
        
        onErrorOccurred: (severity, message, details) => {
            console.error("Error:", message, details);
            errorDialog.text = message + "\n\n" + details;
            errorDialog.open();
        }
    }
    
    // Main layout
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16
        
        // Top bar with start/stop button
        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            
            Button {
                id: startStopButton
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 200
                Layout.preferredHeight: 50
                
                text: bridge.isRecording ? "STOP RECORDING" : "START RECORDING"
                
                background: Rectangle {
                    color: bridge.isRecording ? "#CC3333" : root.accentColor
                    radius: 8
                    
                    Behavior on color {
                        ColorAnimation { duration: 200 }
                    }
                }
                
                contentItem: Text {
                    text: startStopButton.text
                    font.pixelSize: 16
                    font.bold: true
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                
                onClicked: {
                    if (bridge.isRecording) {
                        bridge.stopRecording();
                    } else {
                        bridge.startRecording();
                    }
                }
            }
            
            // Spacer
            Item { Layout.fillWidth: true }
            
            // Clear button
            Button {
                text: "Clear"
                onClicked: {
                    bridge.clearTranscript();
                    transcriptModel.clear();
                }
            }
        }
        
        // Status bar
        Text {
            id: statusText
            Layout.fillWidth: true
            color: root.textColor
            font.pixelSize: 12
            text: "Ready"
        }
        
        // Transcript display
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: root.surfaceColor
            radius: 8
            
            ListView {
                id: transcriptView
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                clip: true
                
                model: ListModel {
                    id: transcriptModel
                }
                
                delegate: RowLayout {
                    width: transcriptView.width - 32
                    spacing: 12
                    
                    // Speaker indicator
                    Rectangle {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        Layout.alignment: Qt.AlignTop
                        radius: 16
                        color: model.speakerId === 0 ? root.speaker0Color : root.speaker1Color
                        
                        Text {
                            anchors.centerIn: parent
                            text: "S" + model.speakerId
                            color: "white"
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }
                    
                    // Text content
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        
                        Text {
                            Layout.fillWidth: true
                            text: model.text
                            color: root.textColor
                            font.pixelSize: 18
                            wrapMode: Text.WordWrap
                            opacity: model.confidence
                        }
                        
                        Text {
                            Layout.fillWidth: true
                            text: formatTime(model.timestampMs) + 
                                  (model.confidence < 0.7 ? " (low confidence: " + 
                                   model.confidence.toFixed(2) + ")" : "")
                            color: "#888888"
                            font.pixelSize: 10
                        }
                    }
                }
                
                // Scroll to bottom when new items added
                onCountChanged: Qt.callLater(positionViewAtEnd)
            }
            
            // Empty state
            Text {
                anchors.centerIn: parent
                visible: transcriptModel.count === 0
                text: "Press START RECORDING to begin..."
                color: "#666666"
                font.pixelSize: 16
            }
        }
        
        // Settings panel
        GroupBox {
            Layout.fillWidth: true
            title: "Settings"
            
            background: Rectangle {
                color: root.surfaceColor
                radius: 8
            }
            
            label: Text {
                text: "Settings"
                color: root.textColor
                font.pixelSize: 14
                font.bold: true
            }
            
            GridLayout {
                anchors.fill: parent
                columns: 2
                rowSpacing: 8
                columnSpacing: 16
                
                Text {
                    text: "Synthetic Audio:"
                    color: root.textColor
                }
                CheckBox {
                    checked: bridge.useSyntheticAudio
                    onCheckedChanged: bridge.useSyntheticAudio = checked
                    enabled: !bridge.isRecording
                }
                
                Text {
                    text: "Audio File:"
                    color: root.textColor
                }
                TextField {
                    Layout.fillWidth: true
                    text: bridge.syntheticAudioFile
                    onTextChanged: bridge.syntheticAudioFile = text
                    enabled: !bridge.isRecording && bridge.useSyntheticAudio
                    
                    background: Rectangle {
                        color: parent.enabled ? "#1E1E1E" : "#333333"
                        border.color: parent.activeFocus ? root.accentColor : "#555555"
                        border.width: 1
                        radius: 4
                    }
                    
                    color: root.textColor
                }
                
                Text {
                    text: "Model:"
                    color: root.textColor
                }
                ComboBox {
                    model: ["tiny.en", "base.en", "small.en"]
                    currentIndex: 0
                    onCurrentTextChanged: bridge.whisperModel = currentText
                    enabled: !bridge.isRecording
                }
                
                Text {
                    text: "Max Speakers:"
                    color: root.textColor
                }
                SpinBox {
                    from: 1
                    to: 5
                    value: bridge.maxSpeakers
                    onValueChanged: bridge.maxSpeakers = value
                    enabled: !bridge.isRecording
                }
            }
        }
    }
    
    // Error dialog
    Dialog {
        id: errorDialog
        anchors.centerIn: parent
        title: "Error"
        modal: true
        standardButtons: Dialog.Ok
        
        property alias text: errorText.text
        
        Text {
            id: errorText
            color: root.textColor
            wrapMode: Text.WordWrap
            width: 400
        }
        
        background: Rectangle {
            color: root.surfaceColor
            border.color: "#CC3333"
            border.width: 2
            radius: 8
        }
    }
    
    // Helper function
    function formatTime(ms) {
        var seconds = Math.floor(ms / 1000);
        var minutes = Math.floor(seconds / 60);
        seconds = seconds % 60;
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds;
    }
}

