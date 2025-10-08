// Copyright (c) 2025 VAM Desktop Live Whisper
// Main entry point for Qt GUI application

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "transcription_bridge.hpp"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    
    // Register TranscriptionBridge type for QML
    qmlRegisterType<TranscriptionBridge>("App", 1, 0, "TranscriptionBridge");
    
    // Create QML engine
    QQmlApplicationEngine engine;
    
    // Load main QML file
    engine.loadFromModule("App", "Main");
    
    if (engine.rootObjects().isEmpty()) {
        return -1;
    }
    
    return app.exec();
}

