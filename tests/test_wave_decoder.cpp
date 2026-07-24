// test_wave_decoder.cpp - WAV decoder tests
// Part of audio-io-1.0.0

#include "audio_io/audio_reader.h"
#include <iostream>
#include <cmath>

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

// Validate audio format
bool validateFormat(const AudioFormat& format, 
                   int expectedRate, int expectedChannels, 
                   SampleFormat expectedFormat) {
    return format.sampleRateHz == expectedRate &&
           format.channelCount == expectedChannels &&
           format.sampleFormat == expectedFormat;
}

// Test: Load 16-bit stereo WAV
void test_load_16bit_stereo(TestResults& results) {
    AudioReader reader;
    
    auto result = reader.readFile(L"test_files/16bit_stereo_44100.wav");
    
    if (!result) {
        results.fail("Load 16-bit stereo WAV", result.error().message.c_str());
        return;
    }
    
    const AudioData& audio = *result;
    
    if (!validateFormat(audio.format, 44100, 2, SampleFormat::Int16)) {
        results.fail("Load 16-bit stereo WAV", "Format mismatch");
        return;
    }
    
    if (audio.samples.empty()) {
        results.fail("Load 16-bit stereo WAV", "No samples loaded");
        return;
    }
    
    // Check samples are in valid range [-1.0, 1.0]
    for (float sample : audio.samples) {
        if (std::abs(sample) > 1.0f) {
            results.fail("Load 16-bit stereo WAV", "Sample out of range");
            return;
        }
    }
    
    results.pass("Load 16-bit stereo WAV");
}

// Test: Load 24-bit mono WAV
void test_load_24bit_mono(TestResults& results) {
    AudioReader reader;
    
    auto result = reader.readFile(L"test_files/24bit_mono_48000.wav");
    
    if (!result) {
        results.fail("Load 24-bit mono WAV", result.error().message.c_str());
        return;
    }
    
    const AudioData& audio = *result;
    
    if (!validateFormat(audio.format, 48000, 1, SampleFormat::Int24)) {
        results.fail("Load 24-bit mono WAV", "Format mismatch");
        return;
    }
    
    results.pass("Load 24-bit mono WAV");
}

// Test: Load float32 WAV
void test_load_float32(TestResults& results) {
    AudioReader reader;
    
    auto result = reader.readFile(L"test_files/float32_stereo_48000.wav");
    
    if (!result) {
        results.fail("Load float32 WAV", result.error().message.c_str());
        return;
    }
    
    const AudioData& audio = *result;
    
    if (!validateFormat(audio.format, 48000, 2, SampleFormat::Float32)) {
        results.fail("Load float32 WAV", "Format mismatch");
        return;
    }
    
    results.pass("Load float32 WAV");
}

// Test: Format only (fast metadata read)
void test_format_only(TestResults& results) {
    AudioReader reader;
    
    auto result = reader.readFormat(L"test_files/16bit_stereo_44100.wav");
    
    if (!result) {
        results.fail("Read format only", result.error().message.c_str());
        return;
    }
    
    const AudioFormat& format = *result;
    
    if (!validateFormat(format, 44100, 2, SampleFormat::Int16)) {
        results.fail("Read format only", "Format mismatch");
        return;
    }
    
    results.pass("Read format only");
}

// Test: Invalid file
void test_invalid_file(TestResults& results) {
    AudioReader reader;
    
    auto result = reader.readFile(L"nonexistent.wav");
    
    if (result) {
        results.fail("Invalid file handling", "Should have failed");
        return;
    }
    
    results.pass("Invalid file handling");
}

// Test: Corrupt WAV
void test_corrupt_wav(TestResults& results) {
    AudioReader reader;
    
    auto result = reader.readFile(L"test_files/corrupt.wav");
    
    if (result) {
        results.fail("Corrupt WAV handling", "Should have failed");
        return;
    }
    
    results.pass("Corrupt WAV handling");
}

int main() {
    std::cout << "========================================\n";
    std::cout << "audio-io-1.0.0 WAV Decoder Tests\n";
    std::cout << "========================================\n\n";
    
    TestResults results;
    
    // Run tests
    test_load_16bit_stereo(results);
    test_load_24bit_mono(results);
    test_load_float32(results);
    test_format_only(results);
    test_invalid_file(results);
    test_corrupt_wav(results);
    
    // Summary
    results.summary();
    
    return (results.passedTests == results.totalTests) ? 0 : 1;
}
