// id3v2_writer.h - ID3v2.3 Tag Writer
// Part of audio-io-1.0.0
// Windows-native, C++20
//
// Writes ID3v2.3 tags and prepends them to a raw MP3 byte stream.
// Supported frames: TIT2 (Title), TPE1 (Artist), TALB (Album),
//                   TCON (Genre), TDRC/TYER (Year), TRCK (Track), COMM (Comment)

#pragma once

#include "audio_io/audio_metadata.h"
#include <cstdint>
#include <vector>

namespace audio_io {
namespace metadata {

class Id3v2Writer {
public:
    // Prepend an ID3v2.3 tag block to an existing raw MP3 byte stream.
    // Returns the complete .mp3 file bytes (ID3 header + original MP3 data).
    // If metadata.isEmpty(), the original mp3 bytes are returned unchanged.
    static std::vector<uint8_t> prependTag(
        const std::vector<uint8_t>& mp3,
        const AudioMetadata& metadata
    );

private:
    // Write a single ID3v2.3 text frame (UTF-8 / Latin-1)
    static void writeTextFrame(std::vector<uint8_t>& buf, const char id[4], const std::string& value);

    // Write the COMM (comment) frame (ISO-8859-1, language "eng")
    static void writeCommentFrame(std::vector<uint8_t>& buf, const std::string& comment);

    // Encode a 32-bit size as a 4-byte synchsafe integer (ID3v2 spec)
    static void writeSynchsafeInt(std::vector<uint8_t>& buf, uint32_t value);

    // Write a big-endian uint32
    static void writeUint32BE(std::vector<uint8_t>& buf, uint32_t value);
};

} // namespace metadata
} // namespace audio_io
