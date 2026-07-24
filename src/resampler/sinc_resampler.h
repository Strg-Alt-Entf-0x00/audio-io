// sinc_resampler.h - High-quality Windowed-Sinc audio resampler (internal)
// Part of audio-io-1.0.0
//
// Algorithm: Windowed-Sinc convolution with Kaiser window.
// No external dependencies. Arbitrary sample rate conversion.
//
// Quality: Stopband attenuation ~80dB, transition band < 5% of Nyquist.
// Performance: O(n * filter_length) where filter_length ~ 64 taps.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace audio_io {
namespace resampler {

// Resample a mono float PCM buffer from src_rate to dst_rate.
// Input:  samples normalized to [-1.0, 1.0]
// Output: resampled samples normalized to [-1.0, 1.0]
// If src_rate == dst_rate, returns a copy of the input.
std::vector<float> resample_mono(
    const float* input,
    size_t       input_frames,
    int          src_rate_hz,
    int          dst_rate_hz
);

// Mix interleaved stereo to mono by averaging left/right channels.
// Input:  interleaved stereo samples (L, R, L, R, ...)
// Output: mono samples (average of L+R)
std::vector<float> stereo_to_mono(
    const float* input,
    size_t       input_frames   // number of stereo frames (total samples / 2)
);

// Duplicate mono to interleaved stereo.
// Output size = input_frames * 2
std::vector<float> mono_to_stereo(
    const float* input,
    size_t       input_frames
);

} // namespace resampler
} // namespace audio_io
