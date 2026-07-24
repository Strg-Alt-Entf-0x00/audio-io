// id3v2_reader.h - ID3v2 Tag Reader
// Part of audio-io-1.0.0
// Windows-native, C++20
// 
// Reads ID3v2.3 and ID3v2.4 tags from MP3 files
// Supports: TIT2 (Title), TPE1 (Artist), TALB (Album), TCON (Genre), APIC (Cover Art)

#pragma once

#include "audio_io/audio_metadata.h"
#include <cstdint>
#include <optional>

namespace audio_io {
namespace metadata {

class Id3v2Reader {
public:
    // Read ID3v2 tags from file
    static std::optional<AudioMetadata> readFromFile(const wchar_t* filePathUtf16);
    static std::optional<AudioMetadata> readFromFile(const std::wstring& filePath) {
        return readFromFile(filePath.c_str());
    }
    
    // Read ID3v2 tags from memory buffer
    static std::optional<AudioMetadata> readFromMemory(const uint8_t* data, size_t dataSize);
    
private:
    // ID3v2 header structure (10 bytes)
    struct Id3v2Header {
        uint8_t identifier[3];  // "ID3"
        uint8_t version[2];     // Major, Minor
        uint8_t flags;
        uint8_t sizeBytes[4];   // Synchsafe integer
        
        bool isValid() const;
        uint32_t getTagSize() const;
        int getMajorVersion() const { return version[0]; }
    };
    
    // ID3v2 frame header (v2.3: 10 bytes, v2.4: 10 bytes)
    struct FrameHeader {
        char frameId[5];      // 4 bytes + null terminator
        uint32_t size;
        uint16_t flags;
        
        bool isValid() const;
    };
    
    // Parse synchsafe integer (ID3v2 specific encoding)
    static uint32_t parseSynchsafeInt(const uint8_t* bytes, int count);
    
    // Parse frame data
    static std::string parseTextFrame(const uint8_t* frameData, uint32_t frameSize);
    static bool parseCoverArtFrame(const uint8_t* frameData, uint32_t frameSize, 
                                   std::vector<uint8_t>& imageData, std::string& mimeType);
    
    // Text encoding handling
    enum class TextEncoding : uint8_t {
        ISO_8859_1 = 0,
        UTF16_BOM = 1,
        UTF16_BE = 2,
        UTF8 = 3
    };
    
    static std::string decodeText(const uint8_t* data, size_t size, TextEncoding encoding);
};

} // namespace metadata
} // namespace audio_io
