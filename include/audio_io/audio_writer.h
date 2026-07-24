// audio_writer.h - Audio file writing and encoding API
// Part of audio-io-1.0.0
// Windows-native, C++20

#pragma once

#include "audio_format.h"
#include "audio_metadata.h"
#include "audio_reader.h"
#include "expected.h"
#include "export.h"
#include <vector>
#include <cstdint>

namespace audio_io {

// Audio file writer class
class AUDIO_IO_API AudioWriter {
public:
    AudioWriter() = default;
    ~AudioWriter() = default;
    
    // Delete copy, allow move
    AudioWriter(const AudioWriter&) = delete;
    AudioWriter& operator=(const AudioWriter&) = delete;
    AudioWriter(AudioWriter&&) noexcept = default;
    AudioWriter& operator=(AudioWriter&&) noexcept = default;
    
    // MP3 ENCODING
    // Encode PCM samples to MP3 format
    // Returns MP3 file bytes ready to write to disk.
    // If metadata is provided, an ID3v2.3 tag block is prepended automatically.
    //
    // Parameters:
    //   pcmSamples: Float samples normalized to [-1.0, 1.0], interleaved (L,R,L,R for stereo)
    //   sampleFrameCount: Number of sample frames (stereo frame = 2 samples)
    //   sampleRateHz: Sample rate (32000, 44100, or 48000 Hz)
    //   channelCount: 1 (mono) or 2 (stereo)
    //   bitrateKbps: Target bitrate (128, 192, 256, or 320 kbps recommended)
    //   metadata: Optional tags to embed (title, artist, album, genre, comment, year, track)
    Expected<std::vector<uint8_t>, AudioError> encodeMp3(
        const float* pcmSamples,
        int64_t sampleFrameCount,
        int sampleRateHz,
        int channelCount,
        int bitrateKbps = 320,
        const AudioMetadata* metadata = nullptr
    );

    // Convenience: encode from AudioData
    Expected<std::vector<uint8_t>, AudioError> encodeMp3(
        const AudioData& audioData,
        int bitrateKbps = 320,
        const AudioMetadata* metadata = nullptr
    );

    // WAV ENCODING
    // Encode PCM samples to WAV format.
    // Returns WAV file bytes ready to write to disk.
    // If metadata is provided, a RIFF INFO LIST chunk is appended and the
    // RIFF header size is updated automatically.
    //
    // bitDepth: 16 (default, standard PCM) or 32 (IEEE float)
    // metadata: Optional tags to embed (INAM, IART, IPRD, IGNR, ICMT, ICRD, ITRK)
    Expected<std::vector<uint8_t>, AudioError> encodeWav(
        const float* pcmSamples,
        int64_t sampleFrameCount,
        int sampleRateHz,
        int channelCount,
        int bitDepth = 16,
        const AudioMetadata* metadata = nullptr
    );

    // Convenience: encode from AudioData
    Expected<std::vector<uint8_t>, AudioError> encodeWav(
        const AudioData& audioData,
        int bitDepth = 16,
        const AudioMetadata* metadata = nullptr
    );

    // FILE WRITING
    // Write audio data to file. Format is determined by file extension (.wav / .mp3).
    // If metadata is provided it is embedded natively in the output file.
    // quality: For MP3 = bitrate in kbps; For WAV = bit depth (16 or 32)
    Expected<bool, AudioError> writeFile(
        const wchar_t* filePathUtf16,
        const AudioData& audioData,
        int quality = 192,
        const AudioMetadata* metadata = nullptr
    );

    Expected<bool, AudioError> writeFile(
        const std::wstring& filePath,
        const AudioData& audioData,
        int quality = 192,
        const AudioMetadata* metadata = nullptr
    ) {
        return writeFile(filePath.c_str(), audioData, quality, metadata);
    }
    Expected<bool, AudioError> writeToFile(
        const std::wstring& filePath,
        const std::vector<uint8_t>& fileData
    );
};

// ============================================================================
// STREAMING WRITER
// ============================================================================

// Stream audio directly to disk in chunks to save memory
class AUDIO_IO_API AudioWriteStream {
public:
    AudioWriteStream();
    ~AudioWriteStream();
    
    // Delete copy, allow move
    AudioWriteStream(const AudioWriteStream&) = delete;
    AudioWriteStream& operator=(const AudioWriteStream&) = delete;
    AudioWriteStream(AudioWriteStream&&) noexcept;
    AudioWriteStream& operator=(AudioWriteStream&&) noexcept;
    
    // Open a new streaming session
    // format: Must be Wav or Mp3
    // bitDepth_or_bitrate:
    //   - For WAV: 16, 24, or 32
    //   - For MP3: Target bitrate in kbps (e.g. 128, 192, 256, 320)
    Expected<bool, AudioError> open(
        const std::wstring& filePath,
        FileFormat format,
        int sampleRateHz,
        int channelCount,
        int bitDepth_or_bitrate,
        const AudioMetadata* metadata = nullptr
    );
    
    // Write a chunk of PCM samples
    Expected<bool, AudioError> writeChunk(const float* pcmSamples, size_t sampleFrameCount);
    
    // Finalize the file (write tail headers, patch RIFF sizes, close handle)
    Expected<bool, AudioError> close();
    
    // Check if stream is currently open
    bool isOpen() const;

private:
    struct Impl;
    Impl* m_impl;
};

} // namespace audio_io
