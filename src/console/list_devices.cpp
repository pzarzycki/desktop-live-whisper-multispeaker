#include <iostream>
#include "audio/windows_wasapi.hpp"

int main() {
    auto devices = audio::WindowsWasapiCapture::list_input_devices();
    if (devices.empty()) {
        std::cout << "No active input devices found." << std::endl;
        return 0;
    }
    std::cout << "Active input devices:" << std::endl;
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << i << ": " << devices[i].name << "\n  ID: " << devices[i].id << "\n";
    }
    return 0;
}
