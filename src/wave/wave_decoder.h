// wave_decoder.h - WAV/WAVE file decoder (internal)
// Part of audio-io-1.0.0
// Windows-native, C++20
// Supports: PCM (8/16/24/32-bit int, 32/64-bit float), all samplerates, all channels

#pragma once

#include "audio_io/audio_format.h"
#include "audio_io/audio_reader.h"
#include "audio_io/expected.h"
#include <windows.h>
#include <cstdint>

namespace audio_io {
namespace wave {

// WAVE file structures (little-endian)

#pragma pack(push, 1)

// RIFF chunk header
struct RiffChunkHeader {
    char chunkId[4];        // "RIFF"
    uint32_t chunkSize;     // File size - 8
    char format[4];         // "WAVE"
};

// Generic chunk header (for fmt, data, etc.)
struct ChunkHeader {
    char chunkId[4];        // "fmt ", "data", etc.
    uint32_t chunkSize;     // Size of chunk data
};

// fmt chunk - PCM format
struct WaveFmtChunkPcm {
    uint16_t audioFormat;       // 1 = PCM, 3 = IEEE Float
    uint16_t channelCount;      // 1 = mono, 2 = stereo, etc.
    uint32_t sampleRateHz;      // Sample rate
    uint32_t byteRatePerSecond; // sampleRate * channelCount * bitsPerSample/8
    uint16_t blockAlign;        // channelCount * bitsPerSample/8
    uint16_t bitsPerSample;     // 8, 16, 24, 32, etc.
};

// fmt chunk - Extended format (for > 2 channels or > 16-bit)
struct WaveFmtChunkExtended {
    uint16_t audioFormat;       // 0xFFFE = WAVE_FORMAT_EXTENSIBLE
    uint16_t channelCount;
    uint32_t sampleRateHz;
    uint32_t byteRatePerSecond;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    uint16_t extensionSize;     // Size of extension (22)
    uint16_t validBitsPerSample;
    uint32_t channelMask;
    uint8_t subFormat[16];      // GUID
};

#pragma pack(pop)

// WAVE format tags
constexpr uint16_t kWaveFormatPcm = 0x0001;
constexpr uint16_t kWaveFormatIeeeFloat = 0x0003;
constexpr uint16_t kWaveFormatExtensible = 0xFFFE;

// WAV decoder class
class WaveDecoder {
public:
    WaveDecoder() = default;
    ~WaveDecoder() = default;
    
    // Chunk parsing (made public and static for streaming)
    static bool findChunk(const uint8_t* fileData, size_t fileSize, 
                   const char* chunkId, size_t* offsetOut, size_t* sizeOut);
    
    // Read entire WAV file
    Expected<AudioData, AudioError> decode(const wchar_t* filePathUtf16);
    
    // Read only format information (fast)
    Expected<AudioFormat, AudioError> readFormat(const wchar_t* filePathUtf16);
    
private:
    // File reading
    Expected<std::vector<uint8_t>, AudioError> readEntireFile(const wchar_t* filePathUtf16);
    
    // Format parsing
    Expected<AudioFormat, AudioError> parseFmtChunk(
        const uint8_t* fmtData, size_t fmtSize);
    
    // Sample conversion to float [-1.0, 1.0]
    std::vector<float> convertSamplesToFloat(
        const uint8_t* sampleData, size_t sampleDataSize,
        const AudioFormat& format);
    
    // Type-specific conversions
    void convertInt8ToFloat(const uint8_t* input, float* output, size_t sampleCount);
    void convertInt16ToFloat(const uint8_t* input, float* output, size_t sampleCount);
    void convertInt24ToFloat(const uint8_t* input, float* output, size_t sampleCount);
    void convertInt32ToFloat(const uint8_t* input, float* output, size_t sampleCount);
    void convertFloat32ToFloat(const uint8_t* input, float* output, size_t sampleCount);
    void convertFloat64ToFloat(const uint8_t* input, float* output, size_t sampleCount);
};

} // namespace wave
} // namespace audio_io
