// stream_and_resample.cpp - AudioStream Streaming + Resampling Example
// Part of audio-io-1.0.0
//
// Demonstrates chunk-based audio streaming with on-the-fly resampling.
// Usage: stream_and_resample <input_file> <target_sample_rate>

#include <audio_io/audio_stream.h>
#include <audio_io/audio_writer.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

using namespace audio_io;

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <input.wav|mp3> [target_sample_rate]\n";
        std::cerr << "  target_sample_rate: e.g. 48000 (default: original)\n";
        return 1;
    }

    std::wstring inputPath(argv[1], argv[1] + std::strlen(argv[1]));
    int targetRate = 0;
    if (argc == 3) {
        targetRate = std::atoi(argv[2]);
    }

    // Configure stream
    AudioStreamConfig config;
    config.chunkSizeFrames = 4096;
    config.targetSampleRateHz = targetRate;

    AudioStream stream;
    auto openResult = stream.open(inputPath, config);
    if (!openResult) {
        std::cerr << "Error opening file: "
                  << openResult.error().message << "\n";
        return 1;
    }

    const auto& fmt = stream.getFormat();
    std::cout << "Format:\n";
    std::cout << "  Sample Rate: " << fmt.sampleRateHz << " Hz\n";
    std::cout << "  Channels:    " << fmt.channelCount << "\n";
    std::cout << "  Duration:    " << fmt.durationSeconds() << " s\n";
    if (targetRate > 0) {
        std::cout << "  Resampling to " << targetRate << " Hz\n";
    }

    // Read all chunks and accumulate statistics
    size_t totalSamples = 0;
    int chunkCount = 0;
    float peakLevel = 0.0f;

    while (!stream.isEndOfStream()) {
        auto chunkResult = stream.readChunk();
        if (!chunkResult || chunkResult->empty()) break;

        const auto& chunk = *chunkResult;
        totalSamples += chunk.size();
        chunkCount++;

        for (float s : chunk) {
            float abs = (s < 0.0f) ? -s : s;
            if (abs > peakLevel) peakLevel = abs;
        }
    }

    stream.close();

    size_t totalFrames = totalSamples / fmt.channelCount;
    double duration = static_cast<double>(totalFrames) / fmt.sampleRateHz;

    std::cout << "\nStreaming Results:\n";
    std::cout << "  Chunks read:   " << chunkCount << "\n";
    std::cout << "  Total samples: " << totalSamples << "\n";
    std::cout << "  Total frames:  " << totalFrames << "\n";
    std::cout << "  Duration:      " << duration << " s\n";
    std::cout << "  Peak level:    " << peakLevel << "\n";

    return 0;
}
