// ==============================================================================
// convert_mp3_to_wav.cpp - Convert MP3 to WAV Example
// ==============================================================================

#include <audio_io/audio_reader.h>
#include <audio_io/audio_writer.h>
#include <iostream>

using namespace audio_io;

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <input.mp3> <output.wav>\n";
        return 1;
    }
    
    std::wstring input = std::wstring(argv[1], argv[1] + strlen(argv[1]));
    std::wstring output = std::wstring(argv[2], argv[2] + strlen(argv[2]));
    
    std::wcout << L"Converting: " << input << L" -> " << output << L"\n";
    
    // Read MP3 file
    AudioReader reader;
    auto audioResult = reader.readFile(input);
    
    if (!audioResult) {
        std::cerr << "Error reading MP3: " << audioResult.error().message << "\n";
        return 1;
    }
    
    const AudioData& audio = *audioResult;
    std::cout << "Sample Rate: " << audio.format.sampleRateHz << " Hz\n";
    std::cout << "Channels: " << audio.format.channelCount << "\n";
    std::cout << "Samples: " << audio.samples.size() << "\n";
    
    // Write as 16-bit WAV
    AudioWriter writer;
    auto writeResult = writer.writeFile(output.c_str(), audio, 16);
    
    if (!writeResult) {
        std::cerr << "Error writing WAV: " << writeResult.error().message << "\n";
        return 1;
    }
    
    std::wcout << L"Successfully converted to " << output << L"\n";
    
    return 0;
}
