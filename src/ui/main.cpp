#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QObject>
#include <QString>

#include "audio/windows_wasapi.hpp"

class LiveBridge : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE void captureStart() { cap_.start(); }
    Q_INVOKABLE void captureStop() { cap_.stop(); }
    Q_INVOKABLE QString pollTranscript() {
        auto chunk = cap_.read_chunk();
        if (!chunk.empty()) {
            return QString("Frames: %1").arg(static_cast<int>(chunk.size()));
        }
        return {};
    }
private:
    audio::WindowsWasapiCapture cap_;
};

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    LiveBridge live;
    engine.rootContext()->setContextProperty("Live", &live);
    engine.loadFromModule("App", "Main");
    if (engine.rootObjects().isEmpty()) return -1;
    return app.exec();
}
