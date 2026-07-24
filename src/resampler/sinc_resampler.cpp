// sinc_resampler.cpp - High-quality Windowed-Sinc audio resampler (internal)
// Part of audio-io-1.0.0
//
// Algorithm details:
//   1. Compute GCD-reduced rational ratio P/Q (src_rate/P -> dst_rate/Q).
//   2. Build a Kaiser-windowed Sinc filter at the Nyquist of the lower rate.
//   3. Polyphase decomposition: for each output sample, apply the correct
//      filter phase to the input without materializing the upsampled buffer.
//
// Kaiser window parameter beta=8.0 gives ~80dB stopband attenuation.
// Filter half-length = 32 taps per polyphase phase.

#include "sinc_resampler.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>
#include <mutex>
#include <map>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audio_io {
namespace resampler {

// ============================================================================
// Math helpers
// ============================================================================

// Modified Bessel function of the first kind, order 0 (for Kaiser window).
// Uses the series expansion: I0(x) = sum_{k=0}^{inf} ((x/2)^k / k!)^2
static double bessel_i0(double x) {
    double sum  = 1.0;
    double term = 1.0;
    double half = x * 0.5;
    for (int k = 1; k <= 30; ++k) {
        term *= (half / k) * (half / k);
        sum  += term;
        if (term < sum * 1e-15) break;
    }
    return sum;
}

// Normalized sinc: sinc(x) = sin(pi*x) / (pi*x), sinc(0) = 1
static double sinc(double x) {
    if (std::fabs(x) < 1e-12) return 1.0;
    const double px = M_PI * x;
    return std::sin(px) / px;
}

// ============================================================================
// Filter construction
// ============================================================================

// Build a Kaiser-windowed lowpass FIR filter.
//   cutoff_hz:    cutoff frequency in normalized units [0, 0.5] (0.5 = Nyquist)
//   half_length:  half the filter length (total = 2*half_length + 1 taps)
//   beta:         Kaiser window beta (8.0 -> ~80dB attenuation)
// Returns filter coefficients centered around index half_length.
static std::vector<double> build_kaiser_sinc(int half_length, double cutoff_norm, double beta) {
    const int   length    = 2 * half_length + 1;
    const double i0_beta  = bessel_i0(beta);

    std::vector<double> h(length);
    for (int n = 0; n < length; ++n) {
        double x  = n - half_length;
        // Kaiser window
        double r  = 1.0 - (x / half_length) * (x / half_length);
        double w  = bessel_i0(beta * std::sqrt(std::max(0.0, r))) / i0_beta;
        // Windowed sinc (lowpass at cutoff_norm * 2, so we multiply by 2*cutoff)
        h[n] = w * 2.0 * cutoff_norm * sinc(2.0 * cutoff_norm * x);
    }
    return h;
}

// ============================================================================
// Core resample_mono implementation
// ============================================================================

#include <immintrin.h>
#include <intrin.h>

// Check AVX2 support at runtime
static bool has_avx2() {
    int cpuInfo[4];
    __cpuid(cpuInfo, 1);
    if ((cpuInfo[2] & (1 << 27)) && (cpuInfo[2] & (1 << 28))) { // OSXSAVE & AVX
        __cpuidex(cpuInfo, 7, 0);
        return (cpuInfo[1] & (1 << 5)) != 0; // AVX2
    }
    return false;
}

// Filter Cache for Resampler (2D Polyphase Table)
struct FilterCacheKey {
    int L, M;
    bool operator<(const FilterCacheKey& other) const {
        if (L != other.L) return L < other.L;
        return M < other.M;
    }
};

static std::mutex g_filterCacheMutex;
static std::map<FilterCacheKey, std::vector<std::vector<float>>> g_filterCache;

constexpr int NUM_TAPS = 64;
constexpr int HALF_TAPS = 32;

// Build 2D Polyphase Table
static std::vector<std::vector<float>> build_polyphase_table(int L, double cutoff_norm, double beta) {
    std::vector<std::vector<float>> table(L, std::vector<float>(NUM_TAPS, 0.0f));
    const int half_len = HALF_TAPS * L;
    auto h_proto = build_kaiser_sinc(half_len, cutoff_norm, beta);

    for (int phase = 0; phase < L; ++phase) {
        for (int k = 0; k < NUM_TAPS; ++k) {
            int old_k = k - 31; // maps k in [0, 63] to [-31, 32]
            int h_idx = half_len - old_k * L + phase;
            if (h_idx >= 0 && h_idx < static_cast<int>(h_proto.size())) {
                table[phase][k] = static_cast<float>(h_proto[h_idx] * L); // pre-multiply gain L
            }
        }
    }
    return table;
}

std::vector<float> resample_mono(
    const float* input,
    size_t       input_frames,
    int          src_rate_hz,
    int          dst_rate_hz)
{
    if (src_rate_hz <= 0 || dst_rate_hz <= 0) {
        return {};
    }
    if (input_frames == 0) return {};
    if (src_rate_hz == dst_rate_hz) {
        return std::vector<float>(input, input + input_frames);
    }

    const int g = std::gcd(src_rate_hz, dst_rate_hz);
    const int L = dst_rate_hz / g;
    const int M = src_rate_hz  / g;

    if (L > 128) {
        return {};
    }

    const double cutoff_norm = 0.5 / std::max(L, M);
    const double beta = 8.0;

    std::vector<std::vector<float>> poly_table;
    {
        std::lock_guard<std::mutex> lock(g_filterCacheMutex);
        FilterCacheKey key{L, M};
        auto it = g_filterCache.find(key);
        if (it != g_filterCache.end()) {
            poly_table = it->second;
        } else {
            poly_table = build_polyphase_table(L, cutoff_norm, beta);
            g_filterCache[key] = poly_table;
        }
    }

    const size_t out_frames = static_cast<size_t>((static_cast<int64_t>(input_frames) * L + M - 1) / M);
    std::vector<float> output(out_frames);

    // Zero-pad input to avoid bounds checking in the inner loop
    // input index maps: n_in_int + old_k. old_k goes down to -31 and up to +32.
    // So we need 31 padding zeros at the start and 32 at the end.
    std::vector<float> padded_input(input_frames + NUM_TAPS, 0.0f);
    std::memcpy(padded_input.data() + 31, input, input_frames * sizeof(float));

    static const bool use_avx2 = has_avx2();

    for (size_t n_out = 0; n_out < out_frames; ++n_out) {
        const int64_t numerator = static_cast<int64_t>(n_out) * M;
        const int64_t n_in_int  = numerator / L;
        const int     phase     = static_cast<int>(numerator % L);

        const float* in_ptr = padded_input.data() + n_in_int;
        const float* f_ptr  = poly_table[phase].data();

        float acc = 0.0f;

        if (use_avx2) {
            __m256 sum256 = _mm256_setzero_ps();
            // NUM_TAPS is exactly 64, so 8 iterations of 8 floats
            for (int k = 0; k < NUM_TAPS; k += 8) {
                __m256 v_in = _mm256_loadu_ps(in_ptr + k);
                __m256 v_f  = _mm256_loadu_ps(f_ptr + k);
                sum256 = _mm256_add_ps(sum256, _mm256_mul_ps(v_in, v_f));
            }
            // Horizontal add
            __m128 lo = _mm256_castps256_ps128(sum256);
            __m128 hi = _mm256_extractf128_ps(sum256, 1);
            lo = _mm_add_ps(lo, hi);
            lo = _mm_hadd_ps(lo, lo);
            lo = _mm_hadd_ps(lo, lo);
            acc = _mm_cvtss_f32(lo);
        } else {
            // Fast scalar fallback
            for (int k = 0; k < NUM_TAPS; ++k) {
                acc += in_ptr[k] * f_ptr[k];
            }
        }

        output[n_out] = acc;
    }

    return output;
}

// ============================================================================
// Channel conversion helpers
// ============================================================================

std::vector<float> stereo_to_mono(const float* input, size_t input_frames) {
    std::vector<float> mono(input_frames);
    for (size_t i = 0; i < input_frames; ++i) {
        mono[i] = (input[i * 2] + input[i * 2 + 1]) * 0.5f;
    }
    return mono;
}

std::vector<float> mono_to_stereo(const float* input, size_t input_frames) {
    std::vector<float> stereo(input_frames * 2);
    for (size_t i = 0; i < input_frames; ++i) {
        stereo[i * 2]     = input[i];
        stereo[i * 2 + 1] = input[i];
    }
    return stereo;
}

} // namespace resampler
} // namespace audio_io
