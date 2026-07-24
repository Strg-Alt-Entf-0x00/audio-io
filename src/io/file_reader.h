#pragma once

#include "audio_io/expected.h"
#include "audio_io/audio_reader.h"
#include <windows.h>
#include <cstdint>
#include <vector>

namespace audio_io {
namespace io {

class FileReader {
public:
    static Expected<std::vector<uint8_t>, AudioError> readEntireFile(const wchar_t* filePathUtf16);
    static Expected<std::vector<uint8_t>, AudioError> readBytes(const wchar_t* filePathUtf16, int64_t offset, size_t count);
    static Expected<HANDLE, AudioError> openForStreaming(const wchar_t* filePathUtf16);
};

} // namespace io
} // namespace audio_io
