#include "audio_input_device_macos.hpp"
#include <CoreAudio/CoreAudio.h>
#include <vector>
#include <cstring>

namespace audio {

AudioInputDevice_macOS::AudioInputDevice_macOS() = default;

AudioInputDevice_macOS::~AudioInputDevice_macOS() {
    stop();
}

std::vector<AudioDeviceInfo> AudioInputDevice_macOS::enumerate_macos_devices() {
    std::vector<AudioDeviceInfo> devices;
    
    // Get list of audio devices
    AudioObjectPropertyAddress prop_addr = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    
    UInt32 data_size = 0;
    OSStatus status = AudioObjectGetPropertyDataSize(
        kAudioObjectSystemObject,
        &prop_addr,
        0,
        nullptr,
        &data_size
    );
    
    if (status != noErr) {
        return devices;  // Return empty list on error
    }
    
    int device_count = data_size / sizeof(AudioDeviceID);
    std::vector<AudioDeviceID> device_ids(device_count);
    
    status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &prop_addr,
        0,
        nullptr,
        &data_size,
        device_ids.data()
    );
    
    if (status != noErr) {
        return devices;
    }
    
    // Get default input device
    AudioDeviceID default_device_id = 0;
    prop_addr.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    data_size = sizeof(AudioDeviceID);
    AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &prop_addr,
        0,
        nullptr,
        &data_size,
        &default_device_id
    );
    
    // Enumerate each device
    for (AudioDeviceID device_id : device_ids) {
        // Check if this is an input device
        prop_addr.mSelector = kAudioDevicePropertyStreams;
        prop_addr.mScope = kAudioDevicePropertyScopeInput;
        data_size = 0;
        
        status = AudioObjectGetPropertyDataSize(
            device_id,
            &prop_addr,
            0,
            nullptr,
            &data_size
        );
        
        if (status != noErr || data_size == 0) {
            continue;  // Not an input device
        }
        
        // Get device name
        CFStringRef device_name_ref = nullptr;
        prop_addr.mSelector = kAudioDevicePropertyDeviceNameCFString;
        prop_addr.mScope = kAudioObjectPropertyScopeGlobal;
        data_size = sizeof(CFStringRef);
        
        status = AudioObjectGetPropertyData(
            device_id,
            &prop_addr,
            0,
            nullptr,
            &data_size,
            &device_name_ref
        );
        
        std::string device_name = "Unknown Device";
        if (status == noErr && device_name_ref) {
            char name_buffer[256];
            if (CFStringGetCString(device_name_ref, name_buffer, sizeof(name_buffer), kCFStringEncodingUTF8)) {
                device_name = name_buffer;
            }
            CFRelease(device_name_ref);
        }
        
        // Get sample rate
        Float64 sample_rate = 48000.0;
        prop_addr.mSelector = kAudioDevicePropertyNominalSampleRate;
        data_size = sizeof(Float64);
        AudioObjectGetPropertyData(
            device_id,
            &prop_addr,
            0,
            nullptr,
            &data_size,
            &sample_rate
        );
        
        // Add to list
        AudioDeviceInfo info;
        info.id = std::to_string(device_id);
        info.name = device_name;
        info.driver = "CoreAudio";
        info.default_sample_rate = static_cast<int>(sample_rate);
        info.max_channels = 2;  // Most devices support stereo
        info.is_default = (device_id == default_device_id);
        
        devices.push_back(info);
    }
    
    return devices;
}

bool AudioInputDevice_macOS::initialize(
    const AudioInputConfig& config,
    AudioCallback audio_callback,
    ErrorCallback error_callback
) {
    config_ = config;
    audio_callback_ = audio_callback;
    error_callback_ = error_callback;
    
    // Setup audio format (PCM 16-bit)
    AudioStreamBasicDescription format = {0};
    format.mSampleRate = config.sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    format.mBitsPerChannel = 16;
    format.mChannelsPerFrame = config.channels;
    format.mBytesPerFrame = (format.mBitsPerChannel / 8) * format.mChannelsPerFrame;
    format.mFramesPerPacket = 1;
    format.mBytesPerPacket = format.mBytesPerFrame * format.mFramesPerPacket;
    
    // Create audio queue
    OSStatus status = AudioQueueNewInput(
        &format,
        audio_input_callback,
        this,  // user data
        nullptr,  // run loop (nullptr = use internal thread)
        kCFRunLoopCommonModes,
        0,  // flags
        &audio_queue_
    );
    
    if (status != noErr) {
        if (error_callback_) {
            error_callback_("Failed to create AudioQueue: " + std::to_string(status), true);
        }
        return false;
    }
    
    // Set device if specified
    if (!config.device_id.empty() && config.device_id != "default") {
        AudioDeviceID device_id = std::stoul(config.device_id);
        CFStringRef device_uid_ref = nullptr;
        
        // Get device UID
        AudioObjectPropertyAddress prop_addr = {
            kAudioDevicePropertyDeviceUID,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };
        
        UInt32 data_size = sizeof(CFStringRef);
        status = AudioObjectGetPropertyData(
            device_id,
            &prop_addr,
            0,
            nullptr,
            &data_size,
            &device_uid_ref
        );
        
        if (status == noErr && device_uid_ref) {
            // Set the device on the audio queue
            status = AudioQueueSetProperty(
                audio_queue_,
                kAudioQueueProperty_CurrentDevice,
                &device_uid_ref,
                sizeof(CFStringRef)
            );
            CFRelease(device_uid_ref);
        }
    }
    
    // Allocate and enqueue buffers
    int buffer_size_bytes = (config.sample_rate * config.buffer_size_ms / 1000) * 
                            format.mBytesPerFrame;
    
    for (int i = 0; i < kNumBuffers; ++i) {
        status = AudioQueueAllocateBuffer(
            audio_queue_,
            buffer_size_bytes,
            &audio_buffers_[i]
        );
        
        if (status != noErr) {
            if (error_callback_) {
                error_callback_("Failed to allocate audio buffer", true);
            }
            AudioQueueDispose(audio_queue_, true);
            audio_queue_ = nullptr;
            return false;
        }
        
        AudioQueueEnqueueBuffer(audio_queue_, audio_buffers_[i], 0, nullptr);
    }
    
    // Store actual configuration
    actual_config_ = config;
    
    // Cache device info
    device_info_.id = config.device_id.empty() ? "default" : config.device_id;
    device_info_.name = "macOS Microphone";
    device_info_.driver = "CoreAudio";
    device_info_.default_sample_rate = config.sample_rate;
    device_info_.max_channels = config.channels;
    device_info_.is_default = (config.device_id.empty() || config.device_id == "default");
    
    return true;
}

bool AudioInputDevice_macOS::start() {
    if (is_capturing_.load()) {
        return true;  // Already capturing
    }
    
    if (!audio_queue_) {
        if (error_callback_) {
            error_callback_("AudioQueue not initialized", true);
        }
        return false;
    }
    
    should_stop_.store(false);
    
    OSStatus status = AudioQueueStart(audio_queue_, nullptr);
    if (status != noErr) {
        if (error_callback_) {
            error_callback_("Failed to start AudioQueue: " + std::to_string(status), true);
        }
        return false;
    }
    
    is_capturing_.store(true);
    return true;
}

void AudioInputDevice_macOS::stop() {
    if (!is_capturing_.load()) {
        return;
    }
    
    should_stop_.store(true);
    
    if (audio_queue_) {
        AudioQueueStop(audio_queue_, true);  // Synchronous stop
        AudioQueueDispose(audio_queue_, true);
        audio_queue_ = nullptr;
    }
    
    for (int i = 0; i < kNumBuffers; ++i) {
        audio_buffers_[i] = nullptr;  // Disposed by AudioQueueDispose
    }
    
    is_capturing_.store(false);
}

void AudioInputDevice_macOS::audio_input_callback(
    void* user_data,
    AudioQueueRef queue,
    AudioQueueBufferRef buffer,
    const AudioTimeStamp* start_time,
    UInt32 num_packets,
    const AudioStreamPacketDescription* packet_desc
) {
    auto* device = static_cast<AudioInputDevice_macOS*>(user_data);
    device->handle_audio_buffer(buffer, num_packets);
}

void AudioInputDevice_macOS::handle_audio_buffer(AudioQueueBufferRef buffer, UInt32 num_packets) {
    if (should_stop_.load() || !audio_callback_) {
        return;
    }
    
    // Buffer contains PCM16 samples
    const int16_t* samples = static_cast<const int16_t*>(buffer->mAudioData);
    size_t sample_count = buffer->mAudioDataByteSize / sizeof(int16_t);
    
    // Call user callback
    audio_callback_(
        samples,
        sample_count,
        actual_config_.sample_rate,
        actual_config_.channels
    );
    
    // Re-enqueue buffer for next capture
    if (!should_stop_.load() && audio_queue_) {
        AudioQueueEnqueueBuffer(audio_queue_, buffer, 0, nullptr);
    }
}

} // namespace audio
