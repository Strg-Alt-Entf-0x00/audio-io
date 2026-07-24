// mp3_codec.h - MPEG Layer 3 audio codec (encode + decode)
// Part of audio-io-1.0.0
// Windows-native, C++20
//
// Encodes and decodes MPEG-1/2/2.5 Layer III audio (ISO/IEC 11172-3, ISO/IEC 13818-3)
// Encoding: Supports MPEG-1 Layer III at 128-320 kbps, 32/44.1/48 kHz
// Decoding: All bitrates (8-320 kbps), VBR/CBR, ID3v2 tag handling

#pragma once

#include "audio_io/audio_format.h"
#include "audio_io/audio_reader.h"
#include "audio_io/expected.h"
#include <windows.h>
#include <cstdint>
#include <vector>

namespace audio_io {
namespace mp3 {

// MP3 frame header (4 bytes)
struct Mp3FrameHeader {
    uint32_t rawHeader;
    
    // MPEG version
    int mpegVersion;      // 1, 2, or 2.5
    int layerVersion;     // Always 3 for Layer III
    
    // Audio properties
    int bitrateKbps;
    int sampleRateHz;
    int channelMode;      // 0=Stereo, 1=JointStereo, 2=DualChannel, 3=Mono
    int channelCount;     // 1 or 2
    
    // Frame properties
    int frameSize;        // Size in bytes
    int samplesPerFrame;  // 1152 or 576
    bool hasCrc;
    bool hasPadding;
};

// ID3v2 tag header
struct Id3v2Header {
    uint8_t identifier[3];  // "ID3"
    uint8_t version[2];     // e.g., [4, 0] for ID3v2.4
    uint8_t flags;
    uint8_t size[4];        // Synchsafe integer (7 bits per byte)
};

// MP3 codec class (decode + encode)
class Mp3Codec {
public:
    Mp3Codec() = default;
    ~Mp3Codec() = default;
    
    // DECODING
    // Read entire MP3 file and decode to PCM
    Expected<AudioData, AudioError> decode(const wchar_t* filePathUtf16);
    
    // Read only format information (fast - reads first frame only)
    Expected<AudioFormat, AudioError> readFormat(const wchar_t* filePathUtf16);
    
    // ENCODING
    // Encode PCM audio data to MP3 format
    // Returns MP3 file bytes ready to write to disk
    Expected<std::vector<uint8_t>, AudioError> encode(
        const float* pcmSamples,
        int64_t sampleFrameCount,
        int sampleRateHz,
        int channelCount,
        int bitrateKbps = 320
    );
    
    // Convenience: encode from AudioData
    Expected<std::vector<uint8_t>, AudioError> encode(
        const AudioData& audioData,
        int bitrateKbps = 320
    );
    
private:
    // File reading
    Expected<std::vector<uint8_t>, AudioError> readEntireFile(const wchar_t* filePathUtf16);
    
    // ID3v2 tag handling
    size_t skipId3v2Tag(const uint8_t* fileData, size_t fileSize);
    uint32_t parseId3v2Size(const uint8_t* sizeBytes);
    
    // Frame header parsing
    Expected<Mp3FrameHeader, AudioError> parseFrameHeader(uint32_t headerBytes);
    bool findNextFrameSync(const uint8_t* data, size_t dataSize, size_t* offsetOut);
    
    // Bitrate/samplerate tables
    int getBitrate(int mpegVersion, int bitrateIndex);
    int getSampleRate(int mpegVersion, int samplerateIndex);
    
    // Frame decoding (simplified - we'll use a simpler approach)
    Expected<std::vector<float>, AudioError> decodeAllFrames(
        const uint8_t* mp3Data, size_t mp3Size,
        const AudioFormat& format, size_t startOffset);
    
    // Simple MP3 frame decode (stub for now - full implementation needed)
    bool decodeFrame(const uint8_t* frameData, int frameSize, 
                    const Mp3FrameHeader& header, std::vector<float>& pcmOut);
};

} // namespace mp3
} // namespace audio_io
