// audio_stream.cpp - Streaming audio decoder implementation
// Part of audio-io-1.0.0

#include "audio_io/audio_stream.h"
#include "wave/wave_decoder.h"
#include "mp3/mp3_codec.h"
#include "resampler/sinc_resampler.h"
#include "io/file_reader.h"
#define MPEG_FLOAT_OUTPUT
#include "mp3/mpeg_layer3_decoder_native.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cwctype>
#include <windows.h>
#include <vector>
#include <algorithm>


namespace audio_io {

// ============================================================================
// Internal WAV streaming implementation
// ============================================================================

struct WavStreamData {
    AudioFormat format;
    int64_t dataOffset;
    int64_t dataSize;
    int64_t currentPosition;
    HANDLE fileHandle;
    
    WavStreamData() : dataOffset(0), dataSize(0), currentPosition(0), fileHandle(INVALID_HANDLE_VALUE) {}
};

struct Mp3StreamData {
    AudioFormat format;
    int64_t currentPosition;
    HANDLE fileHandle;
    MpegDecoderContext decoder;
    std::vector<uint8_t> readBuffer;
    size_t bufferPos;
    size_t bufferLength;
    std::vector<float> overflowPcm;
    
    Mp3StreamData() : currentPosition(0), fileHandle(INVALID_HANDLE_VALUE), bufferPos(0), bufferLength(0) {
        mpeg_decoder_initialize(&decoder);
        readBuffer.resize(16384);
    }
};

// ============================================================================
// StreamImpl - internal implementation
// ============================================================================

struct AudioStream::StreamImpl {
    FileFormat fileFormat;
    WavStreamData wavData;
    Mp3StreamData mp3Data;
    
    StreamImpl() : fileFormat(FileFormat::Unknown) {}
    
    Expected<std::vector<float>, AudioError> readChunkWav(size_t chunkSizeFrames);
    Expected<bool, AudioError> seekWav(int64_t sampleFrame);
    void closeWav();

    Expected<std::vector<float>, AudioError> readChunkMp3(size_t chunkSizeFrames);
    Expected<bool, AudioError> seekMp3(int64_t sampleFrame);
    void closeMp3();
};
// ============================================================================
// WAV Streaming Methods
// ============================================================================

Expected<std::vector<float>, AudioError> AudioStream::StreamImpl::readChunkWav(size_t chunkSizeFrames) {
    if (wavData.fileHandle == INVALID_HANDLE_VALUE) {
        return makeUnexpected(AudioError{"Stream not open", 0});
    }
    
    const AudioFormat& format = wavData.format;
    
    // Calculate bytes to read
    int bytesPerSample = format.bitDepth / 8;
    int bytesPerFrame = bytesPerSample * format.channelCount;
    size_t bytesToRead = chunkSizeFrames * bytesPerFrame;
    
    // Check if we're at end of data
    int64_t bytesRemaining = wavData.dataSize - (wavData.currentPosition * bytesPerFrame);
    if (bytesRemaining <= 0) {
        return std::vector<float>{};  // End of stream
    }
    
    bytesToRead = std::min<size_t>(bytesToRead, static_cast<size_t>(bytesRemaining));
    
    // Read PCM data
    std::vector<uint8_t> pcmData(bytesToRead);
    DWORD bytesRead = 0;
    
    if (!ReadFile(wavData.fileHandle, pcmData.data(), static_cast<DWORD>(bytesToRead), &bytesRead, nullptr)) {
        return makeUnexpected(AudioError{"Failed to read audio data", static_cast<int>(GetLastError())});
    }
    
    if (bytesRead == 0) {
        return std::vector<float>{};  // End of stream
    }
    
    // Convert PCM to float
    size_t samplesRead = bytesRead / bytesPerSample;
    std::vector<float> floatSamples(samplesRead);
    
    if (format.sampleFormat == SampleFormat::Int16) {
        const int16_t* pcm16 = reinterpret_cast<const int16_t*>(pcmData.data());
        for (size_t i = 0; i < samplesRead; ++i) {
            floatSamples[i] = static_cast<float>(pcm16[i]) / 32768.0f;
        }
    } else if (format.sampleFormat == SampleFormat::Float32) {
        std::memcpy(floatSamples.data(), pcmData.data(), bytesRead);
    } else {
        return makeUnexpected(AudioError{"Unsupported sample format for streaming", 0});
    }
    
    wavData.currentPosition += samplesRead / format.channelCount;
    return floatSamples;
}

Expected<bool, AudioError> AudioStream::StreamImpl::seekWav(int64_t sampleFrame) {
    if (wavData.fileHandle == INVALID_HANDLE_VALUE) {
        return makeUnexpected(AudioError{"Stream not open", 0});
    }
    
    const AudioFormat& format = wavData.format;
    int bytesPerFrame = (format.bitDepth / 8) * format.channelCount;
    int64_t byteOffset = wavData.dataOffset + (sampleFrame * bytesPerFrame);
    
    LARGE_INTEGER offset;
    offset.QuadPart = byteOffset;
    
    if (!SetFilePointerEx(wavData.fileHandle, offset, nullptr, FILE_BEGIN)) {
        return makeUnexpected(AudioError{"Seek failed", static_cast<int>(GetLastError())});
    }
    
    wavData.currentPosition = sampleFrame;
    return true;
}

void AudioStream::StreamImpl::closeWav() {
    if (wavData.fileHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(wavData.fileHandle);
        wavData.fileHandle = INVALID_HANDLE_VALUE;
    }
}

// ============================================================================
// MP3 Streaming Methods
// ============================================================================

Expected<std::vector<float>, AudioError> AudioStream::StreamImpl::readChunkMp3(size_t chunkSizeFrames) {
    if (mp3Data.fileHandle == INVALID_HANDLE_VALUE) {
        return makeUnexpected(AudioError{"Stream not open", 0});
    }

    size_t targetSamples = chunkSizeFrames * mp3Data.format.channelCount;
    std::vector<float> outPcm;
    outPcm.reserve(targetSamples);

    if (!mp3Data.overflowPcm.empty()) {
        size_t toCopy = mp3Data.overflowPcm.size() < targetSamples ? mp3Data.overflowPcm.size() : targetSamples;
        outPcm.insert(outPcm.end(), mp3Data.overflowPcm.begin(), mp3Data.overflowPcm.begin() + toCopy);
        mp3Data.overflowPcm.erase(mp3Data.overflowPcm.begin(), mp3Data.overflowPcm.begin() + toCopy);
    }

    float framePcm[MAX_MPEG_SAMPLES_PER_FRAME];
    MpegFrameInfo info;

    while (outPcm.size() < targetSamples) {
        // Refill buffer if empty
        if (mp3Data.bufferPos >= mp3Data.bufferLength) {
            mp3Data.bufferPos = 0;
            DWORD bytesRead = 0;
            if (!ReadFile(mp3Data.fileHandle, mp3Data.readBuffer.data(), static_cast<DWORD>(mp3Data.readBuffer.size()), &bytesRead, nullptr)) {
                return makeUnexpected(AudioError{"Failed to read MP3 data", static_cast<int>(GetLastError())});
            }
            mp3Data.bufferLength = bytesRead;
            if (bytesRead == 0) break; // EOF
        }

        int decodedSamples = mpeg_decode_frame(
            &mp3Data.decoder,
            mp3Data.readBuffer.data() + mp3Data.bufferPos,
            static_cast<int>(mp3Data.bufferLength - mp3Data.bufferPos),
            framePcm,
            &info
        );

        if (info.frame_bytes > 0) {
            mp3Data.bufferPos += info.frame_bytes;
        } else {
            // Need more data for a full frame, shift remaining to start and refill
            size_t remaining = mp3Data.bufferLength - mp3Data.bufferPos;
            if (remaining > 0) {
                std::memmove(mp3Data.readBuffer.data(), mp3Data.readBuffer.data() + mp3Data.bufferPos, remaining);
            }
            mp3Data.bufferPos = 0;
            DWORD bytesRead = 0;
            if (!ReadFile(mp3Data.fileHandle, mp3Data.readBuffer.data() + remaining, static_cast<DWORD>(mp3Data.readBuffer.size() - remaining), &bytesRead, nullptr)) {
                return makeUnexpected(AudioError{"Failed to read MP3 data", static_cast<int>(GetLastError())});
            }
            mp3Data.bufferLength = remaining + bytesRead;
            if (bytesRead == 0) break; // EOF
            continue;
        }

        if (decodedSamples > 0) {
            size_t needed = targetSamples - outPcm.size();
            size_t decodedCount = static_cast<size_t>(decodedSamples);
            if (decodedCount <= needed) {
                outPcm.insert(outPcm.end(), framePcm, framePcm + decodedCount);
            } else {
                outPcm.insert(outPcm.end(), framePcm, framePcm + needed);
                mp3Data.overflowPcm.assign(framePcm + needed, framePcm + decodedCount);
            }
        }
    }

    mp3Data.currentPosition += outPcm.size() / mp3Data.format.channelCount;
    return outPcm;
}

Expected<bool, AudioError> AudioStream::StreamImpl::seekMp3(int64_t sampleFrame) {
    if (mp3Data.fileHandle == INVALID_HANDLE_VALUE) {
        return makeUnexpected(AudioError{"Stream not open", 0});
    }

    if (sampleFrame < mp3Data.currentPosition) {
        // Seek backwards requires reopening/resetting
        LARGE_INTEGER offset;
        offset.QuadPart = 0;
        SetFilePointerEx(mp3Data.fileHandle, offset, nullptr, FILE_BEGIN);
        mpeg_decoder_initialize(&mp3Data.decoder);
        mp3Data.bufferPos = 0;
        mp3Data.bufferLength = 0;
        mp3Data.currentPosition = 0;
        mp3Data.overflowPcm.clear();
    }

    // Seek forward by decoding and discarding
    int64_t framesToSkip = sampleFrame - mp3Data.currentPosition;
    while (framesToSkip > 0) {
        size_t chunk = static_cast<size_t>(framesToSkip < 4096 ? framesToSkip : 4096);
        auto result = readChunkMp3(chunk);
        if (!result) return makeUnexpected(result.error());
        if (result->empty()) break; // EOF
        framesToSkip -= (result->size() / mp3Data.format.channelCount);
    }

    return true;
}

void AudioStream::StreamImpl::closeMp3() {
    if (mp3Data.fileHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(mp3Data.fileHandle);
        mp3Data.fileHandle = INVALID_HANDLE_VALUE;
    }
}

// ============================================================================
// AudioStream Public API
// ============================================================================

AudioStream::AudioStream() = default;

AudioStream::~AudioStream() {
    close();
}

AudioStream::AudioStream(AudioStream&& other) noexcept
    : impl_(std::move(other.impl_))
    , format_(std::move(other.format_))
    , config_(other.config_)
    , isOpen_(other.isOpen_)
    , isEof_(other.isEof_)
    , currentPosition_(other.currentPosition_)
{
    other.isOpen_ = false;
    other.isEof_ = false;
    other.currentPosition_ = 0;
}

AudioStream& AudioStream::operator=(AudioStream&& other) noexcept {
    if (this != &other) {
        close();
        impl_ = std::move(other.impl_);
        format_ = std::move(other.format_);
        config_ = other.config_;
        isOpen_ = other.isOpen_;
        isEof_ = other.isEof_;
        currentPosition_ = other.currentPosition_;
        
        other.isOpen_ = false;
        other.isEof_ = false;
        other.currentPosition_ = 0;
    }
    return *this;
}

Expected<bool, AudioError> AudioStream::open(const wchar_t* filePathUtf16, const AudioStreamConfig& config) {
    // Close any existing stream
    close();
    
    config_ = config;
    
    // Detect file format from extension
    std::wstring path(filePathUtf16);
    size_t dotPos = path.find_last_of(L'.');
    if (dotPos == std::wstring::npos) {
        return makeUnexpected(AudioError{"Unknown file format (no extension)", 0});
    }
    
    std::wstring ext = path.substr(dotPos + 1);
    for (auto& c : ext) c = std::towlower(c);
    
    // Currently only WAV streaming is supported
    if (ext == L"wav" || ext == L"wave") {
        // Read WAV format first
        wave::WaveDecoder decoder;
        auto formatResult = decoder.readFormat(filePathUtf16);
        
        if (!formatResult) {
            return makeUnexpected(formatResult.error());
        }
        
        format_ = *formatResult;
        
        // Create impl
        impl_ = std::make_unique<StreamImpl>();
        impl_->fileFormat = FileFormat::Wave;
        impl_->wavData.format = format_;
        
        auto handleResult = io::FileReader::openForStreaming(filePathUtf16);
        if (!handleResult) {
            return makeUnexpected(handleResult.error());
        }
        impl_->wavData.fileHandle = *handleResult;
        
        // Read a small header to find exact data offset using findChunk
        impl_->wavData.dataOffset = 44; // Default fallback
        uint8_t headerBuf[4096];
        DWORD bytesRead = 0;
        if (ReadFile(impl_->wavData.fileHandle, headerBuf, sizeof(headerBuf), &bytesRead, nullptr)) {
            size_t dataOffset, dataSize;
            if (wave::WaveDecoder::findChunk(headerBuf, bytesRead, "data", &dataOffset, &dataSize)) {
                impl_->wavData.dataOffset = dataOffset;
                impl_->wavData.dataSize = dataSize;
            }
        }
        impl_->wavData.currentPosition = 0;
        
        // Seek to start of audio data
        LARGE_INTEGER offset;
        offset.QuadPart = impl_->wavData.dataOffset;
        SetFilePointerEx(impl_->wavData.fileHandle, offset, nullptr, FILE_BEGIN);
        
        isOpen_ = true;
        isEof_ = false;
        currentPosition_ = 0;
        
        return true;
    } else if (ext == L"mp3") {
        mp3::Mp3Codec mp3Decoder;
        auto formatResult = mp3Decoder.readFormat(filePathUtf16);
        if (!formatResult) {
            return makeUnexpected(formatResult.error());
        }
        
        format_ = *formatResult;
        
        impl_ = std::make_unique<StreamImpl>();
        impl_->fileFormat = FileFormat::Mp3;
        impl_->mp3Data.format = format_;
        
        auto handleResult = io::FileReader::openForStreaming(filePathUtf16);
        if (!handleResult) {
            return makeUnexpected(handleResult.error());
        }
        impl_->mp3Data.fileHandle = *handleResult;
        
        isOpen_ = true;
        isEof_ = false;
        currentPosition_ = 0;
        
        return true;
    }
    
    return makeUnexpected(AudioError{"Unsupported file format for streaming", 0});
}



Expected<std::vector<float>, AudioError> AudioStream::readChunk() {
    if (!isOpen_ || !impl_) {
        return makeUnexpected(AudioError{"Stream not open", 0});
    }
    
    if (isEof_) {
        return std::vector<float>{};
    }
    
    auto result = (impl_->fileFormat == FileFormat::Wave) 
        ? impl_->readChunkWav(config_.chunkSizeFrames)
        : impl_->readChunkMp3(config_.chunkSizeFrames);
    
    if (result && result->empty()) {
        isEof_ = true;
    }
    
    if (result && impl_->fileFormat == FileFormat::Wave) {
        currentPosition_ = impl_->wavData.currentPosition;
    } else if (result && impl_->fileFormat == FileFormat::Mp3) {
        currentPosition_ = impl_->mp3Data.currentPosition;
    }
    
    // Resample if requested
    if (result && !result->empty() && config_.targetSampleRateHz > 0 && config_.targetSampleRateHz != format_.sampleRateHz) {
        std::vector<float>& pcm = *result;
        int srcRate = format_.sampleRateHz;
        int dstRate = config_.targetSampleRateHz;
        
        if (format_.channelCount == 1) {
            *result = resampler::resample_mono(pcm.data(), pcm.size(), srcRate, dstRate);
        } else if (format_.channelCount == 2) {
            // Deinterleave, resample, interleave
            size_t frames = pcm.size() / 2;
            std::vector<float> left(frames), right(frames);
            for (size_t i = 0; i < frames; ++i) {
                left[i] = pcm[i * 2];
                right[i] = pcm[i * 2 + 1];
            }
            auto resLeft = resampler::resample_mono(left.data(), frames, srcRate, dstRate);
            auto resRight = resampler::resample_mono(right.data(), frames, srcRate, dstRate);
            
            std::vector<float> stereo(resLeft.size() * 2);
            for (size_t i = 0; i < resLeft.size(); ++i) {
                stereo[i * 2] = resLeft[i];
                stereo[i * 2 + 1] = resRight[i];
            }
            *result = std::move(stereo);
        }
    }
    
    return result;
}

Expected<bool, AudioError> AudioStream::seek(int64_t sampleFrame) {
    if (!isOpen_ || !impl_) {
        return makeUnexpected(AudioError{"Stream not open", 0});
    }
    
    auto result = (impl_->fileFormat == FileFormat::Wave) 
        ? impl_->seekWav(sampleFrame)
        : impl_->seekMp3(sampleFrame);
    
    if (result && *result) {
        currentPosition_ = sampleFrame;
        isEof_ = false;
    }
    
    return result;
}

int64_t AudioStream::getCurrentPosition() const {
    return currentPosition_;
}

void AudioStream::close() {
    if (impl_) {
        if (impl_->fileFormat == FileFormat::Wave) {
            impl_->closeWav();
        } else if (impl_->fileFormat == FileFormat::Mp3) {
            impl_->closeMp3();
        }
        impl_.reset();
    }
    isOpen_ = false;
    isEof_ = false;
    currentPosition_ = 0;
}

} // namespace audio_io
