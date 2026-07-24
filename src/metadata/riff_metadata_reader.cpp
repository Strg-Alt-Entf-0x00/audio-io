// riff_metadata_reader.cpp - RIFF/WAV Metadata Reader Implementation
// Part of audio-io-1.0.0

#include "riff_metadata_reader.h"
#include <windows.h>
#include <cstring>

namespace audio_io {
namespace metadata {

// ===================================================================
// ChunkHeader Implementation
// ===================================================================

bool RiffMetadataReader::ChunkHeader::isValid() const {
    // Chunk ID should be printable ASCII
    for (int i = 0; i < 4; ++i) {
        if (id[i] < 0x20 || id[i] > 0x7E) {
            return false;
        }
    }
    return size > 0 && size < 0x7FFFFFFF;  // Reasonable size limit
}

// ===================================================================
// Helper Functions
// ===================================================================

// Read 32-bit little-endian integer
static uint32_t readUInt32LE(const uint8_t* data) {
    return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}

// Compare chunk ID
static bool chunkIdEquals(const char* id1, const char* id2) {
    return std::memcmp(id1, id2, 4) == 0;
}

// ===================================================================
// RIFF Parsing
// ===================================================================

bool RiffMetadataReader::parseRiffHeader(const uint8_t* data, size_t dataSize, size_t& offset) {
    if (dataSize < 12) {
        return false;  // Too small for RIFF header
    }
    
    // Check RIFF signature
    if (!chunkIdEquals(reinterpret_cast<const char*>(data), "RIFF")) {
        return false;
    }
    
    // Read file size (unused here)
    // uint32_t fileSize = readUInt32LE(data + 4);
    
    // Check WAVE format
    if (!chunkIdEquals(reinterpret_cast<const char*>(data + 8), "WAVE")) {
        return false;
    }
    
    offset = 12;  // Start after RIFF header
    return true;
}

bool RiffMetadataReader::findChunk(const uint8_t* data, size_t dataSize, 
                                   const char* chunkId, size_t& chunkOffset, uint32_t& chunkSize) {
    size_t offset = 12;  // Start after RIFF header
    
    while (offset + 8 <= dataSize) {
        ChunkHeader header;
        std::memcpy(header.id, data + offset, 4);
        header.size = readUInt32LE(data + offset + 4);
        
        if (chunkIdEquals(header.id, chunkId)) {
            chunkOffset = offset + 8;
            chunkSize = header.size;
            return true;
        }
        
        // Move to next chunk (chunks are word-aligned)
        offset += 8 + header.size;
        if (header.size & 1) {
            offset++;  // Skip padding byte
        }
    }
    
    return false;
}

// ===================================================================
// INFO List Parsing
// ===================================================================

std::string RiffMetadataReader::parseInfoString(const uint8_t* data, uint32_t size) {
    if (size == 0) return "";
    
    // INFO strings are null-terminated ASCII/UTF-8
    std::string result(reinterpret_cast<const char*>(data), size);
    
    // Remove trailing nulls
    while (!result.empty() && result.back() == '\0') {
        result.pop_back();
    }
    
    return result;
}

void RiffMetadataReader::parseInfoListChunk(const uint8_t* data, size_t dataSize,
                                            size_t offset, uint32_t size, AudioMetadata& metadata) {
    if (offset + size > dataSize) {
        return;
    }
    
    const uint8_t* listData = data + offset;
    const uint8_t* listEnd = listData + size;
    
    // First 4 bytes should be "INFO"
    if (size < 4 || !chunkIdEquals(reinterpret_cast<const char*>(listData), "INFO")) {
        return;
    }
    
    const uint8_t* ptr = listData + 4;
    
    while (ptr + 8 <= listEnd) {
        ChunkHeader header;
        std::memcpy(header.id, ptr, 4);
        header.size = readUInt32LE(ptr + 4);
        
        const uint8_t* chunkData = ptr + 8;
        
        if (chunkData + header.size > listEnd) {
            break;  // Invalid chunk
        }
        
        // Parse known INFO items
        if (chunkIdEquals(header.id, INFO_TITLE)) {
            metadata.title = parseInfoString(chunkData, header.size);
        } else if (chunkIdEquals(header.id, INFO_ARTIST)) {
            metadata.artist = parseInfoString(chunkData, header.size);
        } else if (chunkIdEquals(header.id, INFO_ALBUM)) {
            metadata.album = parseInfoString(chunkData, header.size);
        } else if (chunkIdEquals(header.id, INFO_GENRE)) {
            metadata.genre = parseInfoString(chunkData, header.size);
        } else if (chunkIdEquals(header.id, INFO_COMMENT)) {
            metadata.comment = parseInfoString(chunkData, header.size);
        } else if (chunkIdEquals(header.id, INFO_DATE)) {
            // Try to parse year from date string (e.g., "2024", "2024-06-29")
            std::string dateStr = parseInfoString(chunkData, header.size);
            if (dateStr.size() >= 4) {
                std::string yearStr = dateStr.substr(0, 4);
                char* endPtr = nullptr;
                long yearVal = std::strtol(yearStr.c_str(), &endPtr, 10);
                if (endPtr != yearStr.c_str() && yearVal > 0) {
                    metadata.year = static_cast<int>(yearVal);
                }
            }
        } else if (chunkIdEquals(header.id, INFO_TRACK)) {
            std::string trackStr = parseInfoString(chunkData, header.size);
            if (!trackStr.empty()) {
                char* endPtr = nullptr;
                long trackVal = std::strtol(trackStr.c_str(), &endPtr, 10);
                if (endPtr != trackStr.c_str() && trackVal > 0) {
                    metadata.trackNumber = static_cast<int>(trackVal);
                }
            }
        }
        
        // Move to next chunk (word-aligned)
        ptr = chunkData + header.size;
        if (header.size & 1) {
            ptr++;  // Skip padding byte
        }
    }
}

// ===================================================================
// Read from Memory
// ===================================================================

std::optional<AudioMetadata> RiffMetadataReader::readFromMemory(const uint8_t* data, size_t dataSize) {
    size_t offset = 0;
    
    // Parse RIFF header
    if (!parseRiffHeader(data, dataSize, offset)) {
        return std::nullopt;
    }
    
    AudioMetadata metadata;
    
    // Find LIST chunk with INFO
    size_t listOffset = 0;
    uint32_t listSize = 0;
    
    // Search for chunks
    offset = 12;
    while (offset + 8 <= dataSize) {
        ChunkHeader header;
        std::memcpy(header.id, data + offset, 4);
        header.size = readUInt32LE(data + offset + 4);
        
        if (chunkIdEquals(header.id, "LIST")) {
            // Check if this is an INFO list
            const uint8_t* listData = data + offset + 8;
            if (header.size >= 4 && chunkIdEquals(reinterpret_cast<const char*>(listData), "INFO")) {
                listOffset = offset + 8;
                listSize = header.size;
            }
        } else if (chunkIdEquals(header.id, "bext")) {
            // Parse Broadcast Wave Format chunk
            if (header.size >= 256 + 32 + 32 + 10 + 8 + 8 + 2 + 64) {
                const uint8_t* bextData = data + offset + 8;
                
                auto readStr = [](const uint8_t* src, size_t maxLen) {
                    size_t len = 0;
                    while (len < maxLen && src[len] != '\0') len++;
                    return std::string(reinterpret_cast<const char*>(src), len);
                };
                
                metadata.bwf.description = readStr(bextData, 256);
                metadata.bwf.originator = readStr(bextData + 256, 32);
                metadata.bwf.originatorReference = readStr(bextData + 288, 32);
                metadata.bwf.originationDate = readStr(bextData + 320, 10);
                metadata.bwf.originationTime = readStr(bextData + 330, 8);
                
                uint32_t timeRefLow = readUInt32LE(bextData + 338);
                uint32_t timeRefHigh = readUInt32LE(bextData + 342);
                metadata.bwf.timeReference = (static_cast<uint64_t>(timeRefHigh) << 32) | timeRefLow;
                
                metadata.bwf.version = bextData[346] | (bextData[347] << 8);
                metadata.bwf.umid = readStr(bextData + 348, 64);
                
                metadata.bwf.hasData = true;
            }
        }
        
        // Move to next chunk
        offset += 8 + header.size;
        if (header.size & 1) {
            offset++;
        }
    }
    
    if (listOffset > 0) {
        parseInfoListChunk(data, dataSize, listOffset, listSize, metadata);
    }
    
    // Return metadata even if empty (caller can check isEmpty())
    return metadata;
}

// ===================================================================
// Read from File
// ===================================================================

std::optional<AudioMetadata> RiffMetadataReader::readFromFile(const wchar_t* filePathUtf16) {
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
    
    // Read first 64KB (should contain all metadata chunks)
    constexpr DWORD READ_SIZE = 65536;
    std::vector<uint8_t> buffer(READ_SIZE);
    
    DWORD bytesRead = 0;
    BOOL success = ReadFile(fileHandle, buffer.data(), READ_SIZE, &bytesRead, nullptr);
    CloseHandle(fileHandle);
    
    if (!success || bytesRead < 12) {
        return std::nullopt;
    }
    
    return readFromMemory(buffer.data(), bytesRead);
}

} // namespace metadata
} // namespace audio_io
