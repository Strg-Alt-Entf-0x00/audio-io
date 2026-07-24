// riff_info_writer.cpp - RIFF INFO Chunk Writer for WAV
// Part of audio-io-1.0.0

#include "riff_info_writer.h"
#include <cstring>
#include <string>

namespace audio_io {
namespace metadata {

// ============================================================================
// Private helpers
// ============================================================================

void RiffInfoWriter::writeUint32LE(std::vector<uint8_t>& buf, uint32_t value) {
    buf.push_back(static_cast<uint8_t>(value & 0xFF));
    buf.push_back(static_cast<uint8_t>((value >> 8)  & 0xFF));
    buf.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void RiffInfoWriter::patchUint32LE(std::vector<uint8_t>& buf, size_t offset, uint32_t value) {
    buf[offset + 0] = static_cast<uint8_t>(value & 0xFF);
    buf[offset + 1] = static_cast<uint8_t>((value >> 8)  & 0xFF);
    buf[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    buf[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void RiffInfoWriter::writeInfoSubchunk(std::vector<uint8_t>& buf, const char id[4], const std::string& value) {
    if (value.empty()) return;

    // Each INFO subchunk: 4-byte ID + 4-byte size (LE) + data + optional padding byte
    uint32_t dataSize = static_cast<uint32_t>(value.size()) + 1; // +1 for null terminator
    bool needsPad = (dataSize % 2) != 0;                         // RIFF chunks must be word-aligned

    buf.push_back(static_cast<uint8_t>(id[0]));
    buf.push_back(static_cast<uint8_t>(id[1]));
    buf.push_back(static_cast<uint8_t>(id[2]));
    buf.push_back(static_cast<uint8_t>(id[3]));
    writeUint32LE(buf, dataSize);
    for (char c : value) buf.push_back(static_cast<uint8_t>(c));
    buf.push_back(0x00); // null terminator

    if (needsPad) buf.push_back(0x00); // padding byte for word alignment
}

// ============================================================================
// Public API
// ============================================================================

std::vector<uint8_t> RiffInfoWriter::appendInfoChunk(
    const std::vector<uint8_t>& wav,
    const AudioMetadata& metadata)
{
    if (metadata.isEmpty()) return wav;
    if (wav.size() < 44) return wav; // Too small to be a valid WAV

    // Build the INFO subchunks
    std::vector<uint8_t> subchunks;
    subchunks.reserve(256);

    writeInfoSubchunk(subchunks, "INAM", metadata.title);
    writeInfoSubchunk(subchunks, "IART", metadata.artist);
    writeInfoSubchunk(subchunks, "IPRD", metadata.album);
    writeInfoSubchunk(subchunks, "IGNR", metadata.genre);
    writeInfoSubchunk(subchunks, "ICMT", metadata.comment);

    if (metadata.year > 0) {
        writeInfoSubchunk(subchunks, "ICRD", std::to_string(metadata.year));
    }
    if (metadata.trackNumber > 0) {
        writeInfoSubchunk(subchunks, "ITRK", std::to_string(metadata.trackNumber));
    }

    if (subchunks.empty()) return wav;

    // The INFO LIST chunk is:
    //   "LIST" (4) + list_size (4) + "INFO" (4) + subchunks
    // list_size = 4 (for "INFO") + subchunks.size()
    uint32_t listPayloadSize = 4 + static_cast<uint32_t>(subchunks.size());
    uint32_t listChunkTotalSize = 8 + listPayloadSize; // includes "LIST" + size field

    // Copy original WAV and append the LIST INFO chunk
    std::vector<uint8_t> result = wav;
    result.reserve(wav.size() + 8 + listPayloadSize);

    result.push_back('L'); result.push_back('I'); result.push_back('S'); result.push_back('T');
    writeUint32LE(result, listPayloadSize);
    result.push_back('I'); result.push_back('N'); result.push_back('F'); result.push_back('O');
    result.insert(result.end(), subchunks.begin(), subchunks.end());

    // Patch the RIFF chunk size at bytes [4..7]:
    // Original RIFF size = (total file size - 8). New size adds our LIST chunk.
    // RIFF chunk size is stored at offset 4 in the WAV file (little-endian)
    uint32_t originalRiffSize = 
        static_cast<uint32_t>(wav[4]) |
        (static_cast<uint32_t>(wav[5]) << 8) |
        (static_cast<uint32_t>(wav[6]) << 16) |
        (static_cast<uint32_t>(wav[7]) << 24);

    uint32_t newRiffSize = originalRiffSize + listChunkTotalSize;
    patchUint32LE(result, 4, newRiffSize);

    return result;
}

} // namespace metadata
} // namespace audio_io
