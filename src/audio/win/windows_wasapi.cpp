// Minimal WASAPI capture (shared mode) for smoke test purposes.
// This implementation intentionally avoids complex error handling and format negotiation.
// It opens the default audio input device and pulls small chunks, converting to int16 mono.

#include "audio/windows_wasapi.hpp"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <functiondiscoverykeys_devpkey.h>
#include <vector>
#include <atomic>

namespace audio {

namespace {
	struct WasapiState {
		IMMDeviceEnumerator* enumerator = nullptr;
		IMMDevice* device = nullptr;
		IAudioClient* client = nullptr;
		IAudioCaptureClient* capture = nullptr;
		WAVEFORMATEX* mixFormat = nullptr;
		std::atomic<bool> running{false};
	};

	static WasapiState g_ws;

	template <typename T>
	void safe_release(T** p) {
		if (p && *p) { (*p)->Release(); *p = nullptr; }
	}
}

bool WindowsWasapiCapture::start() {
	if (g_ws.running.load()) return true;
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
						  __uuidof(IMMDeviceEnumerator), (void**)&g_ws.enumerator);
	if (FAILED(hr)) return false;

	hr = g_ws.enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &g_ws.device);
	if (FAILED(hr)) return false;

	hr = g_ws.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_ws.client);
	if (FAILED(hr)) return false;

	WAVEFORMATEX* mix = nullptr;
	hr = g_ws.client->GetMixFormat(&mix);
	if (FAILED(hr) || !mix) return false;

	// Try to use 16 kHz mono float in shared mode; fallback to closest or mix format
	WAVEFORMATEXTENSIBLE desired{};
	desired.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	desired.Format.nChannels = 1;
	desired.Format.nSamplesPerSec = 16000;
	desired.Format.wBitsPerSample = 32;
	desired.Format.nBlockAlign = (desired.Format.nChannels * desired.Format.wBitsPerSample) / 8; // 4
	desired.Format.nAvgBytesPerSec = desired.Format.nSamplesPerSec * desired.Format.nBlockAlign; // 64k
	desired.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	desired.Samples.wValidBitsPerSample = 32;
	desired.dwChannelMask = SPEAKER_FRONT_CENTER;
	desired.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

	WAVEFORMATEX* closest = nullptr;
	hr = g_ws.client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &desired.Format, &closest);
	const WAVEFORMATEX* chosen = nullptr;
	if (hr == S_OK) {
		chosen = &desired.Format;
	} else if (hr == S_FALSE && closest) {
		chosen = closest;
	} else {
		chosen = mix;
	}

	// Shared mode, 20 ms buffer
	REFERENCE_TIME hnsBufferDuration = 20 * 10000; // 20 ms
	hr = g_ws.client->Initialize(AUDCLNT_SHAREMODE_SHARED,
								 0,
								 hnsBufferDuration, 0, chosen, nullptr);
	if (FAILED(hr)) {
		if (closest) CoTaskMemFree(closest);
		if (mix) CoTaskMemFree(mix);
		return false;
	}
	// Store a copy of the chosen format in mixFormat for later queries
	if (closest) {
		g_ws.mixFormat = closest; // take ownership
		CoTaskMemFree(mix);
	} else if (chosen == &desired.Format) {
		// Allocate and copy desired into CoTaskMem to unify freeing path
		size_t sz = sizeof(WAVEFORMATEXTENSIBLE);
		WAVEFORMATEXTENSIBLE* copy = (WAVEFORMATEXTENSIBLE*)CoTaskMemAlloc(sz);
		if (!copy) { CoTaskMemFree(mix); return false; }
		*copy = desired;
		g_ws.mixFormat = &copy->Format;
		CoTaskMemFree(mix);
	} else {
		g_ws.mixFormat = mix; // using device mix format
	}

	hr = g_ws.client->GetService(__uuidof(IAudioCaptureClient), (void**)&g_ws.capture);
	if (FAILED(hr)) return false;

	hr = g_ws.client->Start();
	if (FAILED(hr)) return false;

	g_ws.running.store(true);
	return true;
}

void WindowsWasapiCapture::stop() {
	if (!g_ws.running.load()) return;
	g_ws.running.store(false);
	if (g_ws.client) g_ws.client->Stop();
	if (g_ws.mixFormat) CoTaskMemFree(g_ws.mixFormat), g_ws.mixFormat = nullptr;
	safe_release(&g_ws.capture);
	safe_release(&g_ws.client);
	safe_release(&g_ws.device);
	safe_release(&g_ws.enumerator);
	CoUninitialize();
}

std::vector<int16_t> WindowsWasapiCapture::read_chunk() {
	std::vector<int16_t> out;
	if (!g_ws.running.load() || !g_ws.capture || !g_ws.mixFormat) return out;

	const WAVEFORMATEX* wfx = g_ws.mixFormat;
	const WORD ch = wfx->nChannels;
	const WORD bps = wfx->wBitsPerSample; // bits per sample
	const WORD bpf = wfx->nBlockAlign;    // bytes per frame
	bool isFloat = (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
	bool isPCM = (wfx->wFormatTag == WAVE_FORMAT_PCM);
	if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		const WAVEFORMATEXTENSIBLE* wfex = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
		if (IsEqualGUID(wfex->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) isFloat = true;
		else if (IsEqualGUID(wfex->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) isPCM = true;
	}

	for (;;) {
		UINT32 packetFrames = 0;
		HRESULT hr = g_ws.capture->GetNextPacketSize(&packetFrames);
		if (FAILED(hr) || packetFrames == 0) break;

		BYTE* data = nullptr;
		UINT32 numFrames = 0;
		DWORD flags = 0;
		hr = g_ws.capture->GetBuffer(&data, &numFrames, &flags, nullptr, nullptr);
		if (FAILED(hr) || numFrames == 0) {
			if (SUCCEEDED(hr)) g_ws.capture->ReleaseBuffer(numFrames);
			break;
		}

		size_t old = out.size();
		out.resize(old + numFrames);

		if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
			std::fill(out.begin() + old, out.end(), 0);
			g_ws.capture->ReleaseBuffer(numFrames);
			continue;
		}

		if (isFloat && bps == 32) {
			const float* f = reinterpret_cast<const float*>(data);
			for (size_t i = 0; i < numFrames; ++i) {
				float sum = 0.0f;
				for (WORD c = 0; c < ch; ++c) sum += f[i * ch + c];
				float mono = sum / (ch ? ch : 1);
				if (mono > 1.0f) mono = 1.0f; else if (mono < -1.0f) mono = -1.0f;
				int v = static_cast<int>(mono * 32767.0f);
				if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
				out[old + i] = static_cast<int16_t>(v);
			}
		} else if (isPCM) {
			if (bps == 16) {
				const int16_t* s = reinterpret_cast<const int16_t*>(data);
				for (size_t i = 0; i < numFrames; ++i) {
					int sum = 0;
					for (WORD c = 0; c < ch; ++c) sum += s[i * ch + c];
					out[old + i] = static_cast<int16_t>(sum / (ch ? ch : 1));
				}
			} else if (bps == 24 || bps == 32) {
				const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
				for (size_t i = 0; i < numFrames; ++i) {
					int acc = 0;
					for (WORD c = 0; c < ch; ++c) {
						const uint8_t* s = p + i * bpf + c * (bps / 8);
						int32_t v = 0;
						if (bps == 24) {
							int32_t t = (s[0] | (s[1] << 8) | (s[2] << 16));
							if (t & 0x800000) t |= ~0xFFFFFF; // sign extend 24-bit
							v = t >> 8; // to ~16-bit
						} else {
							v = *reinterpret_cast<const int32_t*>(s) >> 16;
						}
						acc += v;
					}
					out[old + i] = static_cast<int16_t>(acc / (ch ? ch : 1));
				}
			} else {
				std::fill(out.begin() + old, out.end(), 0);
			}
		} else {
			std::fill(out.begin() + old, out.end(), 0);
		}

		g_ws.capture->ReleaseBuffer(numFrames);
	}

	return out;
}

std::vector<WindowsWasapiCapture::DeviceInfo> WindowsWasapiCapture::list_input_devices() {
	std::vector<DeviceInfo> out;
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return out;
	IMMDeviceEnumerator* enumerator = nullptr;
	IMMDeviceCollection* collection = nullptr;
	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
						  __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
	if (FAILED(hr)) return out;
	hr = enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
	if (FAILED(hr)) { enumerator->Release(); return out; }
	UINT count = 0;
	collection->GetCount(&count);
	for (UINT i = 0; i < count; ++i) {
		IMMDevice* dev = nullptr;
		if (SUCCEEDED(collection->Item(i, &dev))) {
			LPWSTR id = nullptr;
			if (SUCCEEDED(dev->GetId(&id))) {
				IPropertyStore* props = nullptr;
				if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
					PROPVARIANT varName;
					PropVariantInit(&varName);
					if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &varName))) {
						// Convert wide strings to UTF-8
						int lenId = WideCharToMultiByte(CP_UTF8, 0, id, -1, nullptr, 0, nullptr, nullptr);
						int lenName = WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, nullptr, 0, nullptr, nullptr);
						std::string sid(lenId > 0 ? lenId - 1 : 0, '\0');
						std::string sname(lenName > 0 ? lenName - 1 : 0, '\0');
						if (lenId > 1) WideCharToMultiByte(CP_UTF8, 0, id, -1, sid.data(), lenId, nullptr, nullptr);
						if (lenName > 1) WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, sname.data(), lenName, nullptr, nullptr);
						out.push_back(DeviceInfo{sid, sname});
					}
					PropVariantClear(&varName);
					props->Release();
				}
				CoTaskMemFree(id);
			}
			dev->Release();
		}
	}
	if (collection) collection->Release();
	if (enumerator) enumerator->Release();
	CoUninitialize();
	return out;
}

bool WindowsWasapiCapture::start_with_device(const std::string& device_id_utf8) {
	if (g_ws.running.load()) return true;
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

	hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
						  __uuidof(IMMDeviceEnumerator), (void**)&g_ws.enumerator);
	if (FAILED(hr)) return false;

	// Convert UTF-8 id to wide
	int wlen = MultiByteToWideChar(CP_UTF8, 0, device_id_utf8.c_str(), -1, nullptr, 0);
	std::wstring wid(wlen > 0 ? wlen - 1 : 0, L'\0');
	if (wlen > 1) MultiByteToWideChar(CP_UTF8, 0, device_id_utf8.c_str(), -1, wid.data(), wlen);

	hr = g_ws.enumerator->GetDevice(wid.c_str(), &g_ws.device);
	if (FAILED(hr)) return false;

	hr = g_ws.device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&g_ws.client);
	if (FAILED(hr)) return false;

	WAVEFORMATEX* mix = nullptr;
	hr = g_ws.client->GetMixFormat(&mix);
	if (FAILED(hr) || !mix) return false;

	WAVEFORMATEXTENSIBLE desired{};
	desired.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
	desired.Format.nChannels = 1;
	desired.Format.nSamplesPerSec = 16000;
	desired.Format.wBitsPerSample = 32;
	desired.Format.nBlockAlign = (desired.Format.nChannels * desired.Format.wBitsPerSample) / 8;
	desired.Format.nAvgBytesPerSec = desired.Format.nSamplesPerSec * desired.Format.nBlockAlign;
	desired.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
	desired.Samples.wValidBitsPerSample = 32;
	desired.dwChannelMask = SPEAKER_FRONT_CENTER;
	desired.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

	WAVEFORMATEX* closest = nullptr;
	hr = g_ws.client->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &desired.Format, &closest);
	const WAVEFORMATEX* chosen = nullptr;
	if (hr == S_OK) {
		chosen = &desired.Format;
	} else if (hr == S_FALSE && closest) {
		chosen = closest;
	} else {
		chosen = mix;
	}

	REFERENCE_TIME hnsBufferDuration = 20 * 10000; // 20 ms
	hr = g_ws.client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, hnsBufferDuration, 0, chosen, nullptr);
	if (FAILED(hr)) {
		if (closest) CoTaskMemFree(closest);
		if (mix) CoTaskMemFree(mix);
		return false;
	}
	if (closest) {
		g_ws.mixFormat = closest;
		CoTaskMemFree(mix);
	} else if (chosen == &desired.Format) {
		size_t sz = sizeof(WAVEFORMATEXTENSIBLE);
		WAVEFORMATEXTENSIBLE* copy = (WAVEFORMATEXTENSIBLE*)CoTaskMemAlloc(sz);
		if (!copy) { CoTaskMemFree(mix); return false; }
		*copy = desired;
		g_ws.mixFormat = &copy->Format;
		CoTaskMemFree(mix);
	} else {
		g_ws.mixFormat = mix;
	}

	hr = g_ws.client->GetService(__uuidof(IAudioCaptureClient), (void**)&g_ws.capture);
	if (FAILED(hr)) return false;

	hr = g_ws.client->Start();
	if (FAILED(hr)) return false;
	g_ws.running.store(true);
	return true;
}

int WindowsWasapiCapture::sample_rate() const {
	if (!g_ws.mixFormat) return 0;
	return static_cast<int>(g_ws.mixFormat->nSamplesPerSec);
}

int WindowsWasapiCapture::channels() const {
	if (!g_ws.mixFormat) return 0;
	return static_cast<int>(g_ws.mixFormat->nChannels);
}

int WindowsWasapiCapture::bits_per_sample() const {
	if (!g_ws.mixFormat) return 0;
	return static_cast<int>(g_ws.mixFormat->wBitsPerSample);
}

bool WindowsWasapiCapture::is_float() const {
	if (!g_ws.mixFormat) return false;
	const WAVEFORMATEX* wfx = g_ws.mixFormat;
	if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
	if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
		const WAVEFORMATEXTENSIBLE* wfex = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
		return IsEqualGUID(wfex->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) ? true : false;
	}
	return false;
}

} // namespace audio

