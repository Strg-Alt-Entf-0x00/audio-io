// benchmark_codec.cpp
// Measures MP3 encoding and decoding performance and SNR.

#include <audio_io/audio_writer.h>
#include <audio_io/audio_reader.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace audio_io;

int main() {
    std::cout << "===========================================\n";
    std::cout << " audio-io Codec Benchmark\n";
    std::cout << "===========================================\n\n";

    const int sampleRate = 44100;
    const int channels = 2;
    // Generate 5 minutes of audio
    const int durationSecs = 300;
    const size_t totalSamples = (size_t)sampleRate * durationSecs * channels;
    
    std::cout << "Generating " << durationSecs << " seconds of complex audio signal (" 
              << sampleRate << "Hz, " << channels << " ch)...\n";
              
    std::vector<float> pcm(totalSamples);
    
    // Generate complex audio (multiple sine waves + a little noise)
    // This gives the psychoacoustic model and bit reservoir some work
    for (size_t i = 0; i < (size_t)sampleRate * durationSecs; ++i) {
        float t = static_cast<float>(i) / sampleRate;
        float val = 0.3f * std::sin(2.0f * 3.14159f * 440.0f * t) +   // A4
                    0.2f * std::sin(2.0f * 3.14159f * 880.0f * t) +   // A5
                    0.1f * std::sin(2.0f * 3.14159f * 3000.0f * t);   // High freq
        
        // Add a tiny bit of noise
        float noise = ((rand() % 10000) / 10000.0f - 0.5f) * 0.05f;
        val += noise;
        
        // Soft clipping just in case
        if (val > 0.99f) val = 0.99f;
        if (val < -0.99f) val = -0.99f;

        pcm[i * 2 + 0] = val; // L
        pcm[i * 2 + 1] = val; // R
    }

    // ENCODING
    std::cout << "\n[1] Encoding to MP3 (320 kbps)...\n";
    AudioWriter writer;
    
    auto t1 = std::chrono::high_resolution_clock::now();
    
    auto mp3Result = writer.encodeMp3(
        pcm.data(),
        sampleRate * durationSecs,
        sampleRate,
        channels,
        320 // kbps
    );
    
    auto t2 = std::chrono::high_resolution_clock::now();
    
    if (!mp3Result) {
        std::cerr << "Encoding failed: " << mp3Result.error().message << "\n";
        return 1;
    }
    
    std::chrono::duration<double> encTime = t2 - t1;
    double encFactor = durationSecs / encTime.count();
    
    std::cout << "  Encoding time: " << encTime.count() << " seconds\n";
    std::cout << "  Encoding speed: " << encFactor << "x realtime\n";
    std::cout << "  Encoded size: " << mp3Result->size() / 1024.0 / 1024.0 << " MB\n";

    // Write temp file to read back
    const std::wstring tempFile = L"benchmark_temp.mp3";
    {
        HANDLE hFile = CreateFileW(tempFile.c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(hFile, mp3Result->data(), (DWORD)mp3Result->size(), &written, nullptr);
            CloseHandle(hFile);
        } else {
            std::cerr << "Failed to write temp MP3 file for decoding phase.\n";
            return 1;
        }
    }

    // DECODING
    std::cout << "\n[2] Decoding MP3 back to PCM...\n";
    AudioReader reader;
    
    auto t3 = std::chrono::high_resolution_clock::now();
    auto decResult = reader.readFile(tempFile.c_str());
    auto t4 = std::chrono::high_resolution_clock::now();
    
    // Clean up temp file
    DeleteFileW(tempFile.c_str());
    
    if (!decResult) {
        std::cerr << "Decoding failed: " << decResult.error().message << "\n";
        return 1;
    }
    
    std::chrono::duration<double> decTime = t4 - t3;
    double decFactor = durationSecs / decTime.count();
    
    std::cout << "  Decoding time: " << decTime.count() << " seconds\n";
    std::cout << "  Decoding speed: " << decFactor << "x realtime\n";

    // SNR CALCULATION
    std::cout << "\n[3] Calculating SNR (Signal-to-Noise Ratio)...\n";
    
    const std::vector<float>& decoded = decResult->samples;
    
    // The decoder might have padding/delay at the start (usually 576 or 1152 samples per channel)
    // We should ideally align them, but we'll do a simple correlation or just accept some delay if 
    // gapless playback is fully working (in which case delay is 0).
    // Our mp3_codec.cpp tries to handle XING tags for gapless playback.
    
    size_t compareLength = std::min(pcm.size(), decoded.size());
    double signalPower = 0.0;
    double noisePower = 0.0;
    
    // Find best alignment (cross-correlation for delay estimation)
    // We only search up to 4000 samples delay
    int bestDelay = 0;
    double minDiff = 1e15;
    
    for (int delay = 0; delay < 4000; delay += 2) {
        double diff = 0;
        // Just test the first 10000 samples to find delay
        int testLen = 10000;
        for (int i = 0; i < testLen; i++) {
            if (i + delay < decoded.size() && i < pcm.size()) {
                double e = pcm[i] - decoded[i + delay];
                diff += e * e;
            }
        }
        if (diff < minDiff) {
            minDiff = diff;
            bestDelay = delay;
        }
    }
    
    std::cout << "  Estimated Decoder Delay: " << (bestDelay / 2) << " frames\n";

    compareLength -= bestDelay;
    
    for (size_t i = 0; i < compareLength; ++i) {
        double orig = pcm[i];
        double dec = decoded[i + bestDelay];
        
        signalPower += orig * orig;
        
        double error = orig - dec;
        noisePower += error * error;
    }
    
    if (noisePower == 0.0) noisePower = 1e-10; // Prevent div by zero
    
    double snr = 10.0 * std::log10(signalPower / noisePower);
    
    std::cout << "  Signal Power: " << signalPower << "\n";
    std::cout << "  Noise Power:  " << noisePower << "\n";
    std::cout << "  SNR:          " << snr << " dB\n";
    
    std::cout << "\n===========================================\n";
    if (snr > 15.0) {
        std::cout << "SUCCESS: The codec is working as expected.\n";
    } else {
        std::cout << "WARNING: SNR is unusually low. There might be a quality issue.\n";
    }

    return 0;
}
