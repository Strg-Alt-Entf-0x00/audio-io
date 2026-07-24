// id3v2_writer.cpp - ID3v2.3 Tag Writer Implementation
// Part of audio-io-1.0.0

#include "id3v2_writer.h"


namespace audio_io {
namespace metadata {

// ============================================================================
// Private helpers
// ============================================================================

void Id3v2Writer::writeUint32BE(std::vector<uint8_t>& buf, uint32_t value) {
    buf.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((value >> 8)  & 0xFF));
    buf.push_back(static_cast<uint8_t>(value & 0xFF));
}

void Id3v2Writer::writeSynchsafeInt(std::vector<uint8_t>& buf, uint32_t value) {
    // Synchsafe: each byte stores 7 bits, MSB is always 0
    buf.push_back(static_cast<uint8_t>((value >> 21) & 0x7F));
    buf.push_back(static_cast<uint8_t>((value >> 14) & 0x7F));
    buf.push_back(static_cast<uint8_t>((value >> 7)  & 0x7F));
    buf.push_back(static_cast<uint8_t>(value & 0x7F));
}

void Id3v2Writer::writeTextFrame(std::vector<uint8_t>& buf, const char id[4], const std::string& value) {
    if (value.empty()) return;

    // Frame content: encoding byte (0x03 = UTF-8) + UTF-8 string (no null terminator needed)
    uint32_t contentSize = 1 + static_cast<uint32_t>(value.size());

    // Frame ID (4 bytes)
    buf.push_back(static_cast<uint8_t>(id[0]));
    buf.push_back(static_cast<uint8_t>(id[1]));
    buf.push_back(static_cast<uint8_t>(id[2]));
    buf.push_back(static_cast<uint8_t>(id[3]));
    // Frame size (4 bytes, big-endian, NOT synchsafe for v2.3)
    writeUint32BE(buf, contentSize);
    // Frame flags (2 bytes, all zero)
    buf.push_back(0x00);
    buf.push_back(0x00);
    // Content: encoding + UTF-8 text
    buf.push_back(0x03); // UTF-8
    for (char c : value) buf.push_back(static_cast<uint8_t>(c));
}

void Id3v2Writer::writeCommentFrame(std::vector<uint8_t>& buf, const std::string& comment) {
    if (comment.empty()) return;

    // COMM frame: encoding(1) + language(3) + short description(1, empty) + text
    // We use UTF-8, language "eng", empty short description
    uint32_t contentSize = 1 + 3 + 1 + static_cast<uint32_t>(comment.size());

    buf.push_back('C'); buf.push_back('O'); buf.push_back('M'); buf.push_back('M');
    writeUint32BE(buf, contentSize);
    buf.push_back(0x00); buf.push_back(0x00); // flags
    buf.push_back(0x03); // encoding: UTF-8
    buf.push_back('e'); buf.push_back('n'); buf.push_back('g'); // language
    buf.push_back(0x00); // empty short description (null terminator)
    for (char c : comment) buf.push_back(static_cast<uint8_t>(c));
}

// ============================================================================
// Public API
// ============================================================================

std::vector<uint8_t> Id3v2Writer::prependTag(
    const std::vector<uint8_t>& mp3,
    const AudioMetadata& metadata)
{
    if (metadata.isEmpty()) return mp3;

    // Build all frames into a buffer
    std::vector<uint8_t> frames;
    frames.reserve(256);

    writeTextFrame(frames, "TIT2", metadata.title);
    writeTextFrame(frames, "TPE1", metadata.artist);
    writeTextFrame(frames, "TALB", metadata.album);
    writeTextFrame(frames, "TCON", metadata.genre);
    writeCommentFrame(frames, metadata.comment);

    if (metadata.year > 0) {
        writeTextFrame(frames, "TYER", std::to_string(metadata.year));
    }
    if (metadata.trackNumber > 0) {
        writeTextFrame(frames, "TRCK", std::to_string(metadata.trackNumber));
    }

    if (frames.empty()) return mp3;

    // Build the ID3v2.3 header (10 bytes)
    uint32_t tagPayloadSize = static_cast<uint32_t>(frames.size());

    std::vector<uint8_t> result;
    result.reserve(10 + tagPayloadSize + mp3.size());

    // ID3 identifier
    result.push_back('I'); result.push_back('D'); result.push_back('3');
    // Version: 2.3.0
    result.push_back(0x03); result.push_back(0x00);
    // Flags: none
    result.push_back(0x00);
    // Tag size as synchsafe integer
    writeSynchsafeInt(result, tagPayloadSize);

    // Append frames
    result.insert(result.end(), frames.begin(), frames.end());
    // Append original MP3 stream
    result.insert(result.end(), mp3.begin(), mp3.end());

    return result;
}

} // namespace metadata
} // namespace audio_io
