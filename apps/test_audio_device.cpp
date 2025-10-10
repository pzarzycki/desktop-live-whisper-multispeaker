#include "audio/audio_input_device.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <iomanip>

std::atomic<size_t> total_samples{0};
std::atomic<int> callback_count{0};
std::chrono::steady_clock::time_point start_time;

int main(int argc, char** argv) {
    std::cout << "=== Audio Device Test ===\n\n";
    
    // 1. Enumerate devices
    std::cout << "Available devices:\n";
    auto devices = audio::AudioInputFactory::enumerate_devices();
    for (size_t i = 0; i < devices.size(); ++i) {
        std::cout << "  [" << i << "] " << devices[i].name 
                  << " (" << devices[i].driver << ")";
        if (devices[i].is_default) {
            std::cout << " [DEFAULT]";
        }
        std::cout << "\n";
        std::cout << "      ID: " << devices[i].id << "\n";
    }
    std::cout << "\n";
    
    // 2. Test synthetic device
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <audio_file.wav>\n";
        std::cerr << "Note: Will test synthetic device with provided WAV file\n";
        return 1;
    }
    
    std::string wav_path = argv[1];
    std::cout << "Testing synthetic device with: " << wav_path << "\n\n";
    
    // 3. Create synthetic device
    auto device = audio::AudioInputFactory::create_device("synthetic");
    if (!device) {
        std::cerr << "Failed to create synthetic device\n";
        return 1;
    }
    
    // 4. Configure
    audio::AudioInputConfig config;
    config.device_id = "synthetic";
    config.synthetic_file_path = wav_path;
    config.synthetic_playback = true;  // Play to speakers
    config.synthetic_loop = false;
    config.buffer_size_ms = 100;
    
    // 5. Initialize with callbacks
    bool init_ok = device->initialize(
        config,
        // Audio callback
        [](const int16_t* samples, size_t sample_count, int sample_rate, int channels) {
            total_samples.fetch_add(sample_count);
            int count = callback_count.fetch_add(1);
            
            if (count % 10 == 0) {  // Print every 10th callback
                double seconds = total_samples.load() / static_cast<double>(sample_rate);
                printf("\r[%.2fs] Got %zu samples at %dHz (%dch)  ", 
                       seconds, sample_count, sample_rate, channels);
                fflush(stdout);
            }
        },
        // Error callback
        [](const std::string& error, bool is_fatal) {
            fprintf(stderr, "\n[%s] %s\n", is_fatal ? "FATAL" : "ERROR", error.c_str());
        }
    );
    
    if (!init_ok) {
        std::cerr << "Failed to initialize device\n";
        return 1;
    }
    
    auto device_info = device->get_device_info();
    std::cout << "Device initialized:\n";
    std::cout << "  Name: " << device_info.name << "\n";
    std::cout << "  Sample Rate: " << device_info.default_sample_rate << " Hz\n";
    std::cout << "  Channels: " << device_info.max_channels << "\n\n";
    
    // 6. Start capture
    std::cout << "Starting capture (you should hear audio playing)...\n";
    start_time = std::chrono::steady_clock::now();
    
    if (!device->start()) {
        std::cerr << "Failed to start device\n";
        return 1;
    }
    
    // 7. Wait for capture to finish
    while (device->is_capturing()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // 8. Stop
    device->stop();
    auto end_time = std::chrono::steady_clock::now();
    
    // 9. Calculate metrics
    double wall_clock_s = std::chrono::duration<double>(end_time - start_time).count();
    double audio_duration_s = total_samples.load() / static_cast<double>(device_info.default_sample_rate);
    double realtime_factor = wall_clock_s / std::max(0.001, audio_duration_s);
    
    std::cout << "\n\n=== Test Complete ===\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Audio duration: " << audio_duration_s << "s\n";
    std::cout << "Wall-clock time: " << wall_clock_s << "s\n";
    std::cout << "Realtime factor: " << realtime_factor << "x\n";
    std::cout << "Total samples: " << total_samples.load() << "\n";
    std::cout << "Total callbacks: " << callback_count.load() << "\n";
    
    if (realtime_factor < 0.95 || realtime_factor > 1.05) {
        std::cout << "\n⚠️  WARNING: Realtime factor outside expected range (0.95-1.05x)\n";
        std::cout << "    Expected near 1.0x for real-time playback\n";
    } else {
        std::cout << "\n✓ Realtime factor within expected range\n";
    }
    
    return 0;
}
