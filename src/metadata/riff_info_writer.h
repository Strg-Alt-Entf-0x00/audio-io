// riff_info_writer.h - RIFF INFO Chunk Writer for WAV
// Part of audio-io-1.0.0
// Windows-native, C++20
//
// Appends a RIFF INFO LIST chunk to an existing WAV byte stream.
// INFO chunk fields used:
//   INAM = title
//   IART = artist
//   IPRD = album
//   IGNR = genre
//   ICMT = comment
//   ICRD = year (as string)
//   ITRK = track number (as string)

#pragma once

#include "audio_io/audio_metadata.h"
#include <cstdint>
#include <vector>

namespace audio_io {
namespace metadata {

class RiffInfoWriter {
public:
    // Append a RIFF INFO LIST chunk to an existing WAV byte stream and
    // update the RIFF header size accordingly.
    // If metadata.isEmpty(), the original wav bytes are returned unchanged.
    static std::vector<uint8_t> appendInfoChunk(
        const std::vector<uint8_t>& wav,
        const AudioMetadata& metadata
    );

private:
    // Write a single 4-byte fourCC + padded data subchunk
    static void writeInfoSubchunk(std::vector<uint8_t>& buf, const char id[4], const std::string& value);

    // Write a little-endian uint32
    static void writeUint32LE(std::vector<uint8_t>& buf, uint32_t value);

    // Patch an existing little-endian uint32 in-place at a given byte offset
    static void patchUint32LE(std::vector<uint8_t>& buf, size_t offset, uint32_t value);
};

} // namespace metadata
} // namespace audio_io
