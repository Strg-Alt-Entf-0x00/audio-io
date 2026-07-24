// wave_decoder.cpp - WAV/WAVE file decoder implementation
// Part of audio-io-1.0.0
// Windows-native, C++20

#include "wave_decoder.h"
#include <cstring>

#include <immintrin.h>

#include "../io/file_reader.h"

namespace audio_io {
namespace wave {

// Read entire file into memory using Windows API
Expected<std::vector<uint8_t>, AudioError> WaveDecoder::readEntireFile(const wchar_t* filePathUtf16) {
    return io::FileReader::readEntireFile(filePathUtf16);
}

// Find chunk in WAVE file
bool WaveDecoder::findChunk(const uint8_t* fileData, size_t fileSize,
                           const char* chunkId, size_t* offsetOut, size_t* sizeOut) {
    // Skip RIFF header (12 bytes)
    size_t offset = 12;
    
    while (offset + 8 <= fileSize) {
        // Read chunk header
        ChunkHeader header;
        std::memcpy(&header, fileData + offset, sizeof(ChunkHeader));
        
        // Check if this is the chunk we're looking for
        if (std::memcmp(header.chunkId, chunkId, 4) == 0) {
            *offsetOut = offset + 8;  // Skip header
            *sizeOut = header.chunkSize;
            return true;
        }
        
        // Move to next chunk (aligned to 2 bytes)
        offset += 8 + header.chunkSize;
        if (header.chunkSize % 2 != 0) {
            offset += 1;  // WAVE chunks are word-aligned
        }
    }
    
    return false;
}

// Parse fmt chunk and extract audio format
Expected<AudioFormat, AudioError> WaveDecoder::parseFmtChunk(
    const uint8_t* fmtData, size_t fmtSize) {
    
    if (fmtSize < sizeof(WaveFmtChunkPcm)) {
        return makeUnexpected(AudioError{
            "WAV fmt chunk too small",
            0
        });
    }
    
    AudioFormat format = {};
    format.fileFormat = FileFormat::Wave;
    
    // Read basic fmt fields
    WaveFmtChunkPcm fmtChunk;
    std::memcpy(&fmtChunk, fmtData, sizeof(WaveFmtChunkPcm));
    
    format.sampleRateHz = static_cast<int>(fmtChunk.sampleRateHz);
    format.channelCount = static_cast<int>(fmtChunk.channelCount);
    format.bitDepth = static_cast<int>(fmtChunk.bitsPerSample);
    
    // Determine sample format
    uint16_t audioFormat = fmtChunk.audioFormat;
    
    if (audioFormat == kWaveFormatExtensible && fmtSize >= sizeof(WaveFmtChunkExtended)) {
        // Extended format - check subformat GUID
        WaveFmtChunkExtended fmtExt;
        std::memcpy(&fmtExt, fmtData, sizeof(WaveFmtChunkExtended));
        
        // PCM GUID: 00000001-0000-0010-8000-00aa00389b71
        // Float GUID: 00000003-0000-0010-8000-00aa00389b71
        uint16_t subFormatCode = *reinterpret_cast<const uint16_t*>(fmtExt.subFormat);
        audioFormat = subFormatCode;
    }
    
    // Map audio format to SampleFormat
    if (audioFormat == kWaveFormatPcm) {
        switch (format.bitDepth) {
            case 8:  format.sampleFormat = SampleFormat::Int8; break;
            case 16: format.sampleFormat = SampleFormat::Int16; break;
            case 24: format.sampleFormat = SampleFormat::Int24; break;
            case 32: format.sampleFormat = SampleFormat::Int32; break;
            default:
                return makeUnexpected(AudioError{
                    "Unsupported WAV bit depth for PCM",
                    0
                });
        }
    } else if (audioFormat == kWaveFormatIeeeFloat) {
        switch (format.bitDepth) {
            case 32: format.sampleFormat = SampleFormat::Float32; break;
            case 64: format.sampleFormat = SampleFormat::Float64; break;
            default:
                return makeUnexpected(AudioError{
                    "Unsupported WAV bit depth for IEEE float",
                    0
                });
        }
    } else {
        return makeUnexpected(AudioError{
            "Unsupported WAV audio format (not PCM or IEEE float)",
            0
        });
    }
    
    // Validate format
    if (format.sampleRateHz < 8000 || format.sampleRateHz > 192000) {
        return makeUnexpected(AudioError{
            "Invalid WAV sample rate (must be 8000-192000 Hz)",
            0
        });
    }
    
    if (format.channelCount < 1 || format.channelCount > 8) {
        return makeUnexpected(AudioError{
            "Invalid WAV channel count (must be 1-8)",
            0
        });
    }
    
    return format;
}

// Read format only (fast)
Expected<AudioFormat, AudioError> WaveDecoder::readFormat(const wchar_t* filePathUtf16) {
    // Read entire file (we need to parse chunks anyway)
    auto fileDataResult = readEntireFile(filePathUtf16);
    if (!fileDataResult) {
        return makeUnexpected(fileDataResult.error());
    }
    
    const auto& fileData = *fileDataResult;
    
    // Verify RIFF/WAVE header
    if (fileData.size() < 12) {
        return makeUnexpected(AudioError{"File too small to be valid WAV", 0});
    }
    
    RiffChunkHeader riffHeader;
    std::memcpy(&riffHeader, fileData.data(), sizeof(RiffChunkHeader));
    
    if (std::memcmp(riffHeader.chunkId, "RIFF", 4) != 0) {
        return makeUnexpected(AudioError{"Not a valid WAV file (missing RIFF)", 0});
    }
    
    if (std::memcmp(riffHeader.format, "WAVE", 4) != 0) {
        return makeUnexpected(AudioError{"Not a valid WAV file (missing WAVE)", 0});
    }
    
    // Find fmt chunk
    size_t fmtOffset, fmtSize;
    if (!findChunk(fileData.data(), fileData.size(), "fmt ", &fmtOffset, &fmtSize)) {
        return makeUnexpected(AudioError{"WAV file missing fmt chunk", 0});
    }
    
    // Parse fmt chunk
    auto formatResult = parseFmtChunk(fileData.data() + fmtOffset, fmtSize);
    if (!formatResult) {
        return makeUnexpected(formatResult.error());
    }
    
    AudioFormat format = *formatResult;
    
    // Find data chunk to get sample count
    size_t dataOffset, dataSize;
    if (!findChunk(fileData.data(), fileData.size(), "data", &dataOffset, &dataSize)) {
        return makeUnexpected(AudioError{"WAV file missing data chunk", 0});
    }
    
    // Calculate total sample frames
    int bytesPerFrame = format.bytesPerSampleFrame();
    if (bytesPerFrame == 0) {
        return makeUnexpected(AudioError{"Invalid WAV format", 0});
    }
    
    format.totalSampleFrames = static_cast<int64_t>(dataSize) / bytesPerFrame;
    
    return format;
}

// Convert samples to normalized float
std::vector<float> WaveDecoder::convertSamplesToFloat(
    const uint8_t* sampleData, size_t sampleDataSize,
    const AudioFormat& format) {
    
    int bytesPerSample = format.bitDepth / 8;
    size_t totalSamples = sampleDataSize / bytesPerSample;
    
    std::vector<float> outputSamples(totalSamples);
    
    switch (format.sampleFormat) {
        case SampleFormat::Int8:
            convertInt8ToFloat(sampleData, outputSamples.data(), totalSamples);
            break;
        case SampleFormat::Int16:
            convertInt16ToFloat(sampleData, outputSamples.data(), totalSamples);
            break;
        case SampleFormat::Int24:
            convertInt24ToFloat(sampleData, outputSamples.data(), totalSamples);
            break;
        case SampleFormat::Int32:
            convertInt32ToFloat(sampleData, outputSamples.data(), totalSamples);
            break;
        case SampleFormat::Float32:
            convertFloat32ToFloat(sampleData, outputSamples.data(), totalSamples);
            break;
        case SampleFormat::Float64:
            convertFloat64ToFloat(sampleData, outputSamples.data(), totalSamples);
            break;
        default:
            break;
    }
    
    return outputSamples;
}

// 8-bit PCM: unsigned 0-255, center at 128
void WaveDecoder::convertInt8ToFloat(const uint8_t* input, float* output, size_t sampleCount) {
    for (size_t i = 0; i < sampleCount; ++i) {
        int8_t sample = static_cast<int8_t>(input[i] - 128);  // Convert to signed
        output[i] = static_cast<float>(sample) / 128.0f;
    }
}

// 16-bit PCM: signed -32768 to 32767
void WaveDecoder::convertInt16ToFloat(const uint8_t* input, float* output, size_t sampleCount) {
    const int16_t* samples = reinterpret_cast<const int16_t*>(input);
#if defined(__x86_64__) || defined(_M_X64)
    size_t i = 0;
    __m128 scale = _mm_set1_ps(1.0f / 32768.0f);
    
    // Process 8 samples at a time
    for (; i + 7 < sampleCount; i += 8) {
        __m128i in16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&samples[i]));
        
        // Unpack and sign-extend 16-bit to 32-bit
        __m128i lo = _mm_unpacklo_epi16(in16, in16);
        lo = _mm_srai_epi32(lo, 16);
        
        __m128i hi = _mm_unpackhi_epi16(in16, in16);
        hi = _mm_srai_epi32(hi, 16);
        
        __m128 f_lo = _mm_cvtepi32_ps(lo);
        __m128 f_hi = _mm_cvtepi32_ps(hi);
        
        _mm_storeu_ps(&output[i], _mm_mul_ps(f_lo, scale));
        _mm_storeu_ps(&output[i + 4], _mm_mul_ps(f_hi, scale));
    }
    
    // Scalar fallback for remaining
    for (; i < sampleCount; ++i) {
        output[i] = static_cast<float>(samples[i]) * (1.0f / 32768.0f);
    }
#else
    for (size_t i = 0; i < sampleCount; ++i) {
        output[i] = static_cast<float>(samples[i]) * (1.0f / 32768.0f);
    }
#endif
}

// 24-bit PCM: signed, 3 bytes little-endian
void WaveDecoder::convertInt24ToFloat(const uint8_t* input, float* output, size_t sampleCount) {
    for (size_t i = 0; i < sampleCount; ++i) {
        // Read 3 bytes and sign-extend to 32-bit
        int32_t sample = static_cast<int32_t>(
            (input[i*3 + 0] << 8) |
            (input[i*3 + 1] << 16) |
            (input[i*3 + 2] << 24)
        ) >> 8;  // Arithmetic shift preserves sign
        
        output[i] = static_cast<float>(sample) / 8388608.0f;  // 2^23
    }
}

// 32-bit PCM: signed -2147483648 to 2147483647
void WaveDecoder::convertInt32ToFloat(const uint8_t* input, float* output, size_t sampleCount) {
    const int32_t* samples = reinterpret_cast<const int32_t*>(input);
    for (size_t i = 0; i < sampleCount; ++i) {
        output[i] = static_cast<float>(samples[i]) / 2147483648.0f;
    }
}

// 32-bit IEEE float: already float, just copy
void WaveDecoder::convertFloat32ToFloat(const uint8_t* input, float* output, size_t sampleCount) {
    const float* samples = reinterpret_cast<const float*>(input);
    std::memcpy(output, samples, sampleCount * sizeof(float));
}

// 64-bit IEEE double: convert to float
void WaveDecoder::convertFloat64ToFloat(const uint8_t* input, float* output, size_t sampleCount) {
    const double* samples = reinterpret_cast<const double*>(input);
    for (size_t i = 0; i < sampleCount; ++i) {
        output[i] = static_cast<float>(samples[i]);
    }
}

// Decode entire WAV file
Expected<AudioData, AudioError> WaveDecoder::decode(const wchar_t* filePathUtf16) {
    // Read entire file once
    auto fileDataResult = readEntireFile(filePathUtf16);
    if (!fileDataResult) {
        return makeUnexpected(fileDataResult.error());
    }
    
    const auto& fileData = *fileDataResult;
    
    // Verify RIFF/WAVE header
    if (fileData.size() < 12) {
        return makeUnexpected(AudioError{"File too small to be valid WAV", 0});
    }
    
    RiffChunkHeader riffHeader;
    std::memcpy(&riffHeader, fileData.data(), sizeof(RiffChunkHeader));
    
    if (std::memcmp(riffHeader.chunkId, "RIFF", 4) != 0 || std::memcmp(riffHeader.format, "WAVE", 4) != 0) {
        return makeUnexpected(AudioError{"Not a valid WAV file", 0});
    }
    
    // Find and parse fmt chunk
    size_t fmtOffset, fmtSize;
    if (!findChunk(fileData.data(), fileData.size(), "fmt ", &fmtOffset, &fmtSize)) {
        return makeUnexpected(AudioError{"WAV file missing fmt chunk", 0});
    }
    
    auto formatResult = parseFmtChunk(fileData.data() + fmtOffset, fmtSize);
    if (!formatResult) {
        return makeUnexpected(formatResult.error());
    }
    
    AudioFormat format = *formatResult;
    
    // Find data chunk
    size_t dataOffset, dataSize;
    if (!findChunk(fileData.data(), fileData.size(), "data", &dataOffset, &dataSize)) {
        return makeUnexpected(AudioError{"WAV file missing data chunk", 0});
    }
    
    // Convert samples to float
    std::vector<float> samples = convertSamplesToFloat(
        fileData.data() + dataOffset,
        dataSize,
        format
    );
    
    // Return audio data
    AudioData audioData;
    audioData.samples = std::move(samples);
    format.totalSampleFrames = audioData.samples.size() / format.channelCount;
    audioData.format = format;
    
    return audioData;
}

} // namespace wave
} // namespace audio_io

