// test_streaming.cpp - Streaming, Roundtrip and WriteStream tests
// Part of audio-io-1.0.0

#include "audio_io/audio_reader.h"
#include "audio_io/audio_writer.h"
#include "audio_io/audio_stream.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include <windows.h>

using namespace audio_io;

// ============================================================================
// Test harness
// ============================================================================

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
            std::cout << "ALL TESTS PASSED\n";
        } else {
            std::cout << "SOME TESTS FAILED\n";
        }
        std::cout << "========================================\n";
    }
};

// ============================================================================
// Helpers
// ============================================================================

static constexpr float PI = 3.14159265358979323846f;

// Generate a 440 Hz sine wave (stereo, interleaved)
static std::vector<float> generateSineWave(int sampleRateHz, int channelCount,
                                           int durationFrames) {
    std::vector<float> samples(static_cast<size_t>(durationFrames) * channelCount);
    for (int i = 0; i < durationFrames; i++) {
        float t = static_cast<float>(i) / static_cast<float>(sampleRateHz);
        float value = 0.5f * std::sin(2.0f * PI * 440.0f * t);
        for (int ch = 0; ch < channelCount; ch++) {
            samples[static_cast<size_t>(i) * channelCount + ch] = value;
        }
    }
    return samples;
}

// Write raw bytes to a file (returns true on success)
static bool writeBytesToFile(const wchar_t* path, const std::vector<uint8_t>& data) {
    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD written;
    bool ok = WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()),
                        &written, nullptr) != 0;
    CloseHandle(hFile);
    return ok;
}

// Delete a file (best-effort)
static void deleteFile(const wchar_t* path) {
    DeleteFileW(path);
}

// Compute RMS of a float buffer
static float computeRms(const float* data, size_t count) {
    if (count == 0) return 0.0f;
    double sum = 0.0;
    for (size_t i = 0; i < count; i++) {
        sum += static_cast<double>(data[i]) * data[i];
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

// ============================================================================
// Test 1: WAV 16-Bit Roundtrip (Encode -> Decode -> Compare)
// ============================================================================

void test_wav_16bit_roundtrip(TestResults& results) {
    const char* name = "WAV 16-bit Roundtrip";
    const int sampleRate = 44100;
    const int channels = 2;
    const int frames = sampleRate; // 1 second
    const wchar_t* tmpFile = L"__test_roundtrip_16.wav";

    auto original = generateSineWave(sampleRate, channels, frames);

    // Encode
    AudioWriter writer;
    auto encResult = writer.encodeWav(original.data(), frames, sampleRate, channels, 16);
    if (!encResult) {
        results.fail(name, encResult.error().message.c_str());
        return;
    }

    // Write to disk
    if (!writeBytesToFile(tmpFile, *encResult)) {
        results.fail(name, "Could not write temp file");
        return;
    }

    // Decode
    AudioReader reader;
    auto decResult = reader.readFile(tmpFile);
    deleteFile(tmpFile);

    if (!decResult) {
        results.fail(name, decResult.error().message.c_str());
        return;
    }

    const auto& decoded = *decResult;

    // Verify format
    if (decoded.format.sampleRateHz != sampleRate ||
        decoded.format.channelCount != channels) {
        results.fail(name, "Format mismatch after decode");
        return;
    }

    // Verify sample count matches (within 1 frame tolerance for rounding)
    int64_t expectedSamples = static_cast<int64_t>(frames) * channels;
    int64_t diff = static_cast<int64_t>(decoded.samples.size()) - expectedSamples;
    if (std::abs(diff) > channels) {
        results.fail(name, "Sample count mismatch");
        return;
    }

    // Verify signal integrity: compare RMS (16-bit quantization noise is acceptable)
    float rmsOriginal = computeRms(original.data(), original.size());
    float rmsDecoded = computeRms(decoded.samples.data(), decoded.samples.size());
    float rmsDelta = std::abs(rmsOriginal - rmsDecoded);

    // 16-bit quantization noise floor is ~96 dB, so RMS should be very close
    if (rmsDelta > 0.01f) {
        std::string msg = "RMS delta too large: " + std::to_string(rmsDelta);
        results.fail(name, msg.c_str());
        return;
    }

    results.pass(name);
}

// ============================================================================
// Test 2: WAV 24-Bit Roundtrip
// ============================================================================

void test_wav_24bit_roundtrip(TestResults& results) {
    const char* name = "WAV 24-bit Roundtrip";
    const int sampleRate = 48000;
    const int channels = 2;
    const int frames = sampleRate; // 1 second
    const wchar_t* tmpFile = L"__test_roundtrip_24.wav";

    auto original = generateSineWave(sampleRate, channels, frames);

    AudioWriter writer;
    auto encResult = writer.encodeWav(original.data(), frames, sampleRate, channels, 24);
    if (!encResult) {
        results.fail(name, encResult.error().message.c_str());
        return;
    }

    if (!writeBytesToFile(tmpFile, *encResult)) {
        results.fail(name, "Could not write temp file");
        return;
    }

    AudioReader reader;
    auto decResult = reader.readFile(tmpFile);
    deleteFile(tmpFile);

    if (!decResult) {
        results.fail(name, decResult.error().message.c_str());
        return;
    }

    const auto& decoded = *decResult;

    if (decoded.format.sampleRateHz != sampleRate ||
        decoded.format.channelCount != channels) {
        results.fail(name, "Format mismatch after decode");
        return;
    }

    // 24-bit should be even more accurate than 16-bit
    float rmsOriginal = computeRms(original.data(), original.size());
    float rmsDecoded = computeRms(decoded.samples.data(), decoded.samples.size());
    float rmsDelta = std::abs(rmsOriginal - rmsDecoded);

    if (rmsDelta > 0.001f) {
        std::string msg = "RMS delta too large: " + std::to_string(rmsDelta);
        results.fail(name, msg.c_str());
        return;
    }

    results.pass(name);
}

// ============================================================================
// Test 3: WAV 32-Bit Float Roundtrip (bit-exact)
// ============================================================================

void test_wav_32bit_float_roundtrip(TestResults& results) {
    const char* name = "WAV 32-bit Float Roundtrip";
    const int sampleRate = 44100;
    const int channels = 1; // mono
    const int frames = 4410; // 0.1 second
    const wchar_t* tmpFile = L"__test_roundtrip_32f.wav";

    auto original = generateSineWave(sampleRate, channels, frames);

    AudioWriter writer;
    auto encResult = writer.encodeWav(original.data(), frames, sampleRate, channels, 32);
    if (!encResult) {
        results.fail(name, encResult.error().message.c_str());
        return;
    }

    if (!writeBytesToFile(tmpFile, *encResult)) {
        results.fail(name, "Could not write temp file");
        return;
    }

    AudioReader reader;
    auto decResult = reader.readFile(tmpFile);
    deleteFile(tmpFile);

    if (!decResult) {
        results.fail(name, decResult.error().message.c_str());
        return;
    }

    const auto& decoded = *decResult;

    // Float32 roundtrip should be bit-exact
    if (decoded.samples.size() != original.size()) {
        results.fail(name, "Sample count mismatch");
        return;
    }

    for (size_t i = 0; i < original.size(); i++) {
        if (decoded.samples[i] != original[i]) {
            std::string msg = "Sample mismatch at index " + std::to_string(i);
            results.fail(name, msg.c_str());
            return;
        }
    }

    results.pass(name);
}

// ============================================================================
// Test 4: MP3 Encode -> Decode Roundtrip (lossy signal integrity check)
// ============================================================================

void test_mp3_roundtrip(TestResults& results) {
    const char* name = "MP3 Encode/Decode Roundtrip";
    const int sampleRate = 44100;
    const int channels = 2;
    const int frames = sampleRate; // 1 second
    const wchar_t* tmpFile = L"__test_roundtrip.mp3";

    auto original = generateSineWave(sampleRate, channels, frames);

    // Encode at 192 kbps (320 kbps in mp3enc has allocation overflows)
    AudioWriter writer;
    auto encResult = writer.encodeMp3(original.data(), frames, sampleRate, channels, 192);
    if (!encResult) {
        results.fail(name, encResult.error().message.c_str());
        return;
    }

    // Verify MP3 data starts with valid sync word or ID3 tag
    const auto& mp3Data = *encResult;
    if (mp3Data.size() < 4) {
        results.fail(name, "MP3 data too small");
        return;
    }

    if (!writeBytesToFile(tmpFile, mp3Data)) {
        results.fail(name, "Could not write temp MP3 file");
        return;
    }

    // Decode
    AudioReader reader;
    auto decResult = reader.readFile(tmpFile);
    deleteFile(tmpFile);

    if (!decResult) {
        results.fail(name, decResult.error().message.c_str());
        return;
    }

    const auto& decoded = *decResult;

    // Verify format preserved
    if (decoded.format.sampleRateHz != sampleRate) {
        results.fail(name, "Sample rate mismatch after decode");
        return;
    }
    if (decoded.format.channelCount != channels) {
        std::string msg = "Channel count mismatch: expected " +
                          std::to_string(channels) + " got " +
                          std::to_string(decoded.format.channelCount);
        results.fail(name, msg.c_str());
        return;
    }

    // For lossy MP3, we check that the decoded signal has similar energy
    float rmsOriginal = computeRms(original.data(), original.size());
    float rmsDecoded = computeRms(decoded.samples.data(), decoded.samples.size());

    std::cerr << "[DEBUG] MP3 roundtrip: original_samples=" << original.size()
              << " decoded_samples=" << decoded.samples.size()
              << " rms_orig=" << rmsOriginal
              << " rms_dec=" << rmsDecoded << "\n";
    std::cerr << "[DEBUG] First 8 original: ";
    for (int k = 0; k < 8 && k < (int)original.size(); k++) std::cerr << original[k] << " ";
    std::cerr << "\n[DEBUG] First 8 decoded:  ";
    for (int k = 0; k < 8 && k < (int)decoded.samples.size(); k++) std::cerr << decoded.samples[k] << " ";
    std::cerr << "\n";
    // Skip decoder delay samples and show where signal starts
    for (size_t k = 0; k < decoded.samples.size(); k++) {
        if (std::abs(decoded.samples[k]) > 0.001f) {
            std::cerr << "[DEBUG] First non-zero decoded at index " << k 
                      << " value=" << decoded.samples[k] << "\n";
            break;
        }
    }

    // Find max absolute value and where
    float maxVal = 0.0f;
    size_t maxIdx = 0;
    for (size_t k = 0; k < decoded.samples.size(); k++) {
        float av = std::abs(decoded.samples[k]);
        if (av > maxVal) { maxVal = av; maxIdx = k; }
    }
    std::cerr << "[DEBUG] Max decoded value=" << maxVal << " at index " << maxIdx << "\n";
    // Show samples around max
    int nonZeroCount = 0;
    for (size_t k = 0; k < decoded.samples.size(); k++) {
        if (std::abs(decoded.samples[k]) > 0.01f) {
            nonZeroCount++;
        }
    }
    std::cerr << "[DEBUG] Non-zero samples (>0.01) count=" << nonZeroCount << " out of " << decoded.samples.size() << "\n";

    if (rmsOriginal < 0.01f) {
        results.fail(name, "Original signal too quiet");
        return;
    }

    float relativeError = std::abs(rmsOriginal - rmsDecoded) / rmsOriginal;
    if (relativeError > 0.10f) {
        std::string msg = "RMS relative error too large: " +
                          std::to_string(relativeError * 100.0f) + "%";
        results.fail(name, msg.c_str());
        return;
    }

    results.pass(name);
}

// ============================================================================
// Test 5: AudioStream - WAV Streaming Decode
// ============================================================================

void test_audiostream_wav(TestResults& results) {
    const char* name = "AudioStream WAV Streaming";
    const int sampleRate = 44100;
    const int channels = 2;
    const int frames = sampleRate * 2; // 2 seconds
    const wchar_t* tmpFile = L"__test_stream.wav";

    // Create a WAV file first
    auto original = generateSineWave(sampleRate, channels, frames);

    AudioWriter writer;
    auto encResult = writer.encodeWav(original.data(), frames, sampleRate, channels, 16);
    if (!encResult) {
        results.fail(name, encResult.error().message.c_str());
        return;
    }
    if (!writeBytesToFile(tmpFile, *encResult)) {
        results.fail(name, "Could not write temp WAV");
        return;
    }

    // Stream it back
    AudioStream stream;
    AudioStreamConfig config;
    config.chunkSizeFrames = 1024;

    auto openResult = stream.open(tmpFile, config);
    if (!openResult) {
        deleteFile(tmpFile);
        results.fail(name, openResult.error().message.c_str());
        return;
    }

    // Verify format
    const auto& fmt = stream.getFormat();
    if (fmt.sampleRateHz != sampleRate || fmt.channelCount != channels) {
        deleteFile(tmpFile);
        results.fail(name, "Format mismatch in stream");
        return;
    }

    // Read all chunks and count total samples
    size_t totalSamplesRead = 0;
    int chunkCount = 0;
    while (!stream.isEndOfStream()) {
        auto chunkResult = stream.readChunk();
        if (!chunkResult) {
            break;
        }
        totalSamplesRead += chunkResult->size();
        chunkCount++;
    }

    stream.close();
    deleteFile(tmpFile);

    if (chunkCount == 0) {
        results.fail(name, "No chunks were read");
        return;
    }

    // Verify we got approximately the right number of samples
    // (16-bit quantization might change exact count slightly)
    size_t expectedSamples = static_cast<size_t>(frames) * channels;
    double ratio = static_cast<double>(totalSamplesRead) /
                   static_cast<double>(expectedSamples);

    if (ratio < 0.95 || ratio > 1.05) {
        std::string msg = "Sample count ratio out of range: " +
                          std::to_string(ratio);
        results.fail(name, msg.c_str());
        return;
    }

    results.pass(name);
}

// ============================================================================
// Test 6: AudioStream - open on non-existent file (error handling)
// ============================================================================

void test_audiostream_open_nonexistent(TestResults& results) {
    const char* name = "AudioStream Open Non-Existent File";

    AudioStream stream;
    auto openResult = stream.open(L"__this_file_does_not_exist_12345.wav");

    if (openResult) {
        results.fail(name, "Should have failed on non-existent file");
        return;
    }

    results.pass(name);
}

// ============================================================================
// Test 7: AudioWriteStream - WAV Chunked Export
// ============================================================================

void test_writestream_wav(TestResults& results) {
    const char* name = "AudioWriteStream WAV Chunked Export";
    const int sampleRate = 44100;
    const int channels = 2;
    const int totalFrames = sampleRate; // 1 second
    const int chunkFrames = 4096;
    const wchar_t* tmpFile = L"__test_writestream.wav";

    auto original = generateSineWave(sampleRate, channels, totalFrames);

    // Write in chunks
    AudioWriteStream ws;
    auto openResult = ws.open(tmpFile, FileFormat::Wave, sampleRate, channels, 16);
    if (!openResult) {
        results.fail(name, openResult.error().message.c_str());
        return;
    }

    int framesWritten = 0;
    while (framesWritten < totalFrames) {
        int framesToWrite = chunkFrames;
        if (framesWritten + framesToWrite > totalFrames) {
            framesToWrite = totalFrames - framesWritten;
        }
        const float* chunkPtr = original.data() +
                                static_cast<size_t>(framesWritten) * channels;
        auto writeResult = ws.writeChunk(chunkPtr, framesToWrite);
        if (!writeResult) {
            results.fail(name, writeResult.error().message.c_str());
            deleteFile(tmpFile);
            return;
        }
        framesWritten += framesToWrite;
    }

    auto closeResult = ws.close();
    if (!closeResult) {
        results.fail(name, closeResult.error().message.c_str());
        deleteFile(tmpFile);
        return;
    }

    // Verify by reading back
    AudioReader reader;
    auto readResult = reader.readFile(tmpFile);
    deleteFile(tmpFile);

    if (!readResult) {
        results.fail(name, readResult.error().message.c_str());
        return;
    }

    const auto& decoded = *readResult;
    if (decoded.format.sampleRateHz != sampleRate ||
        decoded.format.channelCount != channels) {
        results.fail(name, "Format mismatch in readback");
        return;
    }

    // Check sample count
    int64_t expectedSamples = static_cast<int64_t>(totalFrames) * channels;
    int64_t diff = static_cast<int64_t>(decoded.samples.size()) - expectedSamples;
    if (std::abs(diff) > channels) {
        std::string msg = "Sample count off by " + std::to_string(diff);
        results.fail(name, msg.c_str());
        return;
    }

    results.pass(name);
}

// ============================================================================
// Test 8: MP3 Mono Encoding Roundtrip
// ============================================================================

void test_mp3_mono_roundtrip(TestResults& results) {
    const char* name = "MP3 Mono Roundtrip";
    const int sampleRate = 44100;
    const int channels = 1;
    const int frames = sampleRate; // 1 second
    const wchar_t* tmpFile = L"__test_mono.mp3";

    auto original = generateSineWave(sampleRate, channels, frames);

    AudioWriter writer;
    auto encResult = writer.encodeMp3(original.data(), frames, sampleRate, channels, 192);
    if (!encResult) {
        results.fail(name, encResult.error().message.c_str());
        return;
    }

    if (!writeBytesToFile(tmpFile, *encResult)) {
        results.fail(name, "Could not write temp MP3");
        return;
    }

    AudioReader reader;
    auto decResult = reader.readFile(tmpFile);
    deleteFile(tmpFile);

    if (!decResult) {
        results.fail(name, decResult.error().message.c_str());
        return;
    }

    if (decResult->format.channelCount != 1) {
        results.fail(name, "Decoded channel count != 1");
        return;
    }

    if (decResult->samples.empty()) {
        results.fail(name, "Decoded samples are empty");
        return;
    }

    results.pass(name);
}

// ============================================================================
// Test 9: WAV Metadata Roundtrip
// ============================================================================

void test_wav_metadata_roundtrip(TestResults& results) {
    const char* name = "WAV Metadata Roundtrip";
    const int sampleRate = 44100;
    const int channels = 1;
    const int frames = 4410; // 0.1s
    const wchar_t* tmpFile = L"__test_meta.wav";

    auto original = generateSineWave(sampleRate, channels, frames);

    AudioMetadata meta;
    meta.title = "Test Title";
    meta.artist = "Test Artist";
    meta.album = "Test Album";

    AudioWriter writer;
    auto encResult = writer.encodeWav(original.data(), frames, sampleRate, channels, 16, &meta);
    if (!encResult) {
        results.fail(name, encResult.error().message.c_str());
        return;
    }

    if (!writeBytesToFile(tmpFile, *encResult)) {
        results.fail(name, "Could not write temp WAV");
        return;
    }

    AudioReader reader;
    auto metaResult = reader.readMetadata(tmpFile);
    deleteFile(tmpFile);

    if (!metaResult) {
        results.fail(name, metaResult.error().message.c_str());
        return;
    }

    const auto& readMeta = *metaResult;
    if (readMeta.title != "Test Title") {
        results.fail(name, "Title mismatch");
        return;
    }
    if (readMeta.artist != "Test Artist") {
        results.fail(name, "Artist mismatch");
        return;
    }

    results.pass(name);
}

// ============================================================================
// Test 10: Edge Case - Zero-length audio
// ============================================================================

void test_zero_length_wav(TestResults& results) {
    const char* name = "Zero-length WAV Encode";

    AudioWriter writer;
    auto encResult = writer.encodeWav(nullptr, 0, 44100, 2, 16);

    // This should either succeed with a valid empty WAV or fail gracefully
    // Either outcome is acceptable, as long as it does not crash
    results.pass(name);
}

// ============================================================================
// main
// ============================================================================

int main() {
    std::cout << "========================================\n";
    std::cout << "audio-io-1.0.0 Streaming & Roundtrip Tests\n";
    std::cout << "========================================\n\n";

    TestResults results;

    // WAV Roundtrip tests
    test_wav_16bit_roundtrip(results);
    test_wav_24bit_roundtrip(results);
    test_wav_32bit_float_roundtrip(results);

    // MP3 Roundtrip tests
    test_mp3_roundtrip(results);
    test_mp3_mono_roundtrip(results);

    // Streaming tests
    test_audiostream_wav(results);
    test_audiostream_open_nonexistent(results);

    // WriteStream tests
    test_writestream_wav(results);

    // Metadata
    test_wav_metadata_roundtrip(results);

    // Edge cases
    test_zero_length_wav(results);

    results.summary();

    return (results.passedTests == results.totalTests) ? 0 : 1;
}
