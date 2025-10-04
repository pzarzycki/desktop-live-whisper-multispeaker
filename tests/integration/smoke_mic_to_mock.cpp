// Minimal smoke test scaffold: start/stop WASAPI capture and try reading a small chunk.
#include <cstdlib>
#include <thread>
#include <chrono>
#include "audio/windows_wasapi.hpp"
#include "core/ring_buffer.hpp"

int main() {
    audio::WindowsWasapiCapture cap;
    if (!cap.start()) {
        return 1; // fail if we cannot start device
    }

    RingBufferI16 rb(16000 * 2); // ~2s at 16kHz
    // Try to read a few times and push into ring buffer
    for (int i = 0; i < 10; ++i) {
        auto chunk = cap.read_chunk();
        if (!chunk.empty()) {
            rb.push(chunk.data(), chunk.size());
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    cap.stop();

    // For now, still failing by design until we assert constraints and finalize capture
    return rb.size() > 0 ? 1 : 1; // keep failing for TDD; switch to 0 once ready
}
