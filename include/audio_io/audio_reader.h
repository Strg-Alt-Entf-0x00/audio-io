// audio_reader.h - Main audio file reading API
// Part of audio-io-1.0.0
// Windows-native, C++20

#pragma once

#include "audio_format.h"
#include "audio_metadata.h"
#include "expected.h"
#include "export.h"
#include <memory>
#include <vector>
#include <string>

namespace audio_io {

// Error information for audio operations
struct AUDIO_IO_API AudioError {
    std::string message;
    int systemErrorCode;  // Windows GetLastError() or 0
};

// Audio data container - samples interleaved (L,R,L,R,... for stereo)
struct AUDIO_IO_API AudioData {
    std::vector<float> samples;  // Normalized float samples [-1.0, 1.0]
    AudioFormat format;
};

// Options for readFile() - controls on-the-fly format conversion.
// All conversions happen inside audio-io; the caller always receives
// clean, normalized float32 samples in the requested format.
struct AUDIO_IO_API ReadOptions {
    // Target sample rate after reading.
    // 0 = keep native sample rate (no resampling).
    // Example: 44100, 48000, 16000
    int targetSampleRateHz = 0;

    // Target channel count after reading.
    // 0 = keep native channel count.
    // Supported conversions: stereo->mono (average), mono->stereo (duplicate).
    int targetChannelCount = 0;
};

// Main audio reader class
class AUDIO_IO_API AudioReader {
public:
    AudioReader() = default;
    ~AudioReader() = default;

    // Delete copy, allow move
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;
    AudioReader(AudioReader&&) noexcept = default;
    AudioReader& operator=(AudioReader&&) noexcept = default;

    // Read entire audio file into memory.
    // Applies channel conversion and resampling if requested via opts.
    // Returns audio data or error.
    Expected<AudioData, AudioError> readFile(const wchar_t* filePathUtf16,
                                              const ReadOptions& opts = ReadOptions{});
    Expected<AudioData, AudioError> readFile(const std::wstring& filePath,
                                              const ReadOptions& opts = ReadOptions{}) {
        return readFile(filePath.c_str(), opts);
    }

    // Get audio format without loading samples (fast metadata read)
    Expected<AudioFormat, AudioError> readFormat(const wchar_t* filePathUtf16);
    Expected<AudioFormat, AudioError> readFormat(const std::wstring& filePath) {
        return readFormat(filePath.c_str());
    }

    // Read metadata tags (ID3v2 for MP3, RIFF INFO for WAV)
    Expected<AudioMetadata, AudioError> readMetadata(const wchar_t* filePathUtf16);
    Expected<AudioMetadata, AudioError> readMetadata(const std::wstring& filePath) {
        return readMetadata(filePath.c_str());
    }

private:
    // Format detection
    FileFormat detectFileFormat(const wchar_t* filePathUtf16);

    // Format-specific readers (return native format, no conversion)
    Expected<AudioData, AudioError> readWaveFile(const wchar_t* filePathUtf16);
    Expected<AudioData, AudioError> readMp3File(const wchar_t* filePathUtf16);

    Expected<AudioFormat, AudioError> readWaveFormat(const wchar_t* filePathUtf16);
    Expected<AudioFormat, AudioError> readMp3Format(const wchar_t* filePathUtf16);

    // Apply ReadOptions (channel conversion + resampling) to decoded audio.
    // Modifies audioData in-place.
    AudioError applyReadOptions(AudioData& audioData, const ReadOptions& opts);
};

} // namespace audio_io
