// test_mp3_encoder.cpp - MP3 encoder tests
// Part of audio-io-1.0.0
// Tests MPEG-1 Layer III encoding functionality

#include "audio_io/audio_reader.h"
#include "audio_io/audio_writer.h"
#include <iostream>
#include <windows.h>

using namespace audio_io;

// Test result tracking
struct TestResults {
    int totalTests = 0;
    int passedTests = 0;
    
    void pass(const char* testName) {
        totalTests++;
        passedTests++;
        std::cout << "[PASS] " << testName << std::endl;
    }
    
    void fail(const char* testName, const char* reason) {
        totalTests++;
        std::cout << "[FAIL] " << testName << ": " << reason << std::endl;
    }
    
    void summary() {
        std::cout << "\n========================================\n";
        std::cout << "Test Results: " << passedTests << "/" << totalTests << " passed\n";
        if (passedTests == totalTests) {
            std::cout << "ALL TESTS PASSED!\n";
        } else {
            std::cout << "SOME TESTS FAILED!\n";
        }
        std::cout << "========================================\n";
    }
};

// Helper: Write bytes to file
bool writeBytesToFile(const wchar_t* path, const std::vector<uint8_t>& data) {
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr, 
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    
    DWORD bytesWritten;
    bool success = WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), 
                            &bytesWritten, nullptr);
    CloseHandle(hFile);
    
    return success && bytesWritten == data.size();
}

// Test 1: Encode synthetic sine wave to MP3
void test_encode_sine_wave(TestResults& results) {
    // Generate 1 second sine wave at 440 Hz (A4 note)
    const int sampleRate = 44100;
    const int channels = 2;
    const int duration = 1; // seconds
    const int sampleCount = sampleRate * duration * channels;
    
    std::vector<float> samples(sampleCount);
    
    // Generate stereo sine wave
    for (int i = 0; i < sampleRate * duration; i++) {
        float t = static_cast<float>(i) / sampleRate;
        float value = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * t);
        samples[i * 2 + 0] = value; // Left
        samples[i * 2 + 1] = value; // Right
    }
    
    // Encode to MP3
    AudioWriter writer;
    auto encodeResult = writer.encodeMp3(
        samples.data(),
        sampleRate * duration,
        sampleRate,
        channels,
        320 // kbps
    );
    
    if (!encodeResult) {
        results.fail("Encode sine wave to MP3", encodeResult.error().message.c_str());
        return;
    }
    
    const auto& mp3Data = *encodeResult;
    
    // Validate MP3 data
    if (mp3Data.size() < 100) {
        results.fail("Encode sine wave to MP3", "Output too small");
        return;
    }
    
    // Check for MP3 frame sync (0xFFE or 0xFFF in first 12 bits)
    bool hasSync = false;
    for (size_t i = 0; i < std::min<size_t>(mp3Data.size() - 1, 1000); i++) {
        if ((mp3Data[i] == 0xFF) && ((mp3Data[i + 1] & 0xE0) == 0xE0)) {
            hasSync = true;
            break;
        }
    }
    
    if (!hasSync) {
        results.fail("Encode sine wave to MP3", "No valid MP3 frame sync found");
        return;
    }
    
    // Write to file for manual inspection
    writeBytesToFile(L"test_output_sine_320kbps.mp3", mp3Data);
    
    results.pass("Encode sine wave to MP3");
}

// Test 2: Encode silence
void test_encode_silence(TestResults& results) {
    const int sampleRate = 44100;
    const int channels = 2;
    const int sampleCount = sampleRate * channels; // 1 second
    
    std::vector<float> samples(sampleCount, 0.0f);
    
    AudioWriter writer;
    auto encodeResult = writer.encodeMp3(
        samples.data(),
        sampleRate,
        sampleRate,
        channels,
        192
    );
    
    if (!encodeResult) {
        results.fail("Encode silence to MP3", encodeResult.error().message.c_str());
        return;
    }
    
    const auto& mp3Data = *encodeResult;
    
    if (mp3Data.size() < 50) {
        results.fail("Encode silence to MP3", "Output too small");
        return;
    }
    
    results.pass("Encode silence to MP3");
}

// Test 3: Encode mono audio
void test_encode_mono(TestResults& results) {
    const int sampleRate = 44100;
    const int channels = 1;
    const int sampleCount = sampleRate; // 1 second
    
    std::vector<float> samples(sampleCount);
    
    // Generate mono sine wave
    for (int i = 0; i < sampleCount; i++) {
        float t = static_cast<float>(i) / sampleRate;
        samples[i] = 0.3f * std::sin(2.0f * 3.14159265f * 880.0f * t);
    }
    
    AudioWriter writer;
    auto encodeResult = writer.encodeMp3(
        samples.data(),
        sampleCount,
        sampleRate,
        channels,
        128
    );
    
    if (!encodeResult) {
        results.fail("Encode mono to MP3", encodeResult.error().message.c_str());
        return;
    }
    
    results.pass("Encode mono to MP3");
}

// Test 4: Test different bitrates
void test_various_bitrates(TestResults& results) {
    const int sampleRate = 44100;
    const int channels = 2;
    const int sampleCount = sampleRate * channels;
    
    std::vector<float> samples(sampleCount, 0.1f); // Low amplitude constant
    
    int bitrates[] = {128, 192, 256, 320};
    
    AudioWriter writer;
    for (int bitrate : bitrates) {
        auto encodeResult = writer.encodeMp3(
            samples.data(),
            sampleRate,
            sampleRate,
            channels,
            bitrate
        );
        
        if (!encodeResult) {
            results.fail("Encode various bitrates", 
                        (std::string("Failed at ") + std::to_string(bitrate) + " kbps").c_str());
            return;
        }
    }
    
    results.pass("Encode various bitrates");
}

// Test 5: Test different sample rates
void test_various_sample_rates(TestResults& results) {
    const int channels = 2;
    int sampleRates[] = {32000, 44100, 48000};
    
    AudioWriter writer;
    for (int sampleRate : sampleRates) {
        std::vector<float> samples(sampleRate * channels, 0.1f); // 1 second
        
        auto encodeResult = writer.encodeMp3(
            samples.data(),
            sampleRate,
            sampleRate,
            channels,
            192
        );
        
        if (!encodeResult) {
            results.fail("Encode various sample rates", 
                        (std::string("Failed at ") + std::to_string(sampleRate) + " Hz").c_str());
            return;
        }
    }
    
    results.pass("Encode various sample rates");
}

// Test 6: Validate error handling - invalid sample rate
void test_invalid_sample_rate(TestResults& results) {
    std::vector<float> samples(1000, 0.0f);
    
    AudioWriter writer;
    auto encodeResult = writer.encodeMp3(
        samples.data(),
        500,
        22050, // Invalid for MP3 (only 32k, 44.1k, 48k supported)
        2,
        192
    );
    
    if (encodeResult) {
        results.fail("Invalid sample rate error handling", "Should have failed");
        return;
    }
    
    results.pass("Invalid sample rate error handling");
}

// Test 7: Validate error handling - invalid channel count
void test_invalid_channels(TestResults& results) {
    std::vector<float> samples(1000, 0.0f);
    
    AudioWriter writer;
    auto encodeResult = writer.encodeMp3(
        samples.data(),
        500,
        44100,
        5, // Invalid (only 1 or 2 supported)
        192
    );
    
    if (encodeResult) {
        results.fail("Invalid channel count error handling", "Should have failed");
        return;
    }
    
    results.pass("Invalid channel count error handling");
}

int main() {
    std::cout << "========================================\n";
    std::cout << "audio-io-1.0.0 MP3 Encoder Tests\n";
    std::cout << "========================================\n\n";
    
    TestResults results;
    
    // Run tests
    test_encode_sine_wave(results);
    test_encode_silence(results);
    test_encode_mono(results);
    test_various_bitrates(results);
    test_various_sample_rates(results);
    test_invalid_sample_rate(results);
    test_invalid_channels(results);
    
    // Summary
    results.summary();
    
    return (results.passedTests == results.totalTests) ? 0 : 1;
}
