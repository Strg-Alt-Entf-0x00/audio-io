#pragma once
// mp3enc-mdct.h
// MDCT and aliasing reduction for MP3 encoding.

#include <cmath>
#include <cstring>
#include "mp3enc-tables.h"

#ifndef M_PI
#    define M_PI 3.14159265358979323846
#endif
// Forward MDCT-36 for long blocks.
// in[36] = prev[18] + cur[18] (raw subband samples)
// out[18] = MDCT frequency coefficients
//
// Formula (ISO 11172-3 Annex C):
//   out[k] = (1/9) * sum(n=0..35) in[n] * sin(pi/36*(n+0.5)) * cos(pi/72*(2n+19)*(2k+1))
static void mp3enc_mdct36(const float * in, float * out) {
    for (int k = 0; k < 18; k++) {
        float sum = 0.0f;
        for (int n = 0; n < 36; n++) {
            float w = sinf((float) M_PI / 36.0f * ((float) n + 0.5f));
            float c = cosf((float) M_PI / 72.0f * (float) (2 * n + 19) * (float) (2 * k + 1));
            sum += in[n] * w * c;
        }
        out[k] = sum * (1.0f / 9.0f);
    }
}

// Alias reduction butterfly between adjacent subbands.
// Applied after MDCT, before quantization.
// ISO 11172-3 Table B.9 coefficients.
//
// For each pair of adjacent bands (band, band+1):
//   mdct[band][17-i] and mdct[band+1][i] are butterflied with cs/ca.
static void mp3enc_alias_reduce(float * mdct_out) {
    for (int band = 1; band < 32; band++) {
        float * a = mdct_out + (band - 1) * 18;  // previous band
        float * b = mdct_out + band * 18;        // current band
        for (int i = 0; i < 8; i++) {
            float u   = a[17 - i];
            float d   = b[i];
            a[17 - i] = u * mp3enc_cs[i] - d * mp3enc_ca[i];
            b[i]      = d * mp3enc_cs[i] + u * mp3enc_ca[i];
        }
    }
}

// Forward MDCT-12 for short blocks.
// in[12] = prev[6] + cur[6] (raw subband samples)
// out[6] = MDCT frequency coefficients
static void mp3enc_mdct12(const float* in, float* out) {
    for (int k = 0; k < 6; k++) {
        float sum = 0.0f;
        for (int n = 0; n < 12; n++) {
            float w = sinf((float) M_PI / 12.0f * ((float) n + 0.5f));
            float c = cosf((float) M_PI / 24.0f * (float) (2 * n + 7) * (float) (2 * k + 1));
            sum += in[n] * w * c;
        }
        out[k] = sum * (1.0f / 3.0f);
    }
}

// Process all 32 subbands for one granule, supporting Long and Short blocks.
// block_type: 0=normal, 1=start, 2=short, 3=stop
static void mp3enc_mdct_granule(const float sb_prev[32][18], const float sb_cur[32][18], float * mdct_out, int block_type) {
    if (block_type == 2) {
        // Short blocks (3 overlapping windows of 12 samples per subband)
        for (int band = 0; band < 32; band++) {
            float mdct_in[36];
            for (int k = 0; k < 18; k++) {
                mdct_in[k]      = sb_prev[band][k];
                mdct_in[k + 18] = sb_cur[band][k];
            }
            // Window 0 (samples 0..11)
            mp3enc_mdct12(&mdct_in[0], mdct_out + band * 18 + 0);
            // Window 1 (samples 6..17)
            mp3enc_mdct12(&mdct_in[6], mdct_out + band * 18 + 6);
            // Window 2 (samples 12..23)
            mp3enc_mdct12(&mdct_in[12], mdct_out + band * 18 + 12);
        }
        // Alias reduction is NOT applied to short blocks in MP3!
    } else {
        // Long blocks
        for (int band = 0; band < 32; band++) {
            float mdct_in[36];
            for (int k = 0; k < 18; k++) {
                mdct_in[k]      = sb_prev[band][k];
                mdct_in[k + 18] = sb_cur[band][k];
            }
            mp3enc_mdct36(mdct_in, mdct_out + band * 18);
        }
        mp3enc_alias_reduce(mdct_out);
    }
}
