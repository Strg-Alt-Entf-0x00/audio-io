// ==============================================================================
// read_audio_file.cpp - Simple Audio File Reading Example
// ==============================================================================

#include <audio_io/audio_reader.h>
#include <iostream>

using namespace audio_io;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <audio_file>\n";
        return 1;
    }
    
    std::wstring filename = std::wstring(argv[1], argv[1] + strlen(argv[1]));
    
    AudioReader reader;
    
    // Read audio file
    auto result = reader.readFile(filename);
    
    if (!result) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
    }
    
    const AudioData& audio = *result;
    
    // Display information
    std::wcout << L"File: " << filename << L"\n";
    std::cout << "Sample Rate: " << audio.format.sampleRateHz << " Hz\n";
    std::cout << "Channels: " << audio.format.channelCount << "\n";
    std::cout << "Sample Count: " << audio.samples.size() << "\n";
    
    double duration = static_cast<double>(audio.samples.size()) / audio.format.channelCount 
                     / static_cast<double>(audio.format.sampleRateHz);
    std::cout << "Duration: " << duration << " seconds\n";
    
    // Calculate peak level
    float peak = 0.0f;
    for (float sample : audio.samples) {
        peak = std::max(peak, std::abs(sample));
    }
    std::cout << "Peak Level: " << peak << "\n";
    
    return 0;
}
