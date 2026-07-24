// mp3_codec.cpp - MPEG Layer III Audio Codec Implementation
// Part of audio-io-1.0.0
// Windows-native C++20 implementation
//
// Decodes MPEG-1/2/2.5 Layer III audio streams (ISO/IEC 11172-3, ISO/IEC 13818-3)
// Encodes PCM to MPEG-1 Layer III (128-320 kbps, 32/44.1/48 kHz)
// Native implementation using custom MPEG-1 Audio Layer III encoder/decoder

#define MPEG_LAYER3_IMPLEMENTATION
#define MPEG_FLOAT_OUTPUT
#include "mpeg_layer3_decoder_native.h"
#include "mp3_codec.h"
#include "encoder/mp3enc.h"
#include "audio_io/audio_format.h"
#include <vector>
#include <cstring>

#include "../io/file_reader.h"

namespace audio_io {
namespace mp3 {

// Read entire file into memory using Windows API
Expected<std::vector<uint8_t>, AudioError> Mp3Codec::readEntireFile(const wchar_t* filePathUtf16) {
    return io::FileReader::readEntireFile(filePathUtf16);
}

// Skip ID3v2 tag if present
size_t Mp3Codec::skipId3v2Tag(const uint8_t* fileData, size_t fileSize) {
    if (fileSize < 10) {
        return 0;  // Too small for ID3v2 tag
    }
    
    // Check for "ID3" signature
    if (fileData[0] != 'I' || fileData[1] != 'D' || fileData[2] != '3') {
        return 0;  // No ID3v2 tag
    }
    
    // Parse tag size (synchsafe integer)
    uint32_t tagSize = parseId3v2Size(fileData + 6);
    
    // Check for footer
    bool hasFooter = (fileData[5] & 0x10) != 0;
    size_t totalTagSize = 10 + tagSize + (hasFooter ? 10 : 0);
    
    return totalTagSize;
}

// Parse ID3v2 synchsafe integer (7 bits per byte)
uint32_t Mp3Codec::parseId3v2Size(const uint8_t* sizeBytes) {
    return ((sizeBytes[0] & 0x7F) << 21) |
           ((sizeBytes[1] & 0x7F) << 14) |
           ((sizeBytes[2] & 0x7F) << 7) |
            (sizeBytes[3] & 0x7F);
}

// Read format only (fast - first frame)
Expected<AudioFormat, AudioError> Mp3Codec::readFormat(const wchar_t* filePathUtf16) {
    // Read entire file
    auto fileDataResult = readEntireFile(filePathUtf16);
    if (!fileDataResult) {
        return makeUnexpected(fileDataResult.error());
    }
    
    const auto& fileData = *fileDataResult;
    
    // Skip ID3v2 tag
    size_t dataOffset = skipId3v2Tag(fileData.data(), fileData.size());
    
    // Initialize MPEG decoder
    MpegDecoderContext mp3Decoder;
    mpeg_decoder_initialize(&mp3Decoder);
    
    // Try to decode first frame to get format info
    MpegFrameInfo frameInfo;
    MpegSample pcmBuffer[MAX_MPEG_SAMPLES_PER_FRAME];
    
    int samplesDecoded = mpeg_decode_frame(
        &mp3Decoder,
        fileData.data() + dataOffset,
        static_cast<int>(fileData.size() - dataOffset),
        pcmBuffer,
        &frameInfo
    );
    
    if (samplesDecoded == 0 || frameInfo.frame_bytes == 0) {
        return makeUnexpected(AudioError{
            "Cannot decode MP3: Invalid or unsupported format",
            0
        });
    }
    
    // Build format structure
    AudioFormat format = {};
    format.fileFormat = FileFormat::Mp3;
    format.sampleRateHz = frameInfo.hz;
    format.channelCount = frameInfo.channels;
    format.sampleFormat = SampleFormat::Float32;  // MPEG decoder outputs float with MPEG_FLOAT_OUTPUT flag
    format.bitDepth = 32;  // Float32
    format.bitrateKbps = frameInfo.bitrate_kbps;
    format.isVbr = false;  // We'll detect VBR by scanning multiple frames later if needed
    
    // Estimate total frames (rough estimate)
    // For accurate count, we'd need to scan all frames
    size_t estimatedFrameCount = (fileData.size() - dataOffset) / frameInfo.frame_bytes;
    format.totalSampleFrames = static_cast<int64_t>(estimatedFrameCount * samplesDecoded / format.channelCount);
    
    return format;
}

// Decode entire MP3 file
Expected<AudioData, AudioError> Mp3Codec::decode(const wchar_t* filePathUtf16) {
    // Read entire file
    auto fileDataResult = readEntireFile(filePathUtf16);
    if (!fileDataResult) {
        return makeUnexpected(fileDataResult.error());
    }
    
    const auto& fileData = *fileDataResult;
    
    // Skip ID3v2 tag
    size_t dataOffset = skipId3v2Tag(fileData.data(), fileData.size());
    
    // Initialize MPEG decoder
    MpegDecoderContext mp3Decoder;
    mpeg_decoder_initialize(&mp3Decoder);
    
    // Allocate output buffer (dynamic estimate based on file size)
    // A 320kbps MP3 is 40KB/sec. So file_size * 8 / 320k = seconds.
    // 1 sec = 44100 * 2 = 88200 floats.
    // So roughly file_size * 2 floats. To be safe, reserve file_size * 4 floats.
    std::vector<float> pcmSamples;
    pcmSamples.reserve(fileData.size() * 4);
    
    // Decode all frames
    size_t currentOffset = dataOffset;
    MpegFrameInfo frameInfo;
    MpegSample pcmBuffer[MAX_MPEG_SAMPLES_PER_FRAME];
    
    AudioFormat format = {};
    bool formatInitialized = false;
    
    while (currentOffset < fileData.size()) {
        int samplesDecoded = mpeg_decode_frame(
            &mp3Decoder,
            fileData.data() + currentOffset,
            static_cast<int>(fileData.size() - currentOffset),
            pcmBuffer,
            &frameInfo
        );
        
        if (frameInfo.frame_bytes == 0) {
            // End of valid MP3 data
            break;
        }
        
        // Initialize format from first frame
        if (!formatInitialized) {
            format.fileFormat = FileFormat::Mp3;
            format.sampleRateHz = frameInfo.hz;
            format.channelCount = frameInfo.channels;
            format.sampleFormat = SampleFormat::Float32;
            format.bitDepth = 32;
            format.bitrateKbps = frameInfo.bitrate_kbps;
            format.isVbr = false;
            formatInitialized = true;
        }
        
        // Append decoded samples
        // IMPORTANT: samplesDecoded is samples PER CHANNEL, not total!
        if (samplesDecoded > 0) {
            const size_t totalSamples = samplesDecoded * frameInfo.channels;
            size_t oldSize = pcmSamples.size();
            pcmSamples.resize(oldSize + totalSamples);
            
            // MPEG decoder produces float samples directly
            std::memcpy(pcmSamples.data() + oldSize, pcmBuffer, totalSamples * sizeof(float));
        }
        
        currentOffset += frameInfo.frame_bytes;
    }
    
    if (pcmSamples.empty()) {
        return makeUnexpected(AudioError{
            "MP3 file contains no decodable audio data",
            0
        });
    }

    // Normalize decoder output to [-1.0, 1.0] range.
    {
        float maxAbs = 0.0f;
        for (size_t i = 0; i < pcmSamples.size(); i++) {
            float av = pcmSamples[i] > 0.0f ? pcmSamples[i] : -pcmSamples[i];
            if (av > maxAbs) maxAbs = av;
        }
        if (maxAbs > 2.0f) {
            constexpr float scale = 1.0f / 32768.0f;
            for (size_t i = 0; i < pcmSamples.size(); i++) {
                pcmSamples[i] *= scale;
            }
        }
    }
    
    // Set total frame count
    format.totalSampleFrames = static_cast<int64_t>(pcmSamples.size() / format.channelCount);
    
    // Return audio data
    AudioData audioData;
    audioData.samples = std::move(pcmSamples);
    audioData.format = format;
    
    return audioData;
}

// ============================================================================
// MP3 ENCODING IMPLEMENTATION
// ============================================================================

// Encode PCM to MP3
Expected<std::vector<uint8_t>, AudioError> Mp3Codec::encode(
    const float* pcmSamples,
    int64_t sampleFrameCount,
    int sampleRateHz,
    int channelCount,
    int bitrateKbps
) {
    // Validate input
    if (!pcmSamples || sampleFrameCount <= 0) {
        return makeUnexpected(AudioError{"Invalid PCM input data", 0});
    }
    
    if (channelCount != 1 && channelCount != 2) {
        return makeUnexpected(AudioError{"MP3 encoding only supports mono or stereo", 0});
    }
    
    if (sampleRateHz != 32000 && sampleRateHz != 44100 && sampleRateHz != 48000) {
        return makeUnexpected(AudioError{"MP3 encoding only supports 32000, 44100, or 48000 Hz", 0});
    }
    
    // Initialize encoder (C API)
    mp3enc_t* encoder = mp3enc_init(sampleRateHz, channelCount, bitrateKbps);
    if (!encoder) {
        return makeUnexpected(AudioError{"Failed to initialize MP3 encoder", 0});
    }
    
    std::vector<uint8_t> mp3Data;
    mp3Data.reserve(static_cast<size_t>(sampleFrameCount * bitrateKbps / 8 / sampleRateHz * 1000));
    
    // Convert interleaved to planar format (encoder expects planar)
    // Input: [L0, R0, L1, R1, ...] -> Output: [L0, L1, ...] [R0, R1, ...]
    std::vector<float> planarSamples(sampleFrameCount * channelCount);
    
    for (int64_t i = 0; i < sampleFrameCount; i++) {
        for (int ch = 0; ch < channelCount; ch++) {
            float s = pcmSamples[i * channelCount + ch];
            if (s >  1.0f) s =  1.0f;
            if (s < -1.0f) s = -1.0f;
            // Scale to int16 range: the analysis filterbank (mp3enc_enwindow)
            // and MDCT quantizer are calibrated for [-32768, 32768] input.
            planarSamples[ch * sampleFrameCount + i] = s * 32768.0f;
        }
    }
    
    // Encode all samples
    int mp3Size = 0;
    const uint8_t* mp3Frame = mp3enc_encode(encoder, planarSamples.data(), 
                                            static_cast<int>(sampleFrameCount), &mp3Size);
    
    if (mp3Frame && mp3Size > 0) {
        mp3Data.insert(mp3Data.end(), mp3Frame, mp3Frame + mp3Size);
    }
    
    // Flush remaining data
    int flushSize = 0;
    mp3Frame = mp3enc_flush(encoder, &flushSize);
    if (mp3Frame && flushSize > 0) {
        mp3Data.insert(mp3Data.end(), mp3Frame, mp3Frame + flushSize);
    }
    

    // Cleanup encoder
    mp3enc_free(encoder);
    
    // ====================================================================
    // XING/Info Frame for Gapless Playback
    // ====================================================================
    // ISO 11172-3: MPEG1 Layer III encoder always adds 576 samples of
    // encoder delay at the start. The last frame is zero-padded so the
    // total encoded samples = N * 1152 (where N = frame count).
    // The "Info" tag (CBR variant of XING) tells decoders how many
    // samples to skip at start (encoder delay) and end (padding).
    //
    // Reference: VBR Info Tag Specification
    //            ISO 11172-3 clause 2.4.3.4 (granule structure)
    // ====================================================================
    
    if (!mp3Data.empty()) {
        // Count audio frames by scanning for sync words
        int frameCount = 0;
        size_t pos = 0;
        while (pos + 4 <= mp3Data.size()) {
            if (mp3Data[pos] == 0xFF && (mp3Data[pos + 1] & 0xE0) == 0xE0) {
                frameCount++;
                // Compute frame size from header to skip to next frame
                int brIdx = (mp3Data[pos + 2] >> 4) & 0x0F;
                int srIdx = (mp3Data[pos + 2] >> 2) & 0x03;
                int pad   = (mp3Data[pos + 2] >> 1) & 0x01;
                if (brIdx > 0 && brIdx < 15 && srIdx < 3) {
                    static const int brTable[] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
                    static const int srTable[] = {44100, 48000, 32000};
                    int fsize = 144 * brTable[brIdx] * 1000 / srTable[srIdx] + pad;
                    pos += fsize;
                    continue;
                }
            }
            pos++;
        }
        
        // Build the Info frame
        // Frame structure: [4-byte MP3 header][side info][Info tag payload][zero padding]
        int sideInfoBytes = (channelCount == 1) ? 17 : 32;
        int infoFrameSize = 144 * bitrateKbps * 1000 / sampleRateHz; // no padding
        
        std::vector<uint8_t> infoFrame(infoFrameSize, 0);
        
        // Write a valid MPEG1 Layer III frame header
        // syncword=0xFFF, ID=1 (MPEG1), layer=01 (III), protection=1, bitrate, sr, no padding
        int brIdx = 0;
        {
            static const int brTable[] = {0,32,40,48,56,64,80,96,112,128,160,192,224,256,320};
            for (int i = 1; i < 15; i++) {
                if (brTable[i] == bitrateKbps) { brIdx = i; break; }
            }
        }
        int srIdx = (sampleRateHz == 44100) ? 0 : (sampleRateHz == 48000) ? 1 : 2;
        int mode  = (channelCount == 1) ? 3 : 1; // mono or joint stereo
        
        infoFrame[0] = 0xFF;
        infoFrame[1] = 0xFB; // sync + MPEG1 + Layer III + no CRC
        infoFrame[2] = static_cast<uint8_t>((brIdx << 4) | (srIdx << 2)); // no padding, no private
        infoFrame[3] = static_cast<uint8_t>((mode << 6) | 0x04); // mode + original=1
        
        // Side info is all zeros (no audio data, main_data_begin=0)
        // Info tag starts right after header + side info
        int tagOffset = 4 + sideInfoBytes;
        
        // "Info" tag (4 bytes) - CBR marker (XING decoders also recognize this)
        infoFrame[tagOffset + 0] = 'I';
        infoFrame[tagOffset + 1] = 'n';
        infoFrame[tagOffset + 2] = 'f';
        infoFrame[tagOffset + 3] = 'o';
        
        // Flags (4 bytes, big-endian): bit 0 = frames present, bit 1 = bytes present
        uint32_t flags = 0x03; // frames + bytes
        infoFrame[tagOffset + 4] = 0;
        infoFrame[tagOffset + 5] = 0;
        infoFrame[tagOffset + 6] = 0;
        infoFrame[tagOffset + 7] = static_cast<uint8_t>(flags);
        
        // Total frame count (4 bytes, big-endian) - includes the info frame itself
        int totalFrames = frameCount + 1;
        infoFrame[tagOffset + 8]  = static_cast<uint8_t>((totalFrames >> 24) & 0xFF);
        infoFrame[tagOffset + 9]  = static_cast<uint8_t>((totalFrames >> 16) & 0xFF);
        infoFrame[tagOffset + 10] = static_cast<uint8_t>((totalFrames >> 8) & 0xFF);
        infoFrame[tagOffset + 11] = static_cast<uint8_t>(totalFrames & 0xFF);
        
        // Total byte count (4 bytes, big-endian) - includes info frame
        uint32_t totalBytes = static_cast<uint32_t>(mp3Data.size() + infoFrameSize);
        infoFrame[tagOffset + 12] = static_cast<uint8_t>((totalBytes >> 24) & 0xFF);
        infoFrame[tagOffset + 13] = static_cast<uint8_t>((totalBytes >> 16) & 0xFF);
        infoFrame[tagOffset + 14] = static_cast<uint8_t>((totalBytes >> 8) & 0xFF);
        infoFrame[tagOffset + 15] = static_cast<uint8_t>(totalBytes & 0xFF);
        
        // Encoder delay + padding (VBR Info Tag extension at offset tagOffset + 141)
        // MPEG1 Layer III always has 576 samples encoder delay.
        // Padding = (frameCount * 1152) - sampleFrameCount - 576
        int encoderDelay = 576;
        int totalEncoded = frameCount * 1152;
        int endPadding   = totalEncoded - static_cast<int>(sampleFrameCount) - encoderDelay;
        if (endPadding < 0) endPadding = 0;
        if (endPadding > 4095) endPadding = 4095; // 12-bit field
        if (encoderDelay > 4095) encoderDelay = 4095;
        
        // Delay/padding packed as 12+12 bits = 3 bytes at a standard VBR Info Tag offset
        // Offset from tag start: 141 bytes
        int dpOffset = tagOffset + 141;
        if (dpOffset + 3 <= infoFrameSize) {
            infoFrame[dpOffset + 0] = static_cast<uint8_t>((encoderDelay >> 4) & 0xFF);
            infoFrame[dpOffset + 1] = static_cast<uint8_t>(((encoderDelay & 0x0F) << 4) | ((endPadding >> 8) & 0x0F));
            infoFrame[dpOffset + 2] = static_cast<uint8_t>(endPadding & 0xFF);
        }
        
        // Prepend the info frame to the MP3 data
        mp3Data.insert(mp3Data.begin(), infoFrame.begin(), infoFrame.end());
    }
    
    return mp3Data;
}

// Convenience wrapper
Expected<std::vector<uint8_t>, AudioError> Mp3Codec::encode(
    const AudioData& audioData,
    int bitrateKbps
) {
    return encode(
        audioData.samples.data(),
        audioData.format.totalSampleFrames,
        audioData.format.sampleRateHz,
        audioData.format.channelCount,
        bitrateKbps
    );
}

} // namespace mp3
} // namespace audio_io
