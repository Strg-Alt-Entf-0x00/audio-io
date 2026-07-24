// audio_stream.h - Streaming audio decoder for large files
// Part of audio-io-1.0.0
// Windows-native, C++20
//
// Supports streaming decode of large audio files (> 1GB)
// Minimal RAM usage through chunk-based decoding
// Optimized for real-time playback and seeking

#pragma once

#include "audio_format.h"
#include "audio_reader.h"
#include "expected.h"
#include "export.h"
#include <memory>
#include <cstdint>

namespace audio_io {

// Streaming audio decoder configuration
struct AudioStreamConfig {
    size_t chunkSizeFrames;  // Number of sample frames per chunk (default: 4096)
    size_t bufferChunks;     // Number of chunks to buffer ahead (default: 3)
    int targetSampleRateHz;  // Target sample rate for on-the-fly resampling (0 = original rate)
    
    AudioStreamConfig() 
        : chunkSizeFrames(4096)
        , bufferChunks(3)
        , targetSampleRateHz(0)
    {}
};

// Streaming audio decoder class
// Decodes audio on-demand in small chunks
// Ideal for playback, transcoding, or processing large files
class AUDIO_IO_API AudioStream {
public:
    AudioStream();
    ~AudioStream();
    
    // Delete copy, allow move
    AudioStream(const AudioStream&) = delete;
    AudioStream& operator=(const AudioStream&) = delete;
    AudioStream(AudioStream&&) noexcept;
    AudioStream& operator=(AudioStream&&) noexcept;
    
    // Open audio file for streaming
    // Does NOT decode entire file - only reads header/format
    Expected<bool, AudioError> open(const wchar_t* filePathUtf16, 
                                    const AudioStreamConfig& config = AudioStreamConfig{});
    Expected<bool, AudioError> open(const std::wstring& filePath,
                                    const AudioStreamConfig& config = AudioStreamConfig{}) {
        return open(filePath.c_str(), config);
    }
    
    // Read next chunk of audio data
    // Returns samples as interleaved float [-1.0, 1.0]
    // Returns empty vector at end of stream
    Expected<std::vector<float>, AudioError> readChunk();
    
    // Seek to specific sample frame position
    // Note: Seeking in MP3 is approximate due to frame boundaries
    Expected<bool, AudioError> seek(int64_t sampleFrame);
    
    // Get current position in sample frames
    int64_t getCurrentPosition() const;
    
    // Get audio format (channels, sample rate, etc.)
    const AudioFormat& getFormat() const { return format_; }
    
    // Check if stream is open
    bool isOpen() const { return isOpen_; }
    
    // Check if end of stream reached
    bool isEndOfStream() const { return isEof_; }
    
    // Close stream and free resources
    void close();
    
private:
    struct StreamImpl;  // Forward declaration for PIMPL pattern
    std::unique_ptr<StreamImpl> impl_;
    
    AudioFormat format_;
    AudioStreamConfig config_;
    bool isOpen_ = false;
    bool isEof_ = false;
    int64_t currentPosition_ = 0;
};

} // namespace audio_io
