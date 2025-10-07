#include "audio/windows_wasapi_out.hpp"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <vector>
#include <cmath>

namespace audio {

struct RenderState {
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    WAVEFORMATEX* mix = nullptr;
    bool running = false;
    int sr = 0;            // input sample rate requested by caller
    int dev_sr = 0;        // device sample rate actually opened
    int ch = 1;           // device channels actually opened
    int bytesPerSample = 2; // 2 for PCM16, 4 for float
    bool useFloat = false;
};
static RenderState g_rs;

bool WindowsWasapiOut::start(int sample_rate, int channels) {
    if (g_rs.running) return true;
    g_rs.sr = sample_rate; g_rs.ch = channels;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&g_rs.enumerator);
    if (FAILED(hr)) return false;
    hr = g_rs.enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &g_rs.device);
    if (FAILED(hr)) return false;
    hr = g_rs.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_rs.client);
    if (FAILED(hr)) return false;
    hr = g_rs.client->GetMixFormat(&g_rs.mix);
    if (FAILED(hr) || !g_rs.mix) return false;
    // Try to open PCM16 at requested SR and given channels; fallback to device mix format
    WAVEFORMATEX fmtPCM{};
    fmtPCM.wFormatTag = WAVE_FORMAT_PCM;
    fmtPCM.nChannels = static_cast<WORD>(channels);
    fmtPCM.nSamplesPerSec = sample_rate;
    fmtPCM.wBitsPerSample = 16;
    fmtPCM.nBlockAlign = (fmtPCM.nChannels * fmtPCM.wBitsPerSample) / 8;
    fmtPCM.nAvgBytesPerSec = fmtPCM.nSamplesPerSec * fmtPCM.nBlockAlign;
    WAVEFORMATEX* closest = nullptr;
    hr = g_rs.client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &fmtPCM, &closest);
    const WAVEFORMATEX* chosen = (hr == S_OK) ? &fmtPCM : g_rs.mix;
    g_rs.ch = chosen->nChannels;
    g_rs.dev_sr = chosen->nSamplesPerSec;
    g_rs.bytesPerSample = (chosen->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) ? 4 : (chosen->wBitsPerSample / 8);
    if (chosen->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        // Inspect extensible subformat
        auto* wfex = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(chosen);
        if (IsEqualGUID(wfex->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            g_rs.bytesPerSample = 4; g_rs.useFloat = true;
        } else {
            g_rs.bytesPerSample = wfex->Format.wBitsPerSample / 8; g_rs.useFloat = false;
        }
    } else if (chosen->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        g_rs.useFloat = true;
    } else {
        g_rs.useFloat = false;
    }
    REFERENCE_TIME dur = 20 * 10000; // 20 ms
    hr = g_rs.client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, dur, 0, chosen, nullptr);
    if (FAILED(hr)) return false;
    hr = g_rs.client->GetService(__uuidof(IAudioRenderClient), (void**)&g_rs.render);
    if (FAILED(hr)) return false;
    hr = g_rs.client->Start();
    if (FAILED(hr)) return false;
    g_rs.running = true;
    return true;
}

void WindowsWasapiOut::stop() {
    if (!g_rs.running) return;
    g_rs.running = false;
    if (g_rs.client) g_rs.client->Stop();
    if (g_rs.mix) CoTaskMemFree(g_rs.mix), g_rs.mix = nullptr;
    if (g_rs.render) { g_rs.render->Release(); g_rs.render = nullptr; }
    if (g_rs.client) { g_rs.client->Release(); g_rs.client = nullptr; }
    if (g_rs.device) { g_rs.device->Release(); g_rs.device = nullptr; }
    if (g_rs.enumerator) { g_rs.enumerator->Release(); g_rs.enumerator = nullptr; }
    CoUninitialize();
}

static void resample_i16_mono(const short* in, size_t in_frames, int in_sr, int out_sr, std::vector<short>& out) {
    if (in_sr <= 0 || out_sr <= 0 || in_frames == 0) { out.clear(); return; }
    if (in_sr == out_sr) {
        out.assign(in, in + in_frames);
        return;
    }
    const double ratio = static_cast<double>(out_sr) / static_cast<double>(in_sr);
    const size_t out_len = static_cast<size_t>(std::lround(in_frames * ratio));
    out.resize(out_len);
    for (size_t i = 0; i < out_len; ++i) {
        double src_pos = i / ratio;
        size_t i0 = static_cast<size_t>(src_pos);
        if (i0 >= in_frames - 1) { out[i] = in[in_frames - 1]; continue; }
        size_t i1 = i0 + 1;
        double frac = src_pos - static_cast<double>(i0);
        double v = (1.0 - frac) * static_cast<double>(in[i0]) + frac * static_cast<double>(in[i1]);
    int vi = static_cast<int>(std::lround(v));
        if (vi > 32767) vi = 32767; else if (vi < -32768) vi = -32768;
        out[i] = static_cast<short>(vi);
    }
}

void WindowsWasapiOut::write(const short* data, unsigned long long frames) {
    if (!g_rs.running || !g_rs.render || !g_rs.client || !data || frames == 0) return;
    // Resample to device rate if needed
    const short* src = data;
    size_t src_frames = static_cast<size_t>(frames);
    std::vector<short> tmp;
    if (g_rs.sr != g_rs.dev_sr) {
        resample_i16_mono(data, static_cast<size_t>(frames), g_rs.sr, g_rs.dev_sr, tmp);
        src = tmp.data();
        src_frames = tmp.size();
    }

    UINT32 pad = 0, cap = 0;
    if (FAILED(g_rs.client->GetCurrentPadding(&pad))) return;
    if (FAILED(g_rs.client->GetBufferSize(&cap))) return;
    UINT32 avail = cap - pad;
    while (frames > 0 && avail > 0) {
        // Use resampled frame count if resampling occurred
        if (src != data) {
            // Map remaining src_frames instead of frames
            if (src_frames == 0) break;
        }
        UINT32 wanted = (src != data) ? (UINT32)std::min<size_t>(src_frames, avail) : (UINT32)std::min<unsigned long long>(frames, avail);
        UINT32 toWrite = wanted;
        BYTE* buf = nullptr;
        if (FAILED(g_rs.render->GetBuffer(toWrite, &buf))) break;
        // Interleave into device channels; input is mono PCM16
        if (g_rs.useFloat) {
            float* out = reinterpret_cast<float*>(buf);
            for (UINT32 i = 0; i < toWrite; ++i) {
                float s = (float)src[i] / 32768.0f;
                for (int c = 0; c < g_rs.ch; ++c) out[i * g_rs.ch + c] = s;
            }
        } else {
            int16_t* out = reinterpret_cast<int16_t*>(buf);
            for (UINT32 i = 0; i < toWrite; ++i) {
                int16_t s = src[i];
                for (int c = 0; c < g_rs.ch; ++c) out[i * g_rs.ch + c] = s;
            }
        }
        g_rs.render->ReleaseBuffer(toWrite, 0);
        if (src != data) {
            // Consumed resampled frames
            src_frames -= toWrite;
            src += toWrite;
        } else {
            frames -= toWrite;
            data += toWrite;
        }
        if (FAILED(g_rs.client->GetCurrentPadding(&pad))) break;
        if (FAILED(g_rs.client->GetBufferSize(&cap))) break;
        avail = cap - pad;
        if (avail == 0) {
            // Wait briefly for device to consume
            Sleep(3);
            if (FAILED(g_rs.client->GetCurrentPadding(&pad))) break;
            if (FAILED(g_rs.client->GetBufferSize(&cap))) break;
            avail = cap - pad;
        }
    }
}

}
