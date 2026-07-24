// riff_metadata_reader.h - RIFF/WAV Metadata Reader
// Part of audio-io-1.0.0
// Windows-native, C++20
// 
// Reads metadata from WAV files (RIFF INFO chunks, BWF metadata)
// Supports: INAM (Title), IART (Artist), IPRD (Album), IGNR (Genre), etc.

#pragma once

#include "audio_io/audio_metadata.h"
#include <cstdint>
#include <optional>

namespace audio_io {
namespace metadata {

class RiffMetadataReader {
public:
    // Read RIFF metadata from file
    static std::optional<AudioMetadata> readFromFile(const wchar_t* filePathUtf16);
    static std::optional<AudioMetadata> readFromFile(const std::wstring& filePath) {
        return readFromFile(filePath.c_str());
    }
    
    // Read RIFF metadata from memory buffer
    static std::optional<AudioMetadata> readFromMemory(const uint8_t* data, size_t dataSize);
    
private:
    // RIFF chunk header (8 bytes)
    struct ChunkHeader {
        char id[4];      // Chunk ID (e.g., "LIST", "INFO", "data")
        uint32_t size;   // Chunk size (little-endian)
        
        bool isValid() const;
    };
    
    // Parse RIFF structure
    static bool parseRiffHeader(const uint8_t* data, size_t dataSize, size_t& offset);
    static bool findChunk(const uint8_t* data, size_t dataSize, const char* chunkId, 
                         size_t& chunkOffset, uint32_t& chunkSize);
    
    // Parse INFO list chunk
    static void parseInfoListChunk(const uint8_t* data, size_t dataSize, 
                                   size_t offset, uint32_t size, AudioMetadata& metadata);
    
    // Parse individual INFO items
    static std::string parseInfoString(const uint8_t* data, uint32_t size);
    
    // Known INFO chunk IDs
    static constexpr const char* INFO_TITLE   = "INAM";  // Title/Name
    static constexpr const char* INFO_ARTIST  = "IART";  // Artist
    static constexpr const char* INFO_ALBUM   = "IPRD";  // Product (Album)
    static constexpr const char* INFO_GENRE   = "IGNR";  // Genre
    static constexpr const char* INFO_COMMENT = "ICMT";  // Comment
    static constexpr const char* INFO_DATE    = "ICRD";  // Creation date
    static constexpr const char* INFO_TRACK   = "ITRK";  // Track number
};

} // namespace metadata
} // namespace audio_io
