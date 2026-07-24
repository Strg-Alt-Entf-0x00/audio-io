// id3v2_reader.cpp - ID3v2 Tag Reader Implementation
// Part of audio-io-1.0.0

#include "id3v2_reader.h"
#include <windows.h>
#include <cstring>

namespace audio_io {
namespace metadata {

// ID3v2 constants
constexpr uint32_t ID3V2_HEADER_SIZE = 10;
constexpr uint32_t ID3V2_FRAME_HEADER_SIZE = 10;

// ===================================================================
// ID3v2Header Implementation
// ===================================================================

bool Id3v2Reader::Id3v2Header::isValid() const {
    return identifier[0] == 'I' && 
           identifier[1] == 'D' && 
           identifier[2] == '3' &&
           version[0] < 0xFF &&
           version[1] < 0xFF &&
           sizeBytes[0] < 0x80 &&
           sizeBytes[1] < 0x80 &&
           sizeBytes[2] < 0x80 &&
           sizeBytes[3] < 0x80;
}

uint32_t Id3v2Reader::Id3v2Header::getTagSize() const {
    return parseSynchsafeInt(sizeBytes, 4);
}

// ===================================================================
// FrameHeader Implementation
// ===================================================================

bool Id3v2Reader::FrameHeader::isValid() const {
    // Frame ID must be 4 alphanumeric characters (A-Z, 0-9)
    for (int i = 0; i < 4; ++i) {
        char c = frameId[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    return size > 0;
}

// ===================================================================
// Synchsafe Integer Parsing
// ===================================================================

uint32_t Id3v2Reader::parseSynchsafeInt(const uint8_t* bytes, int count) {
    uint32_t result = 0;
    for (int i = 0; i < count; ++i) {
        result = (result << 7) | (bytes[i] & 0x7F);
    }
    return result;
}

// ===================================================================
// Text Decoding
// ===================================================================

std::string Id3v2Reader::decodeText(const uint8_t* data, size_t size, TextEncoding encoding) {
    if (size == 0) return "";
    
    switch (encoding) {
        case TextEncoding::ISO_8859_1: {
            // Latin-1: direct conversion
            return std::string(reinterpret_cast<const char*>(data), size);
        }
        
        case TextEncoding::UTF8: {
            return std::string(reinterpret_cast<const char*>(data), size);
        }
        
        case TextEncoding::UTF16_BOM:
        case TextEncoding::UTF16_BE: {
            // UTF-16 conversion using Windows API
            if (size < 2) return "";
            
            // Determine byte order
            bool isBigEndian = (encoding == TextEncoding::UTF16_BE);
            if (encoding == TextEncoding::UTF16_BOM && size >= 2) {
                if (data[0] == 0xFE && data[1] == 0xFF) {
                    isBigEndian = true;
                    data += 2;
                    size -= 2;
                } else if (data[0] == 0xFF && data[1] == 0xFE) {
                    isBigEndian = false;
                    data += 2;
                    size -= 2;
                }
            }
            
            // Convert to wstring
            std::wstring wideStr;
            for (size_t i = 0; i + 1 < size; i += 2) {
                wchar_t wc;
                if (isBigEndian) {
                    wc = (data[i] << 8) | data[i + 1];
                } else {
                    wc = (data[i + 1] << 8) | data[i];
                }
                if (wc == 0) break; // Null terminator
                wideStr += wc;
            }
            
            // Convert wstring to UTF-8
            if (wideStr.empty()) return "";
            
            int utf8Size = WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (utf8Size <= 0) return "";
            
            std::string utf8Str(utf8Size - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, wideStr.c_str(), -1, &utf8Str[0], utf8Size, nullptr, nullptr);
            
            return utf8Str;
        }
    }
    
    return "";
}

// ===================================================================
// Frame Parsing
// ===================================================================

std::string Id3v2Reader::parseTextFrame(const uint8_t* frameData, uint32_t frameSize) {
    if (frameSize < 1) return "";
    
    TextEncoding encoding = static_cast<TextEncoding>(frameData[0]);
    const uint8_t* textData = frameData + 1;
    uint32_t textSize = frameSize - 1;
    
    std::string result = decodeText(textData, textSize, encoding);
    
    // Remove trailing nulls
    while (!result.empty() && result.back() == '\0') {
        result.pop_back();
    }
    
    return result;
}

bool Id3v2Reader::parseCoverArtFrame(const uint8_t* frameData, uint32_t frameSize, 
                                     std::vector<uint8_t>& imageData, std::string& mimeType) {
    if (frameSize < 1) return false;
    
    const uint8_t* ptr = frameData;
    uint32_t remaining = frameSize;
    
    // Read text encoding
    TextEncoding encoding = static_cast<TextEncoding>(*ptr++);
    remaining--;
    
    // Read MIME type (null-terminated)
    std::string mime;
    while (remaining > 0 && *ptr != 0) {
        mime += static_cast<char>(*ptr++);
        remaining--;
    }
    if (remaining > 0) { ptr++; remaining--; } // Skip null terminator
    
    mimeType = mime;
    
    // Read picture type (1 byte)
    if (remaining < 1) return false;
    ptr++; // Skip picture type
    remaining--;
    
    // Read description (null-terminated, encoding-aware)
    while (remaining > 0) {
        if (encoding == TextEncoding::UTF16_BOM || encoding == TextEncoding::UTF16_BE) {
            // UTF-16: look for double null
            if (remaining >= 2 && ptr[0] == 0 && ptr[1] == 0) {
                ptr += 2;
                remaining -= 2;
                break;
            }
            ptr++;
            remaining--;
        } else {
            // 8-bit encoding: look for single null
            if (*ptr == 0) {
                ptr++;
                remaining--;
                break;
            }
            ptr++;
            remaining--;
        }
    }
    
    // Remaining data is image
    if (remaining > 0) {
        imageData.assign(ptr, ptr + remaining);
        return true;
    }
    
    return false;
}

// ===================================================================
// Read from Memory
// ===================================================================

std::optional<AudioMetadata> Id3v2Reader::readFromMemory(const uint8_t* data, size_t dataSize) {
    if (dataSize < ID3V2_HEADER_SIZE) {
        return std::nullopt;
    }
    
    // Parse ID3v2 header
    Id3v2Header header;
    std::memcpy(&header, data, ID3V2_HEADER_SIZE);
    
    if (!header.isValid()) {
        return std::nullopt;
    }
    
    uint32_t tagSize = header.getTagSize();
    int majorVersion = header.getMajorVersion();
    
    // Only support v2.3 and v2.4
    if (majorVersion != 3 && majorVersion != 4) {
        return std::nullopt;
    }
    
    if (dataSize < ID3V2_HEADER_SIZE + tagSize) {
        return std::nullopt;
    }
    
    AudioMetadata metadata;
    
    // Parse frames
    const uint8_t* framePtr = data + ID3V2_HEADER_SIZE;
    const uint8_t* tagEnd = framePtr + tagSize;
    
    while (framePtr + ID3V2_FRAME_HEADER_SIZE <= tagEnd) {
        FrameHeader frameHeader;
        std::memcpy(frameHeader.frameId, framePtr, 4);
        frameHeader.frameId[4] = '\0';
        
        if (majorVersion == 4) {
            frameHeader.size = parseSynchsafeInt(framePtr + 4, 4);
        } else {
            // v2.3 uses normal integer
            frameHeader.size = (framePtr[4] << 24) | (framePtr[5] << 16) | 
                              (framePtr[6] << 8) | framePtr[7];
        }
        
        frameHeader.flags = (framePtr[8] << 8) | framePtr[9];
        
        if (!frameHeader.isValid() || frameHeader.size == 0) {
            break; // Padding or end of frames
        }
        
        const uint8_t* frameData = framePtr + ID3V2_FRAME_HEADER_SIZE;
        
        if (frameData + frameHeader.size > tagEnd) {
            break; // Invalid frame size
        }
        
        // Parse known frames
        std::string frameId(frameHeader.frameId);
        
        if (frameId == "TIT2") {
            metadata.title = parseTextFrame(frameData, frameHeader.size);
        } else if (frameId == "TPE1") {
            metadata.artist = parseTextFrame(frameData, frameHeader.size);
        } else if (frameId == "TALB") {
            metadata.album = parseTextFrame(frameData, frameHeader.size);
        } else if (frameId == "TCON") {
            metadata.genre = parseTextFrame(frameData, frameHeader.size);
        } else if (frameId == "COMM") {
            metadata.comment = parseTextFrame(frameData, frameHeader.size);
        } else if (frameId == "TLEN") {
            std::string durationStr = parseTextFrame(frameData, frameHeader.size);
            if (!durationStr.empty()) {
                char* endPtr = nullptr;
                long durVal = std::strtol(durationStr.c_str(), &endPtr, 10);
                if (endPtr != durationStr.c_str() && durVal > 0) {
                    metadata.durationMs = static_cast<int>(durVal);
                }
            }
        } else if (frameId == "TYER" || frameId == "TDRC") {
            // TYER (v2.3) or TDRC (v2.4) = recording year
            std::string yearStr = parseTextFrame(frameData, frameHeader.size);
            if (yearStr.size() >= 4) {
                std::string yearSub = yearStr.substr(0, 4);
                char* endPtr = nullptr;
                long yearVal = std::strtol(yearSub.c_str(), &endPtr, 10);
                if (endPtr != yearSub.c_str() && yearVal > 0) {
                    metadata.year = static_cast<int>(yearVal);
                }
            }
        } else if (frameId == "TRCK") {
            std::string trackStr = parseTextFrame(frameData, frameHeader.size);
            if (!trackStr.empty()) {
                size_t slashPos = trackStr.find('/');
                std::string trackSub = (slashPos != std::string::npos) ? trackStr.substr(0, slashPos) : trackStr;
                char* endPtr = nullptr;
                long trackVal = std::strtol(trackSub.c_str(), &endPtr, 10);
                if (endPtr != trackSub.c_str() && trackVal > 0) {
                    metadata.trackNumber = static_cast<int>(trackVal);
                }
            }
        } else if (frameId == "APIC") {
            parseCoverArtFrame(frameData, frameHeader.size, 
                             metadata.coverArtData, metadata.coverArtMimeType);
        }
        
        framePtr = frameData + frameHeader.size;
    }
    
    return metadata;
}

// ===================================================================
// Read from File
// ===================================================================

std::optional<AudioMetadata> Id3v2Reader::readFromFile(const wchar_t* filePathUtf16) {
    // Open file
    HANDLE fileHandle = CreateFileW(
        filePathUtf16,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    
    if (fileHandle == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    
    // Read first 64KB (enough for most ID3 tags)
    constexpr DWORD READ_SIZE = 65536;
    std::vector<uint8_t> buffer(READ_SIZE);
    
    DWORD bytesRead = 0;
    BOOL success = ReadFile(fileHandle, buffer.data(), READ_SIZE, &bytesRead, nullptr);
    CloseHandle(fileHandle);
    
    if (!success || bytesRead < ID3V2_HEADER_SIZE) {
        return std::nullopt;
    }
    
    return readFromMemory(buffer.data(), bytesRead);
}

} // namespace metadata
} // namespace audio_io
