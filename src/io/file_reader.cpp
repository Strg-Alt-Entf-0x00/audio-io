#include "file_reader.h"

namespace audio_io {
namespace io {

Expected<std::vector<uint8_t>, AudioError> FileReader::readEntireFile(const wchar_t* filePathUtf16) {
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
        return makeUnexpected(AudioError{
            "Cannot open file (CreateFileW failed)",
            static_cast<int>(GetLastError())
        });
    }
    
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(fileHandle, &fileSize)) {
        DWORD error = GetLastError();
        CloseHandle(fileHandle);
        return makeUnexpected(AudioError{
            "Cannot get file size",
            static_cast<int>(error)
        });
    }
    
    if (fileSize.QuadPart > 0x7FFFFFFF) {  // > 2GB
        CloseHandle(fileHandle);
        return makeUnexpected(AudioError{
            "File too large (> 2GB)",
            0
        });
    }
    
    size_t fileSizeBytes = static_cast<size_t>(fileSize.QuadPart);
    std::vector<uint8_t> fileData(fileSizeBytes);
    
    DWORD bytesRead = 0;
    if (!ReadFile(fileHandle, fileData.data(), static_cast<DWORD>(fileSizeBytes), &bytesRead, nullptr)) {
        DWORD error = GetLastError();
        CloseHandle(fileHandle);
        return makeUnexpected(AudioError{
            "Cannot read file data",
            static_cast<int>(error)
        });
    }
    
    CloseHandle(fileHandle);
    
    if (bytesRead != fileSizeBytes) {
        return makeUnexpected(AudioError{
            "File read incomplete",
            0
        });
    }
    
    return fileData;
}

Expected<std::vector<uint8_t>, AudioError> FileReader::readBytes(const wchar_t* filePathUtf16, int64_t offset, size_t count) {
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
        return makeUnexpected(AudioError{
            "Cannot open file (CreateFileW failed)",
            static_cast<int>(GetLastError())
        });
    }
    
    LARGE_INTEGER liOffset;
    liOffset.QuadPart = offset;
    if (!SetFilePointerEx(fileHandle, liOffset, nullptr, FILE_BEGIN)) {
        DWORD error = GetLastError();
        CloseHandle(fileHandle);
        return makeUnexpected(AudioError{
            "Seek failed",
            static_cast<int>(error)
        });
    }
    
    std::vector<uint8_t> data(count);
    DWORD bytesRead = 0;
    if (!ReadFile(fileHandle, data.data(), static_cast<DWORD>(count), &bytesRead, nullptr)) {
        DWORD error = GetLastError();
        CloseHandle(fileHandle);
        return makeUnexpected(AudioError{
            "Cannot read file data",
            static_cast<int>(error)
        });
    }
    
    CloseHandle(fileHandle);
    data.resize(bytesRead); // Truncate if EOF reached early
    return data;
}

Expected<HANDLE, AudioError> FileReader::openForStreaming(const wchar_t* filePathUtf16) {
    HANDLE fileHandle = CreateFileW(
        filePathUtf16,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );
    
    if (fileHandle == INVALID_HANDLE_VALUE) {
        return makeUnexpected(AudioError{
            "Failed to open file for streaming",
            static_cast<int>(GetLastError())
        });
    }
    return fileHandle;
}

} // namespace io
} // namespace audio_io
