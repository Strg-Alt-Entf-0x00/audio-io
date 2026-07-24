// audio_metadata.h - Unified audio metadata structure
// Part of audio-io-1.0.0
// Windows-native, C++20
//
// Supports:
// - ID3v2.3/2.4 tags (MP3)
// - RIFF INFO chunks (WAV)
// - Common metadata fields across all formats

#pragma once

#include "export.h"
#include <string>
#include <vector>
#include <cstdint>
#include <optional>

namespace audio_io {

struct AUDIO_IO_API BwfMetadata {
    std::string description;
    std::string originator;
    std::string originatorReference;
    std::string originationDate; // YYYY-MM-DD
    std::string originationTime; // HH:MM:SS
    uint64_t timeReference;
    uint16_t version;
    std::string umid; // 64 bytes
    
    bool hasData = false;
};

// Unified metadata structure for all audio formats
struct AUDIO_IO_API AudioMetadata {
    // Common tags (supported by both MP3 and WAV)
    std::string title;
    std::string artist;
    std::string album;
    std::string genre;
    std::string comment;
    
    // Extended metadata
    int year;               // Recording year (0 = unknown)
    int trackNumber;        // Track number (0 = unknown)
    int durationMs;         // Duration in milliseconds (0 = unknown)
    
    // Cover art / album art
    std::vector<uint8_t> coverArtData;
    std::string coverArtMimeType;  // e.g., "image/jpeg", "image/png"
    
    // Broadcast Wave Format extension
    BwfMetadata bwf;
    
    // Constructor with defaults
    AudioMetadata() 
        : year(0)
        , trackNumber(0)
        , durationMs(0)
    {}
    
    // Check if metadata has any content
    bool isEmpty() const {
        return title.empty() && 
               artist.empty() && 
               album.empty() && 
               genre.empty() &&
               year == 0 &&
               trackNumber == 0 &&
               coverArtData.empty() &&
               !bwf.hasData;
    }
};

} // namespace audio_io
