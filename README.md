# Audio-IO Library

[![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)](https://github.com/KernelMeltdown/audio-io)
[![License](https://img.shields.io/badge/license-Proprietary-red.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C++-20-orange.svg)](https://isocpp.org/)
[![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)](https://www.microsoft.com/windows)
[![Free for Non-Commercial](https://img.shields.io/badge/free-non--commercial-brightgreen.svg)](LICENSE)

Audio file I/O library for Windows. Read and write WAV and MP3 files with C++20 API and native Windows integration.

## Features

- 🎵 **WAV Support** - Read/Write PCM WAV files (16-bit, 24-bit, 32-bit float)
- 🎧 **MP3 Encoding** - MPEG-1 Audio Layer III encoding (VBR/CBR, 32-320 kbps)
- 🎼 **MP3 Decoding** - Native MPEG-1/2/2.5 Layer III decoder
- 🔄 **Streaming** - Chunk-based AudioStream for large files with low memory usage
- 🎚️ **Resampling** - AVX2-optimized Sinc resampler (on-the-fly in AudioStream)
- 📊 **Metadata** - ID3v2 tags (MP3) and RIFF INFO/BEXT chunks (WAV)
- 🚀 **Modern C++20** - Expected<T,E>, move semantics, no exceptions
- 🏗️ **DLL Architecture** - Shared library design
- 💯 **Zero Dependencies** - Pure Windows API and self-contained codecs

## Requirements

- **CMake**: 3.20 or higher
- **Compiler**: C++20 support required
  - MSVC 2019 16.11+ (Visual Studio 2019)
  - MSVC 2022 (recommended)
- **Platform**: Windows 10 or higher
- **Windows SDK**: 10.0 or higher

### Dependencies
- `kernel32.lib` - File I/O (CreateFileW, ReadFile, WriteFile)

## Quick Start

### Building

```bash
# Using the provided build script
build.bat

# Or manually
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
cmake --install . --prefix ../install
```

### Reading Audio Files

```cpp
#include <audio_io/audio_reader.h>
#include <iostream>

using namespace audio_io;

int main() {
    AudioReader reader;
    
    // Read WAV file
    auto result = reader.readFile(L"music.wav");
    
    if (!result) {
        std::cerr << "Error: " << result.error().message << "\n";
        return 1;
    }
    
    const AudioData& audio = *result;
    
    std::cout << "Sample Rate: " << audio.format.sampleRateHz << " Hz\n";
    std::cout << "Channels: " << audio.format.channelCount << "\n";
    std::cout << "Samples: " << audio.samples.size() << "\n";
    std::cout << "Duration: " << (audio.samples.size() / audio.format.channelCount) 
              / (float)audio.format.sampleRateHz << " seconds\n";
    
    // Samples are normalized floats [-1.0, 1.0]
    // Interleaved: L,R,L,R,... for stereo
    
    return 0;
}
```

### Writing MP3 Files

```cpp
#include <audio_io/audio_writer.h>
#include <vector>
#include <cmath>

using namespace audio_io;

int main() {
    // Generate 1 second 440 Hz sine wave (stereo)
    const int sampleRate = 44100;
    const int channels = 2;
    const int duration = 1;
    
    std::vector<float> samples(sampleRate * duration * channels);
    
    for (int i = 0; i < sampleRate * duration; i++) {
        float t = static_cast<float>(i) / sampleRate;
        float value = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * t);
        samples[i * 2 + 0] = value; // Left
        samples[i * 2 + 1] = value; // Right
    }
    
    // Encode to MP3 at 320 kbps
    AudioWriter writer;
    auto mp3Result = writer.encodeMp3(
        samples.data(),
        sampleRate * duration,  // frame count
        sampleRate,
        channels,
        320  // bitrate in kbps
    );
    
    if (!mp3Result) {
        std::cerr << "Encoding failed: " << mp3Result.error().message << "\n";
        return 1;
    }
    
    // Write MP3 bytes to file
    const auto& mp3Data = *mp3Result;
    
    HANDLE hFile = CreateFileW(L"output.mp3", GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, mp3Data.data(), mp3Data.size(), &written, nullptr);
        CloseHandle(hFile);
    }
    
    return 0;
}
```

### Writing WAV Files

```cpp
#include <audio_io/audio_writer.h>

using namespace audio_io;

int main() {
    AudioReader reader;
    auto audioResult = reader.readFile(L"input.mp3");
    
    if (!audioResult) return 1;
    
    // Convert MP3 to WAV
    AudioWriter writer;
    auto wavResult = writer.encodeWav(
        *audioResult,
        16  // bit depth (16, 24, or 32)
    );
    
    if (!wavResult) return 1;
    
    // Write WAV bytes to file
    const auto& wavData = *wavResult;
    
    HANDLE hFile = CreateFileW(L"output.wav", GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, wavData.data(), wavData.size(), &written, nullptr);
        CloseHandle(hFile);
    }
    
    return 0;
}
```

### Streaming and Resampling (AudioStream)

```cpp
#include <audio_io/audio_stream.h>
#include <iostream>

using namespace audio_io;

int main() {
    AudioStream stream;
    AudioStreamConfig config;
    config.targetSampleRateHz = 48000; // On-the-fly Sinc Resampling
    config.chunkSizeFrames = 4096;
    
    if (!stream.open(L"large_audio.mp3", config)) {
        return 1;
    }
    
    while (!stream.isEndOfStream()) {
        auto chunk = stream.readChunk();
        if (chunk && !chunk->empty()) {
            // Process 4096 frames of audio data (resampled to 48kHz)
            const float* samples = chunk->data();
        }
    }
    
    return 0;
}
```

### Chunked File Writing (AudioWriteStream)

```cpp
#include <audio_io/audio_writer.h>

using namespace audio_io;

int main() {
    AudioWriteStream writer;
    
    // Open a 24-bit WAV file for streaming writes
    if (writer.open(L"output.wav", FileFormat::Wav, 48000, 2, 24)) {
        // Write chunks in a loop
        // writer.writeChunk(pcmData, frameCount);
        
        // Finalizes headers (e.g. RIFF chunk sizes, gapless tags)
        writer.close();
    }
    return 0;
}
```

## API Reference

### AudioReader Class

#### Read Audio Data
```cpp
Expected<AudioData, AudioError> readFile(const wchar_t* filePath);
Expected<AudioData, AudioError> readFile(const std::wstring& filePath);
```
Loads entire audio file into memory. Supports WAV and MP3.

#### Read Format Only (Fast)
```cpp
Expected<AudioFormat, AudioError> readFormat(const wchar_t* filePath);
```
Reads only format metadata without loading samples. Fast for getting duration/sample rate.

#### Read Metadata Tags
```cpp
Expected<AudioMetadata, AudioError> readMetadata(const wchar_t* filePath);
```
Reads ID3v2 tags (MP3) or RIFF INFO chunks (WAV).

### AudioWriter Class

#### Encode MP3
```cpp
Expected<std::vector<uint8_t>, AudioError> encodeMp3(
    const float* pcmSamples,
    int64_t sampleFrameCount,
    int sampleRateHz,           // 32000, 44100, or 48000 Hz
    int channelCount,            // 1 (mono) or 2 (stereo)
    int bitrateKbps = 320        // 128, 192, 256, 320 kbps (VBR average)
);
```
Encodes PCM float samples to MP3 format.

#### Encode WAV
```cpp
Expected<std::vector<uint8_t>, AudioError> encodeWav(
    const float* pcmSamples,
    int64_t sampleFrameCount,
    int sampleRateHz,
    int channelCount,
    int bitDepth = 16            // 16, 24, or 32 bits
);
```
Encodes PCM float samples to WAV format.

#### Write File
```cpp
Expected<bool, AudioError> writeFile(
    const wchar_t* filePath,
    const AudioData& audioData,
    int quality = 320            // MP3: kbps, WAV: bit depth
);
```
Writes audio data to file. Format detected from file extension.

### Data Structures

#### AudioData
```cpp
struct AudioData {
    std::vector<float> samples;  // Normalized [-1.0, 1.0], interleaved
    AudioFormat format;
};
```

#### AudioFormat
```cpp
struct AudioFormat {
    int sampleRateHz;            // Sample rate (e.g., 44100)
    int channelCount;            // 1 = mono, 2 = stereo
    SampleFormat sampleFormat;   // Int16, Int24, Float32
    FileFormat fileFormat;       // WAV, MP3
};
```

#### AudioMetadata
```cpp
struct AudioMetadata {
    std::wstring title;
    std::wstring artist;
    std::wstring album;
    std::wstring year;
    std::wstring genre;
    std::wstring comment;
};
```

#### AudioError
```cpp
struct AudioError {
    std::string message;
    int systemErrorCode;         // Windows GetLastError() or 0
};
```

## Supported Formats

### WAV (Read/Write)
- **PCM 16-bit**: CD quality, most common
- **PCM 24-bit**: Professional audio
- **IEEE Float 32-bit**: DAW standard
- **Sample Rates**: Any (typically 44.1 kHz, 48 kHz)
- **Channels**: Mono, Stereo

### MP3 (Read/Write)
- **Encoding**: MPEG-1 Audio Layer III
- **Bitrates**: 32-320 kbps (CBR), adaptive VBR
- **Sample Rates**: 32 kHz, 44.1 kHz, 48 kHz
- **Channels**: Mono, Stereo (Joint Stereo M/S)
- **Gapless Playback**: XING/Info header with encoder delay/padding
- **Decoder**: Native MPEG-1/2/2.5 Layer III implementation
- **Encoder**: Native implementation with psychoacoustic model

## CMake Integration

```cmake
# In your CMakeLists.txt
add_subdirectory(third-party-cpp/audio-io-1.0.0)

add_executable(your_audio_app main.cpp)
target_link_libraries(your_audio_app PRIVATE audio-io)
```

## Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `AUDIO_IO_BUILD_TESTS` | `ON` | Build test programs |

Build without tests:
```bash
cmake -DAUDIO_IO_BUILD_TESTS=OFF ..
```

## Examples

### Convert MP3 to WAV

```cpp
#include <audio_io/audio_reader.h>
#include <audio_io/audio_writer.h>

int main() {
    audio_io::AudioReader reader;
    audio_io::AudioWriter writer;
    
    // Read MP3
    auto audioResult = reader.readFile(L"input.mp3");
    if (!audioResult) return 1;
    
    // Write as 16-bit WAV
    writer.writeFile(L"output.wav", *audioResult, 16);
    
    return 0;
}
```

### Mix Two Audio Files

```cpp
#include <audio_io/audio_reader.h>
#include <audio_io/audio_writer.h>
#include <algorithm>

int main() {
    audio_io::AudioReader reader;
    
    auto audio1 = reader.readFile(L"track1.wav");
    auto audio2 = reader.readFile(L"track2.wav");
    
    if (!audio1 || !audio2) return 1;
    
    // Mix (simple addition, should normalize after)
    size_t minSize = std::min(audio1->samples.size(), audio2->samples.size());
    
    for (size_t i = 0; i < minSize; i++) {
        audio1->samples[i] = (audio1->samples[i] + audio2->samples[i]) * 0.5f;
    }
    
    // Save mixed audio as MP3
    audio_io::AudioWriter writer;
    writer.writeFile(L"mixed.mp3", *audio1, 320);
    
    return 0;
}
```

### Extract Audio Segment

```cpp
#include <audio_io/audio_reader.h>
#include <audio_io/audio_writer.h>

int main() {
    audio_io::AudioReader reader;
    auto audioResult = reader.readFile(L"long_track.mp3");
    
    if (!audioResult) return 1;
    
    audio_io::AudioData& audio = *audioResult;
    
    // Extract 5 seconds starting at 10 seconds
    int startSample = 10 * audio.format.sampleRateHz * audio.format.channelCount;
    int endSample = 15 * audio.format.sampleRateHz * audio.format.channelCount;
    
    audio_io::AudioData segment;
    segment.format = audio.format;
    segment.samples = std::vector<float>(
        audio.samples.begin() + startSample,
        audio.samples.begin() + endSample
    );
    
    // Save segment
    audio_io::AudioWriter writer;
    writer.writeFile(L"segment.mp3", segment, 256);
    
    return 0;
}
```

## Project Structure

```
audio-io-1.0.0/
├── include/
│   └── audio_io/
│       ├── audio_format.h       # Format definitions
│       ├── audio_reader.h       # Reading API
│       ├── audio_writer.h       # Writing/encoding API
│       ├── audio_metadata.h     # Metadata structures
│       ├── audio_stream.h       # Streaming support
│       ├── expected.h           # Expected<T,E> type
│       └── export.h             # DLL export macros
├── src/
│   ├── audio_reader.cpp
│   ├── audio_writer.cpp
│   ├── audio_stream.cpp
│   ├── io/
│   │   ├── file_reader.h        # File I/O utility
│   │   └── file_reader.cpp
│   ├── resampler/
│   │   ├── sinc_resampler.h     # AVX2 Sinc resampler
│   │   └── sinc_resampler.cpp
│   ├── metadata/
│   │   ├── id3v2_reader.cpp     # MP3 ID3v2 tags
│   │   ├── riff_metadata_reader.cpp  # WAV INFO/BEXT
│   │   ├── id3v2_writer.cpp
│   │   └── riff_info_writer.cpp
│   ├── wave/
│   │   └── wave_decoder.cpp     # WAV decoder
│   └── mp3/
│       ├── mp3_codec.cpp        # MP3 encoder/decoder
│       └── encoder/
│           ├── mp3enc.h          # Encoder core
│           ├── mp3enc-psy.h      # Psychoacoustic model
│           ├── mp3enc-quant.h    # Quantization / outer loop
│           └── mp3enc-huff.h     # Huffman table selection
├── tests/
│   ├── test_mp3_encoder.cpp
│   └── test_wave_decoder.cpp
├── build.bat
├── CMakeLists.txt
├── LICENSE
└── README.md
```

## Performance & Audio Quality

We continuously track the performance of the audio-io codecs through our automated `audio-io-benchmark` tool and the Python evaluation suite (`tools/eval_suite.py`). The following metrics are measured on a modern CPU processing 5 minutes of a highly complex stereo audio "torture test" (44.1kHz, 2 channels, containing sweeps, impulses, and noise):

### Speed (I/O & Codecs)
- **WAV Reading**: Memory-mapped I/O, ~500 MB/s
- **WAV Writing**: Buffered writes, ~300 MB/s
- **MP3 Encoding**: **~8x - 12x realtime** (320 kbps, stereo)
- **MP3 Decoding**: **~450x - 1300x realtime** (Native decoder, highly optimized)

### Objective Quality Metrics (MP3 320 kbps)
*Compared against the uncompressed PCM reference signal.*
- **Segmental SNR (SegSNR)**: **~17.4 dB** (Solid performance. The encoder preserves transients and basic waveforms well).
- **Log-Spectral Distance (LSD)**: **~33.7 dB** (Basic Psychoacoustic Model. The encoder uses an aggressive low-pass filter to save bits, resulting in a loss of high frequencies above ~15kHz).

> **Honest Assessment:** 
> From an engineering standpoint, this is a highly stable, exception-free, and incredibly fast C++20 MP3 codec written from scratch. However, the psychoacoustic tuning is currently at a "basic/prototype" level. It cannot yet compete with the audio quality of 25-year tuned encoders like LAME, particularly in the high-frequency spectrum.

## Best Practices

### Sample Format
- Always normalize to [-1.0, 1.0] range
- Samples are interleaved: L,R,L,R for stereo
- Use `float` for processing, convert to int only for storage

### Error Handling
```cpp
auto result = reader.readFile(L"audio.mp3");
if (!result) {
    // Handle error
    std::cerr << "Error: " << result.error().message << "\n";
    std::cerr << "System Error: " << result.error().systemErrorCode << "\n";
    return;
}

// Use result
const AudioData& audio = *result;
```

### Memory Management
- `AudioData` owns its sample buffer
- Move semantics supported for efficient transfers
- Large files (>100 MB) will consume significant RAM

### MP3 Encoding Quality
- **VBR**: Adaptive bitrate based on signal complexity (recommended)
- **128 kbps**: Acceptable for voice
- **192 kbps**: Good for music
- **256 kbps**: Very good quality
- **320 kbps**: Maximum quality, near-transparent

## Troubleshooting

### MP3 Encoding Fails
- Verify sample rate is 32000, 44100, or 48000 Hz
- Check channel count is 1 or 2
- Ensure bitrate is between 128-320 kbps

### WAV File Won't Load
- Verify file is valid PCM WAV
- Check for unsupported formats (compressed WAV, u-law, etc.)
- Ensure file path uses wide strings (wchar_t*)

### Samples Sound Distorted
- Check for clipping (values outside [-1.0, 1.0])
- Verify sample rate matches playback device
- Ensure channel interleaving is correct

## License

See [LICENSE](LICENSE) file for details.

**Free for non-commercial use. Commercial use requires a license.**

## Technical Details

### MP3 Encoder
Native C++ implementation (ISO/IEC 11172-3):
- Psychoacoustic Model 2 (MDCT-domain masking thresholds)
- MDCT long blocks (576 lines) and short blocks (192 lines, transient detection)
- Huffman encoding with exhaustive table selection
- Bit reservoir management
- M/S Joint Stereo with energy-based channel correlation
- VBR via Perceptual Entropy-driven bitrate adaptation
- XING/Info header for gapless playback

### MP3 Decoder
Native C++ implementation:
- MPEG-1/2/2.5 Layer III compliant
- Huffman decoding
- Inverse MDCT
- Synthesis filterbank

### Sinc Resampler
- Windowed-sinc FIR filter (Kaiser window)
- AVX2 vectorized inner loop (`_mm256_fmadd_ps`)
- Arbitrary sample rate conversion

