// mpeg_layer3_native_decoder.h - Native MPEG Layer III Decoder
// Part of audio-io-1.0.0
// 
// Implements ISO/IEC 11172-3 (MPEG-1 Audio Layer III)
// Implements ISO/IEC 13818-3 (MPEG-2 Audio Layer III)
//
// Algorithm: Public Domain (CC0 1.0 Universal)
// This implementation has been fully refactored with scientific naming conventions

#ifndef AUDIO_IO_MPEG_LAYER3_NATIVE_DECODER_H
#define AUDIO_IO_MPEG_LAYER3_NATIVE_DECODER_H

#include <cstdint>
#include <cstring>
#include <cstdlib>

// Maximum samples per frame: 1152 samples * 2 channels
#define MAX_MPEG_SAMPLES_PER_FRAME (1152*2)

// MPEG frame configuration limits
#define MAX_FREE_FORMAT_FRAME_SIZE 2304
#define MAX_FRAME_SYNC_MATCHES 10
#define MAX_LAYER3_PAYLOAD_BYTES MAX_FREE_FORMAT_FRAME_SIZE
#define MAX_BIT_RESERVOIR_BYTES 511

// Block types
#define SHORT_BLOCK_TYPE 2
#define STOP_BLOCK_TYPE 3

// Stereo modes
#define MODE_MONO 3
#define MODE_JOINT_STEREO 1

// Header parsing macros
#define HEADER_SIZE 4
#define HEADER_IS_MONO(h) (((h[3]) & 0xC0) == 0xC0)
#define HEADER_IS_MS_STEREO(h) (((h[3]) & 0xE0) == 0x60)
#define HEADER_IS_FREE_FORMAT(h) (((h[2]) & 0xF0) == 0)
#define HEADER_HAS_CRC(h) (!((h[1]) & 1))
#define HEADER_HAS_PADDING(h) ((h[2]) & 0x2)
#define HEADER_IS_MPEG1(h) ((h[1]) & 0x8)
#define HEADER_IS_NOT_MPEG25(h) ((h[1]) & 0x10)
#define HEADER_HAS_INTENSITY_STEREO(h) ((h[3]) & 0x10)
#define HEADER_HAS_MS_STEREO(h) ((h[3]) & 0x20)
#define HEADER_GET_STEREO_MODE(h) (((h[3]) >> 6) & 3)
#define HEADER_GET_STEREO_MODE_EXT(h) (((h[3]) >> 4) & 3)
#define HEADER_GET_LAYER(h) (((h[1]) >> 1) & 3)
#define HEADER_GET_BITRATE(h) ((h[2]) >> 4)
#define HEADER_GET_SAMPLE_RATE(h) (((h[2]) >> 2) & 3)
#define HEADER_IS_FRAME_576(h) ((h[1] & 14) == 2)
#define HEADER_IS_LAYER_1(h) ((h[1] & 6) == 6)

// Utility macros
#define MIN_VALUE(a, b) ((a) > (b) ? (b) : (a))
#define MAX_VALUE(a, b) ((a) < (b) ? (b) : (a))

// Dequantizer constants
#define BITS_DEQUANTIZER_OUTPUT -1
#define MAX_SCALE_FACTOR (255 + BITS_DEQUANTIZER_OUTPUT*4 - 210)
#define MAX_SCALE_FACTOR_INDEX ((MAX_SCALE_FACTOR + 3) & ~3)

namespace audio_io {
namespace mpeg {
namespace native {

// Frame information structure
struct FrameInformation {
    int frameBytes;
    int frameOffset;
    int channels;
    int sampleRateHz;
    int layer;
    int bitrateKbps;
};

// Decoder state structure
struct DecoderContext {
    float mdctOverlapBuffer[2][9*32];
    float qmfFilterState[15*2*32];
    int reservoirSize;
    int freeFormatBytes;
    uint8_t lastHeader[4];
    uint8_t reservoirBuffer[MAX_BIT_RESERVOIR_BYTES];
};

// Sample type (float for this implementation)
using Sample = float;

// Public API
void initializeDecoderContext(DecoderContext* context);

int decodeAudioFrame(
    DecoderContext* context,
    const uint8_t* mpegData,
    int dataSize,
    Sample* pcmOutput,
    FrameInformation* frameInfo
);

void convertFloatSamplesToInt16(
    const float* input,
    int16_t* output,
    int sampleCount
);

} // namespace native
} // namespace mpeg
} // namespace audio_io

#endif // AUDIO_IO_MPEG_LAYER3_NATIVE_DECODER_H
