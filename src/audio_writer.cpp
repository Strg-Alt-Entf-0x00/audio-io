// audio_writer.cpp - Audio file writing and encoding implementation
// Part of audio-io-1.0.0

#include "audio_io/audio_writer.h"
#include "metadata/id3v2_writer.h"
#include "metadata/riff_info_writer.h"
#include "mp3/mp3_codec.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>
#include <immintrin.h>

namespace audio_io {

// Encode PCM to MP3 (with optional metadata)
Expected<std::vector<uint8_t>, AudioError> AudioWriter::encodeMp3(
    const float* pcmSamples,
    int64_t sampleFrameCount,
    int sampleRateHz,
    int channelCount,
    int bitrateKbps,
    const AudioMetadata* metadata
) {
    mp3::Mp3Codec codec;
    auto result = codec.encode(pcmSamples, sampleFrameCount, sampleRateHz, channelCount, bitrateKbps);
    if (!result) return result;
    
    if (metadata && !metadata->isEmpty()) {
        return metadata::Id3v2Writer::prependTag(result.value(), *metadata);
    }
    
    return result;
}

// Convenience wrapper
Expected<std::vector<uint8_t>, AudioError> AudioWriter::encodeMp3(
    const AudioData& audioData,
    int bitrateKbps,
    const AudioMetadata* metadata
) {
    return encodeMp3(
        audioData.samples.data(),
        audioData.format.totalSampleFrames,
        audioData.format.sampleRateHz,
        audioData.format.channelCount,
        bitrateKbps,
        metadata
    );
}

// Encode PCM to WAV (with optional RIFF INFO metadata)
Expected<std::vector<uint8_t>, AudioError> AudioWriter::encodeWav(
    const float* pcmSamples,
    int64_t sampleFrameCount,
    int sampleRateHz,
    int channelCount,
    int bitDepth,
    const AudioMetadata* metadata
) {
    if (bitDepth != 16 && bitDepth != 24 && bitDepth != 32) {
        return makeUnexpected(AudioError{"Only 16-bit, 24-bit, and 32-bit (float) WAV supported", 0});
    }

    std::vector<uint8_t> wavData;
    
    // Calculate sizes
    int byteRate = sampleRateHz * channelCount * (bitDepth / 8);
    short blockAlign = static_cast<short>(channelCount * (bitDepth / 8));
    int dataSize = static_cast<int>(sampleFrameCount * channelCount * (bitDepth / 8));
    int chunkSize = 36 + dataSize;
    
    wavData.reserve(44 + dataSize);
    
    // RIFF chunk descriptor
    wavData.insert(wavData.end(), {'R', 'I', 'F', 'F'});
    wavData.insert(wavData.end(), reinterpret_cast<uint8_t*>(&chunkSize), reinterpret_cast<uint8_t*>(&chunkSize) + 4);
    wavData.insert(wavData.end(), {'W', 'A', 'V', 'E'});
    
    // fmt sub-chunk
    int fmtChunkSize = 16;
    short audioFormat = (bitDepth == 32) ? 3 : 1; // 1 = PCM, 3 = IEEE float
    short numChannels = static_cast<short>(channelCount);
    
    wavData.insert(wavData.end(), {'f', 'm', 't', ' '});
    wavData.insert(wavData.end(), reinterpret_cast<uint8_t*>(&fmtChunkSize), reinterpret_cast<uint8_t*>(&fmtChunkSize) + 4);
    wavData.insert(wavData.end(), reinterpret_cast<uint8_t*>(&audioFormat), reinterpret_cast<uint8_t*>(&audioFormat) + 2);
    wavData.insert(wavData.end(), reinterpret_cast<uint8_t*>(&numChannels), reinterpret_cast<uint8_t*>(&numChannels) + 2);
    wavData.insert(wavData.end(), reinterpret_cast<uint8_t*>(&sampleRateHz), reinterpret_cast<uint8_t*>(&sampleRateHz) + 4);
    wavData.insert(wavData.end(), reinterpret_cast<uint8_t*>(&byteRate), reinterpret_cast<uint8_t*>(&byteRate) + 4);
    wavData.insert(wavData.end(), reinterpret_cast<uint8_t*>(&blockAlign), reinterpret_cast<uint8_t*>(&blockAlign) + 2);
    short bitsPerSample = static_cast<short>(bitDepth);
    wavData.insert(wavData.end(), reinterpret_cast<uint8_t*>(&bitsPerSample), reinterpret_cast<uint8_t*>(&bitsPerSample) + 2);
    
    // data sub-chunk
    wavData.insert(wavData.end(), {'d', 'a', 't', 'a'});
    wavData.insert(wavData.end(), reinterpret_cast<uint8_t*>(&dataSize), reinterpret_cast<uint8_t*>(&dataSize) + 4);
    
    // Write audio data
    size_t headerSize = wavData.size();
    wavData.resize(headerSize + dataSize);
    uint8_t* outPtr = wavData.data() + headerSize;
    
    int totalSamples = static_cast<int>(sampleFrameCount * channelCount);
    if (bitDepth == 16) {
        // TPDF Dithering: Triangular Probability Density Function
        // Eliminates quantization distortion when converting Float to Int16
        uint32_t rng_state = 123456789;
        
#if defined(__x86_64__) || defined(_M_X64)
        int i = 0;
        __m128i rng = _mm_set_epi32(rng_state ^ 0x12345678, rng_state ^ 0x9ABCDEF0, 
                                    rng_state ^ 0x0FEDCBA9, rng_state ^ 0x87654321);
        __m128 scale32k = _mm_set1_ps(32768.0f);
        __m128 norm = _mm_set1_ps(1.0f / 65535.0f);
        __m128 offset = _mm_set1_ps(-0.5f);
        __m128i mask = _mm_set1_epi32(0xFFFF);
        
        for (; i <= totalSamples - 4; i += 4) {
            rng = _mm_xor_si128(rng, _mm_slli_epi32(rng, 13));
            rng = _mm_xor_si128(rng, _mm_srli_epi32(rng, 17));
            rng = _mm_xor_si128(rng, _mm_slli_epi32(rng, 5));
            __m128i r1_int = _mm_and_si128(rng, mask);
            __m128 r1_flt = _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(r1_int), norm), offset);
            
            rng = _mm_xor_si128(rng, _mm_slli_epi32(rng, 13));
            rng = _mm_xor_si128(rng, _mm_srli_epi32(rng, 17));
            rng = _mm_xor_si128(rng, _mm_slli_epi32(rng, 5));
            __m128i r2_int = _mm_and_si128(rng, mask);
            __m128 r2_flt = _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(r2_int), norm), offset);
            
            __m128 tpdf = _mm_add_ps(r1_flt, r2_flt);
            __m128 floats = _mm_loadu_ps(&pcmSamples[i]);
            __m128 scaled = _mm_add_ps(_mm_mul_ps(floats, scale32k), tpdf);
            __m128i ints = _mm_cvtps_epi32(scaled);
            __m128i pcm16 = _mm_packs_epi32(ints, ints);
            _mm_storel_epi64((__m128i*)outPtr, pcm16);
            outPtr += 8;
        }
        
        rng_state = _mm_cvtsi128_si32(rng);
        
        for (; i < totalSamples; ++i) {
            float sample = pcmSamples[i];
            rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
            float r1 = (float)(rng_state & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;
            rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
            float r2 = (float)(rng_state & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;
            float val = sample * 32768.0f + (r1 + r2);
            short pcm16;
            if (val >= 32767.0f) pcm16 = 32767;
            else if (val <= -32768.0f) pcm16 = -32768;
            else pcm16 = static_cast<short>(val);
            std::memcpy(outPtr, &pcm16, 2);
            outPtr += 2;
        }
#else
        for (int i = 0; i < totalSamples; ++i) {
            float sample = pcmSamples[i];
            rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
            float r1 = (float)(rng_state & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;
            rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
            float r2 = (float)(rng_state & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;
            float val = sample * 32768.0f + (r1 + r2);
            short pcm16;
            if (val >= 32767.0f) pcm16 = 32767;
            else if (val <= -32768.0f) pcm16 = -32768;
            else pcm16 = static_cast<short>(val);
            std::memcpy(outPtr, &pcm16, 2);
            outPtr += 2;
        }
#endif
    } else if (bitDepth == 24) {
        // TPDF Dithering for 24-bit
        uint32_t rng_state = 123456789;
        const float max_val = 8388607.0f; // 2^23 - 1
        const float min_val = -8388608.0f;
        
        for (int i = 0; i < totalSamples; ++i) {
            float sample = pcmSamples[i];
            
            rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
            float r1 = (float)(rng_state & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;
            rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
            float r2 = (float)(rng_state & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;
            
            float dither = r1 + r2;
            float val = sample * 8388608.0f + dither;
            
            int pcm24;
            if (val >= max_val) pcm24 = 8388607;
            else if (val <= min_val) pcm24 = -8388608;
            else pcm24 = static_cast<int>(val);
            
            // Write 3 bytes little-endian
            outPtr[0] = static_cast<uint8_t>(pcm24 & 0xFF);
            outPtr[1] = static_cast<uint8_t>((pcm24 >> 8) & 0xFF);
            outPtr[2] = static_cast<uint8_t>((pcm24 >> 16) & 0xFF);
            outPtr += 3;
        }
    } else {
        std::memcpy(outPtr, pcmSamples, totalSamples * sizeof(float));
    }
    
    if (metadata && !metadata->isEmpty()) {
        return metadata::RiffInfoWriter::appendInfoChunk(wavData, *metadata);
    }
    return wavData;
}

// Convenience wrapper
Expected<std::vector<uint8_t>, AudioError> AudioWriter::encodeWav(
    const AudioData& audioData,
    int bitDepth,
    const AudioMetadata* metadata
) {
    return encodeWav(
        audioData.samples.data(),
        audioData.format.totalSampleFrames,
        audioData.format.sampleRateHz,
        audioData.format.channelCount,
        bitDepth,
        metadata
    );
}

// Write to file
Expected<bool, AudioError> AudioWriter::writeFile(
    const wchar_t* filePathUtf16,
    const AudioData& audioData,
    int quality,
    const AudioMetadata* metadata
) {
    // Detect format from extension - handles mixed case (.WAV, .Mp3, etc.)
    std::wstring path(filePathUtf16);
    bool isMp3 = false;
    {
        size_t dotPos = path.find_last_of(L'.');
        if (dotPos != std::wstring::npos) {
            std::wstring ext = path.substr(dotPos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
            isMp3 = (ext == L"mp3");
        }
    }
    
    // Encode based on format
    Expected<std::vector<uint8_t>, AudioError> encodeResult =
        isMp3 ? encodeMp3(audioData, quality, metadata)
              : encodeWav(audioData, quality, metadata);
    
    if (!encodeResult) {
        return makeUnexpected(encodeResult.error());
    }
    
    const auto& fileData = *encodeResult;
    
    // Write to file using Windows API
    HANDLE hFile = CreateFileW(
        filePathUtf16,
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        return makeUnexpected(AudioError{
            "Cannot create output file",
            static_cast<int>(error)
        });
    }
    
    DWORD bytesWritten;
    BOOL success = WriteFile(
        hFile,
        fileData.data(),
        static_cast<DWORD>(fileData.size()),
        &bytesWritten,
        nullptr
    );
    
    CloseHandle(hFile);
    
    if (!success || bytesWritten != fileData.size()) {
        return makeUnexpected(AudioError{
            "Failed to write audio file data",
            0
        });
    }
    
    return true;
}

// writeToFile - properly scoped as AudioWriter member
Expected<bool, AudioError> AudioWriter::writeToFile(
    const std::wstring& filePath,
    const std::vector<uint8_t>& fileData
) {
    HANDLE hFile = CreateFileW(
        filePath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return makeUnexpected(AudioError{
            "Cannot create output file", static_cast<int>(GetLastError())
        });
    }
    DWORD bytesWritten;
    BOOL success = WriteFile(
        hFile, fileData.data(), static_cast<DWORD>(fileData.size()),
        &bytesWritten, nullptr
    );
    CloseHandle(hFile);
    if (!success || bytesWritten != fileData.size()) {
        return makeUnexpected(AudioError{"Failed to write file data", 0});
    }
    return true;
}

// ============================================================================
// AudioWriteStream - Full Implementation
// ============================================================================
// WAV: writes header on open(), streams PCM chunks, patches sizes on close().
// MP3: accumulates PCM in memory, encodes all at once on close() via Mp3Codec.
//      (mp3enc.h cannot be included here due to windows.h <cmath> collision)

#include "mp3/mp3_codec.h"

struct AudioWriteStream::Impl {
    HANDLE    fileHandle = INVALID_HANDLE_VALUE;
    bool      isWav      = false;
    int       sampleRate = 0;
    int       channels   = 0;
    int       bitDepth   = 0;  // WAV only: 16, 24, or 32
    int64_t   totalSamplesWritten = 0;

    // MP3-specific: buffer all PCM, encode on close()
    int                    mp3Bitrate = 0;
    std::vector<float>     mp3PcmBuffer;

    // TPDF dither PRNG state (persistent across chunks for determinism)
    uint32_t  rngState = 123456789;
};

AudioWriteStream::AudioWriteStream() : m_impl(new Impl()) {}

AudioWriteStream::~AudioWriteStream() {
    if (m_impl) {
        if (m_impl->fileHandle != INVALID_HANDLE_VALUE) {
            close(); // best-effort finalize
        }
        delete m_impl;
    }
}

AudioWriteStream::AudioWriteStream(AudioWriteStream&& other) noexcept
    : m_impl(other.m_impl) {
    other.m_impl = nullptr;
}

AudioWriteStream& AudioWriteStream::operator=(AudioWriteStream&& other) noexcept {
    if (this != &other) {
        if (m_impl) {
            if (m_impl->fileHandle != INVALID_HANDLE_VALUE) close();
            delete m_impl;
        }
        m_impl = other.m_impl;
        other.m_impl = nullptr;
    }
    return *this;
}

bool AudioWriteStream::isOpen() const {
    return m_impl && m_impl->fileHandle != INVALID_HANDLE_VALUE;
}

Expected<bool, AudioError> AudioWriteStream::open(
    const std::wstring& filePath,
    FileFormat format,
    int sampleRateHz,
    int channelCount,
    int bitDepth_or_bitrate,
    const AudioMetadata* /*metadata*/
) {
    if (!m_impl) return makeUnexpected(AudioError{"Stream object moved-from", 0});
    if (m_impl->fileHandle != INVALID_HANDLE_VALUE) {
        return makeUnexpected(AudioError{"Stream already open", 0});
    }

    HANDLE hFile = CreateFileW(
        filePath.c_str(), GENERIC_WRITE | GENERIC_READ, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        return makeUnexpected(AudioError{
            "Cannot create output file", static_cast<int>(GetLastError())
        });
    }
    m_impl->fileHandle  = hFile;
    m_impl->sampleRate  = sampleRateHz;
    m_impl->channels    = channelCount;
    m_impl->totalSamplesWritten = 0;

    if (format == FileFormat::Wave) {
        m_impl->isWav    = true;
        m_impl->bitDepth = bitDepth_or_bitrate;
        if (m_impl->bitDepth != 16 && m_impl->bitDepth != 24 && m_impl->bitDepth != 32) {
            CloseHandle(hFile);
            m_impl->fileHandle = INVALID_HANDLE_VALUE;
            return makeUnexpected(AudioError{"WAV stream: bitDepth must be 16, 24, or 32", 0});
        }

        // Write a WAV header with placeholder sizes (patched in close())
        uint8_t header[44];
        memset(header, 0, 44);

        int bytesPerSample = m_impl->bitDepth / 8;
        int byteRate       = sampleRateHz * channelCount * bytesPerSample;
        short blockAlign   = static_cast<short>(channelCount * bytesPerSample);
        short audioFormat  = (m_impl->bitDepth == 32) ? 3 : 1; // IEEE float or PCM
        short numChannels  = static_cast<short>(channelCount);
        short bitsPerSamp  = static_cast<short>(m_impl->bitDepth);

        memcpy(header + 0, "RIFF", 4);
        memcpy(header + 8, "WAVE", 4);
        memcpy(header + 12, "fmt ", 4);
        int fmtSize = 16;
        memcpy(header + 16, &fmtSize, 4);
        memcpy(header + 20, &audioFormat, 2);
        memcpy(header + 22, &numChannels, 2);
        memcpy(header + 24, &sampleRateHz, 4);
        memcpy(header + 28, &byteRate, 4);
        memcpy(header + 32, &blockAlign, 2);
        memcpy(header + 34, &bitsPerSamp, 2);
        memcpy(header + 36, "data", 4);
        // header[4..7] and header[40..43] are zero (patched in close())

        DWORD bw;
        WriteFile(hFile, header, 44, &bw, nullptr);
    } else if (format == FileFormat::Mp3) {
        m_impl->isWav      = false;
        m_impl->mp3Bitrate = bitDepth_or_bitrate;
        m_impl->mp3PcmBuffer.clear();
    } else {
        CloseHandle(hFile);
        m_impl->fileHandle = INVALID_HANDLE_VALUE;
        return makeUnexpected(AudioError{"AudioWriteStream: unsupported format", 0});
    }

    return true;
}

Expected<bool, AudioError> AudioWriteStream::writeChunk(
    const float* pcmSamples,
    size_t sampleFrameCount
) {
    if (!m_impl || m_impl->fileHandle == INVALID_HANDLE_VALUE) {
        return makeUnexpected(AudioError{"Stream not open", 0});
    }
    if (!pcmSamples || sampleFrameCount == 0) return true;

    int ch = m_impl->channels;

    if (m_impl->isWav) {
        // Convert float PCM to target bit depth and write directly to disk
        int totalSamples   = static_cast<int>(sampleFrameCount * ch);
        int bytesPerSample = m_impl->bitDepth / 8;
        size_t chunkBytes  = static_cast<size_t>(totalSamples) * bytesPerSample;

        // Use stack buffer for small chunks, heap for large
        constexpr size_t STACK_LIMIT = 32768;
        uint8_t stackBuf[STACK_LIMIT];
        std::vector<uint8_t> heapBuf;
        uint8_t* outBuf;

        if (chunkBytes <= STACK_LIMIT) {
            outBuf = stackBuf;
        } else {
            heapBuf.resize(chunkBytes);
            outBuf = heapBuf.data();
        }

        uint8_t* ptr = outBuf;

        if (m_impl->bitDepth == 16) {
#if defined(__x86_64__) || defined(_M_X64)
            // SSE2 SIMD implementation (4 samples at once)
            int i = 0;
            // Seed 4 separate PRNG streams based on the single rngState
            __m128i rng = _mm_set_epi32(m_impl->rngState ^ 0x12345678, m_impl->rngState ^ 0x9ABCDEF0, 
                                        m_impl->rngState ^ 0x0FEDCBA9, m_impl->rngState ^ 0x87654321);
            __m128 scale32k = _mm_set1_ps(32768.0f);
            __m128 norm = _mm_set1_ps(1.0f / 65535.0f);
            __m128 offset = _mm_set1_ps(-0.5f);
            __m128i mask = _mm_set1_epi32(0xFFFF);
            
            for (; i <= totalSamples - 4; i += 4) {
                // TPDF requires 2 random numbers per sample
                // R1
                rng = _mm_xor_si128(rng, _mm_slli_epi32(rng, 13));
                rng = _mm_xor_si128(rng, _mm_srli_epi32(rng, 17));
                rng = _mm_xor_si128(rng, _mm_slli_epi32(rng, 5));
                __m128i r1_int = _mm_and_si128(rng, mask);
                __m128 r1_flt = _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(r1_int), norm), offset);
                
                // R2
                rng = _mm_xor_si128(rng, _mm_slli_epi32(rng, 13));
                rng = _mm_xor_si128(rng, _mm_srli_epi32(rng, 17));
                rng = _mm_xor_si128(rng, _mm_slli_epi32(rng, 5));
                __m128i r2_int = _mm_and_si128(rng, mask);
                __m128 r2_flt = _mm_add_ps(_mm_mul_ps(_mm_cvtepi32_ps(r2_int), norm), offset);
                
                __m128 tpdf = _mm_add_ps(r1_flt, r2_flt);
                
                // Load 4 floats
                __m128 floats = _mm_loadu_ps(&pcmSamples[i]);
                // Scale by 32768 and add TPDF dither
                __m128 scaled = _mm_add_ps(_mm_mul_ps(floats, scale32k), tpdf);
                // Convert to int32 (truncates/rounds depending on CSR)
                __m128i ints = _mm_cvtps_epi32(scaled);
                // Saturating pack to int16 (takes 2 __m128i, we just duplicate to fill)
                __m128i pcm16 = _mm_packs_epi32(ints, ints);
                // Store lower 64 bits (4 x 16-bit)
                _mm_storel_epi64((__m128i*)ptr, pcm16);
                ptr += 8;
            }
            
            // Save state back (just take one channel's state)
            m_impl->rngState = _mm_cvtsi128_si32(rng);
            
            // Scalar fallback for remaining samples
            for (; i < totalSamples; ++i) {
                float sample = pcmSamples[i];
                m_impl->rngState ^= m_impl->rngState << 13;
                m_impl->rngState ^= m_impl->rngState >> 17;
                m_impl->rngState ^= m_impl->rngState << 5;
                float r1 = (float)(m_impl->rngState & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;
                m_impl->rngState ^= m_impl->rngState << 13;
                m_impl->rngState ^= m_impl->rngState >> 17;
                m_impl->rngState ^= m_impl->rngState << 5;
                float r2 = (float)(m_impl->rngState & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;
                float val = sample * 32768.0f + (r1 + r2);

                short pcm16;
                if (val >= 32767.0f) pcm16 = 32767;
                else if (val <= -32768.0f) pcm16 = -32768;
                else pcm16 = static_cast<short>(val);

                memcpy(ptr, &pcm16, 2);
                ptr += 2;
            }
#else
            for (int i = 0; i < totalSamples; ++i) {
                float sample = pcmSamples[i];
                // TPDF dither
                m_impl->rngState ^= m_impl->rngState << 13;
                m_impl->rngState ^= m_impl->rngState >> 17;
                m_impl->rngState ^= m_impl->rngState << 5;
                float r1 = (float)(m_impl->rngState & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;
                m_impl->rngState ^= m_impl->rngState << 13;
                m_impl->rngState ^= m_impl->rngState >> 17;
                m_impl->rngState ^= m_impl->rngState << 5;
                float r2 = (float)(m_impl->rngState & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;
                float val = sample * 32768.0f + (r1 + r2);

                short pcm16;
                if (val >= 32767.0f) pcm16 = 32767;
                else if (val <= -32768.0f) pcm16 = -32768;
                else pcm16 = static_cast<short>(val);

                memcpy(ptr, &pcm16, 2);
                ptr += 2;
            }
#endif
        } else if (m_impl->bitDepth == 24) {
            for (int i = 0; i < totalSamples; ++i) {
                float sample = pcmSamples[i];
                m_impl->rngState ^= m_impl->rngState << 13;
                m_impl->rngState ^= m_impl->rngState >> 17;
                m_impl->rngState ^= m_impl->rngState << 5;
                float r1 = (float)(m_impl->rngState & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;
                m_impl->rngState ^= m_impl->rngState << 13;
                m_impl->rngState ^= m_impl->rngState >> 17;
                m_impl->rngState ^= m_impl->rngState << 5;
                float r2 = (float)(m_impl->rngState & 0xFFFF) * (1.0f / 65535.0f) - 0.5f;
                float val = sample * 8388608.0f + (r1 + r2);

                int pcm24;
                if (val >= 8388607.0f) pcm24 = 8388607;
                else if (val <= -8388608.0f) pcm24 = -8388608;
                else pcm24 = static_cast<int>(val);

                ptr[0] = static_cast<uint8_t>(pcm24 & 0xFF);
                ptr[1] = static_cast<uint8_t>((pcm24 >> 8) & 0xFF);
                ptr[2] = static_cast<uint8_t>((pcm24 >> 16) & 0xFF);
                ptr += 3;
            }
        } else { // 32-bit float: direct copy
            memcpy(ptr, pcmSamples, totalSamples * sizeof(float));
        }

        DWORD bw;
        if (!WriteFile(m_impl->fileHandle, outBuf, static_cast<DWORD>(chunkBytes), &bw, nullptr)) {
            return makeUnexpected(AudioError{"Failed to write WAV chunk", static_cast<int>(GetLastError())});
        }
        m_impl->totalSamplesWritten += static_cast<int64_t>(sampleFrameCount);

    } else {
        // MP3: accumulate interleaved PCM (encode all at once in close())
        size_t totalFloats = sampleFrameCount * ch;
        m_impl->mp3PcmBuffer.insert(
            m_impl->mp3PcmBuffer.end(),
            pcmSamples, pcmSamples + totalFloats
        );
        m_impl->totalSamplesWritten += static_cast<int64_t>(sampleFrameCount);
    }

    return true;
}

Expected<bool, AudioError> AudioWriteStream::close() {
    if (!m_impl || m_impl->fileHandle == INVALID_HANDLE_VALUE) {
        return makeUnexpected(AudioError{"Stream not open", 0});
    }

    DWORD bw;

    if (m_impl->isWav) {
        // Patch RIFF header sizes
        int bytesPerSample = m_impl->bitDepth / 8;
        int dataSize = static_cast<int>(
            m_impl->totalSamplesWritten * m_impl->channels * bytesPerSample
        );
        int riffSize = 36 + dataSize;

        LARGE_INTEGER pos;
        pos.QuadPart = 4;
        SetFilePointerEx(m_impl->fileHandle, pos, nullptr, FILE_BEGIN);
        WriteFile(m_impl->fileHandle, &riffSize, 4, &bw, nullptr);

        pos.QuadPart = 40;
        SetFilePointerEx(m_impl->fileHandle, pos, nullptr, FILE_BEGIN);
        WriteFile(m_impl->fileHandle, &dataSize, 4, &bw, nullptr);

    } else if (!m_impl->mp3PcmBuffer.empty()) {
        // Encode all accumulated PCM via Mp3Codec (compiled in its own TU)
        mp3::Mp3Codec codec;
        auto result = codec.encode(
            m_impl->mp3PcmBuffer.data(),
            m_impl->totalSamplesWritten,
            m_impl->sampleRate,
            m_impl->channels,
            m_impl->mp3Bitrate
        );
        if (!result) {
            CloseHandle(m_impl->fileHandle);
            m_impl->fileHandle = INVALID_HANDLE_VALUE;
            m_impl->mp3PcmBuffer.clear();
            return makeUnexpected(result.error());
        }
        const auto& mp3Data = result.value();
        WriteFile(m_impl->fileHandle, mp3Data.data(),
                  static_cast<DWORD>(mp3Data.size()), &bw, nullptr);
        m_impl->mp3PcmBuffer.clear();
    }

    CloseHandle(m_impl->fileHandle);
    m_impl->fileHandle = INVALID_HANDLE_VALUE;
    return true;
}

} // namespace audio_io
