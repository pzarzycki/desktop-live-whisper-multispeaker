#pragma once
namespace audio {

class WindowsWasapiOut {
public:
    bool start(int sample_rate, int channels = 1);
    void stop();
    // Write PCM16 interleaved frames (mono input). Frames is number of mono samples.
    void write(const short* data, unsigned long long frames);

private:
    void* _dev = nullptr; // reserved
};

}
