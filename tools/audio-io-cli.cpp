// audio-io-cli.cpp
// Command-line interface for encoding and decoding audio using audio-io.

#include <audio_io/audio_reader.h>
#include <audio_io/audio_writer.h>
#include <iostream>
#include <string>
#include <vector>
#include <windows.h>

using namespace audio_io;

std::wstring stringToWstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

void printUsage() {
    std::cout << "audio-io-cli - Audio I/O Command Line Tool\n";
    std::cout << "Usage:\n";
    std::cout << "  audio-io-cli encode <in.wav> <out.mp3> [kbps]\n";
    std::cout << "  audio-io-cli decode <in.mp3> <out.wav>\n";
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printUsage();
        return 1;
    }

    std::string command = argv[1];
    std::wstring inFile = stringToWstring(argv[2]);
    std::wstring outFile = stringToWstring(argv[3]);

    if (command == "encode") {
        int kbps = 192;
        if (argc >= 5) {
            kbps = std::stoi(argv[4]);
        }
        
        AudioReader reader;
        auto readRes = reader.readFile(inFile.c_str());
        if (!readRes) {
            std::cerr << "Failed to read input file: " << readRes.error().message << "\n";
            return 1;
        }

        const auto& audio = *readRes;
        AudioWriter writer;
        
        // Ensure input is PCM (we only support encoding from raw samples via API)
        auto encRes = writer.encodeMp3(
            audio.samples.data(),
            audio.samples.size() / audio.format.channelCount,
            audio.format.sampleRateHz,
            audio.format.channelCount,
            kbps
        );

        if (!encRes) {
            std::cerr << "Failed to encode MP3: " << encRes.error().message << "\n";
            return 1;
        }

        HANDLE hFile = CreateFileW(outFile.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            std::cerr << "Failed to open output file for writing.\n";
            return 1;
        }

        DWORD written;
        WriteFile(hFile, encRes->data(), (DWORD)encRes->size(), &written, nullptr);
        CloseHandle(hFile);
        
        std::cout << "Successfully encoded to " << argv[3] << " (" << kbps << " kbps)\n";

    } else if (command == "decode") {
        AudioReader reader;
        auto readRes = reader.readFile(inFile.c_str());
        if (!readRes) {
            std::cerr << "Failed to read input file: " << readRes.error().message << "\n";
            return 1;
        }
        
        const auto& audio = *readRes;
        AudioWriter writer;
        
        // Write out as WAV (16-bit)
        auto writeRes = writer.writeFile(outFile.c_str(), audio, 16);
        if (!writeRes) {
            std::cerr << "Failed to write WAV file: " << writeRes.error().message << "\n";
            return 1;
        }
        
        std::cout << "Successfully decoded to " << argv[3] << "\n";
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        printUsage();
        return 1;
    }

    return 0;
}
