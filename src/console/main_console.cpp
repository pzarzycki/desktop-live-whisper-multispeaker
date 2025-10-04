#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include "audio/windows_wasapi.hpp"

int main(int argc, char** argv) {
    audio::WindowsWasapiCapture cap;
    bool ok = false;
    if (argc > 1) {
        std::string id = argv[1];
        ok = cap.start_with_device(id);
    } else {
        ok = cap.start();
    }
    if (!ok) {
        std::cerr << "Failed to start capture" << std::endl;
        return 1;
    }
    std::cout << "Capturing... press Ctrl+C to stop" << std::endl;
    for (int i = 0; i < 50; ++i) {
        auto chunk = cap.read_chunk();
        if (!chunk.empty()) {
            std::cout << "Frames: " << chunk.size() << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    cap.stop();
    return 0;
}
