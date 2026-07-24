// audio_format.h - Audio format definitions
// Part of audio-io-1.0.0
// Windows-native, C++20

#pragma once

#include <cstdint>

namespace audio_io {

// Audio sample format types
enum class SampleFormat {
    Unknown,
    Int8,           // 8-bit signed integer
    Int16,          // 16-bit signed integer
    Int24,          // 24-bit signed integer (packed)
    Int32,          // 32-bit signed integer
    Float32,        // 32-bit IEEE float
    Float64         // 64-bit IEEE float (double)
};

// Audio file format types
enum class FileFormat {
    Unknown,
    Wave,           // WAV/WAVE (RIFF)
    Mp3             // MPEG-1/2/2.5 Layer III
};

// Complete audio format description
struct AudioFormat {
    int sampleRateHz;           // Sample rate in Hz (e.g., 44100, 48000)
    int channelCount;           // Number of channels (1=mono, 2=stereo, etc.)
    SampleFormat sampleFormat;  // Sample data type
    FileFormat fileFormat;      // Source file format
    
    // WAV-specific
    int bitDepth;               // Bits per sample (8, 16, 24, 32)
    
    // MP3-specific
    int bitrateKbps;            // Bitrate in kbps (e.g., 320)
    bool isVbr;                 // Variable bitrate?
    
    // Duration
    int64_t totalSampleFrames;  // Total frames (one frame = one sample per channel)
    
    // Calculated properties
    double durationSeconds() const {
        return sampleRateHz > 0 
            ? static_cast<double>(totalSampleFrames) / sampleRateHz 
            : 0.0;
    }
    
    int bytesPerSampleFrame() const {
        int bytesPerSample = 0;
        switch (sampleFormat) {
            case SampleFormat::Int8:    bytesPerSample = 1; break;
            case SampleFormat::Int16:   bytesPerSample = 2; break;
            case SampleFormat::Int24:   bytesPerSample = 3; break;
            case SampleFormat::Int32:   bytesPerSample = 4; break;
            case SampleFormat::Float32: bytesPerSample = 4; break;
            case SampleFormat::Float64: bytesPerSample = 8; break;
            default: return 0;
        }
        return bytesPerSample * channelCount;
    }
};

} // namespace audio_io
