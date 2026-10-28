/* Native C port of the MaxxAudio index4 scalar bass kernel (0x1801249c0).
 *
 * Mirrors the Python transcription already validated bit-for-bit against the
 * isolated original on 96,000 factory-parameter frames (outputs and the full
 * 0x114-byte state trace). All arithmetic follows that model: split sections
 * promote to binary64 and narrow each stored state, envelope/mix steps stay in
 * binary32, and the two power calls use the verified positive-normal powf
 * backend. The original wrapper runs this kernel with FTZ/DAZ enabled; the
 * image's powf was measured bit-identical with and without those bits over
 * 200,000 pairs, so the backend is invoked with MXCSR temporarily set to
 * 0x1f80 and restored afterwards.
 */
#ifndef XPS_BASS_KERNEL_H
#define XPS_BASS_KERNEL_H

#include <math.h>
#include <stdint.h>
#include <string.h>
#include <xmmintrin.h>
#include "native_powf_sse2.h"

typedef struct {
    float *state;       /* raw object state; kernel uses 0x4bc..0x5cf */
    const float *p;     /* 0x100-byte host parameter block */
    int wide;           /* byte P+0xda == 1 */
    int error;          /* nonzero if a pow call left its verified domain */
} xps_bass_kernel_ctx;

static inline float xps_bass_k_get(const float *s, unsigned off) {
    float v; memcpy(&v, (const unsigned char *)s + off, 4); return v;
}
static inline void xps_bass_k_put(float *s, unsigned off, float v) {
    memcpy((unsigned char *)s + off, &v, 4);
}
static inline int32_t xps_bass_k_get_i32(const float *s, unsigned off) {
    int32_t v; memcpy(&v, (const unsigned char *)s + off, 4); return v;
}
static inline void xps_bass_k_put_i32(float *s, unsigned off, int32_t v) {
    memcpy((unsigned char *)s + off, &v, 4);
}

static inline float xps_bass_k_clamp(float x) {
    if (x < -1.0f) return -1.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static inline float xps_bass_k_powf(xps_bass_kernel_ctx *k, float base, float power) {
    unsigned save = _mm_getcsr();
    _mm_setcsr(0x1f80u);
    float result = 0.0f;
    int status = xps_powf_positive(base, power, &result);
    _mm_setcsr(save);
    if (status != XPS_POWF_OK) {
        k->error = status;
        return 0.0f;
    }
    return result;
}

/* One two-section split. plus==0: (second-first)*0.5f; plus!=0: (second+first)*0.5f. */
static inline float xps_bass_k_split(xps_bass_kernel_ctx *k, float x, unsigned c,
                                     unsigned so, int plus) {
    float *s = k->state;
    const float *p = k->p;
    const float a = p[c / 4], b = p[(c + 4) / 4], d = p[(c + 8) / 4];
    const float u = xps_bass_k_get(s, so);
    const float v = xps_bass_k_get(s, so + 4);
    const float w = xps_bass_k_get(s, so + 8);
    const double z0 = (double)x - (double)a * (double)u;
    const float first = (float)(z0 * (double)a + (double)u);
    const double z1 = ((double)x - (double)b * (double)v) - (double)d * (double)w;
    const float second = (float)((z1 * (double)d + (double)b * (double)v) + (double)w);
    xps_bass_k_put(s, so, (float)z0);
    xps_bass_k_put(s, so + 4, (float)z1);
    xps_bass_k_put(s, so + 8, v);
    return (plus ? (second + first) : (second - first)) * 0.5f;
}

static inline float xps_bass_k_envelope(xps_bass_kernel_ctx *k, float x, unsigned c,
                                        unsigned exponent, unsigned bias, unsigned memory) {
    const float base = xps_bass_k_clamp(fabsf(x) + k->p[(c + bias) / 4]);
    const float target = xps_bass_k_powf(k, base, -k->p[(c + exponent) / 4]);
    const float previous = xps_bass_k_get(k->state, memory);
    const float delta = target - previous;
    const float speed = k->p[(c + (delta > 0.0f ? 0x50u : 0x4cu)) / 4];
    const float result = speed * delta + previous;
    xps_bass_k_put(k->state, memory, result);
    return result;
}

static inline float xps_bass_k_helper(xps_bass_kernel_ctx *k, float x, unsigned c,
                                      unsigned so) {
    float *s = k->state;
    const float *p = k->p;
    float gain = xps_bass_k_envelope(k, x, c, 0x18, 0x1c, so + 0x1c);
    x = xps_bass_k_clamp((x * gain) * 0.5f);
    x = xps_bass_k_clamp(x + xps_bass_k_get(s, so + 0x58) * x);
    x = xps_bass_k_split(k, x, c + 0x28, so + 0x2c, 0);
    const float intermediate = x;
    x = xps_bass_k_split(k, x, c + 0x34, so + 0x38, 0);
    gain = xps_bass_k_envelope(k, x, c, 0x20, 0x24, so + 0x24);
    const float quarter = intermediate * 0.25f;
    float limit = (float)((double)p[(c + 0x60) / 4] /
                          (double)(fabsf(quarter) + p[(c + 0x5c) / 4]));
    const float smoothed = (limit - xps_bass_k_get(s, so + 0x64)) * p[(c + 0x50) / 4] +
                           xps_bass_k_get(s, so + 0x64);
    if (smoothed < limit) limit = smoothed;
    xps_bass_k_put(s, so + 0x64, limit);
    if (limit < gain) gain = limit;
    xps_bass_k_put(s, so + 0x58, xps_bass_k_clamp(quarter * gain));
    float wet = xps_bass_k_clamp(-(x * gain));
    if (k->wide) wet = xps_bass_k_split(k, wet, c + 0x40, so + 0x4c, 1);
    return wet;
}

/* Input pair is spaced: channel 0 at byte 0, channel 1 at byte 0x10.
 * Output is contiguous: out[0], out[1]. */
static inline void xps_bass_kernel_process(xps_bass_kernel_ctx *k,
                                           const float *input, float *output) {
    float *s = k->state;
    const float *p = k->p;
    const float left = input[0];
    const float right = input[4];
    const float factor = p[0xcc / 4];
    float phase = (float)xps_bass_k_get_i32(s, 0x4bc);
    const float mono = (left + right) * 0.5f;

    float a = xps_bass_k_split(k, mono, 0x00, 0x504, 0);
    a = xps_bass_k_split(k, a, 0x0c, 0x510, 1);
    const float old_a = xps_bass_k_get(s, 0x4c4);
    if (phase != 0.0f || factor == 1.0f) {
        a = xps_bass_k_helper(k, a, 0x00, 0x500);
        xps_bass_k_put(s, 0x4c4, a);
        if (factor > 1.0f) a = (a + old_a) * 0.5f;
    } else {
        a = old_a;
    }
    phase = 1.0f - phase;
    xps_bass_k_put_i32(s, 0x4bc, (int32_t)phase);

    float b = xps_bass_k_split(k, mono, 0x64, 0x56c, 0);
    const float old_b = xps_bass_k_get(s, 0x4c0);
    if (phase != 0.0f || factor == 1.0f) {
        b = xps_bass_k_helper(k, b, 0x64, 0x568);
        xps_bass_k_put(s, 0x4c0, b);
        if (factor > 1.0f) b = (b + old_b) * 0.5f;
    } else {
        b = old_b;
    }

    for (int ch = 0; ch < 2; ch++) {
        const float dry = ch ? right : left;
        const float weight = p[(0xd0 + 4 * ch) / 4];
        float first = (p[0xc8 / 4] * p[0x54 / 4]) * weight;
        first = first * a;
        first = first + p[0x58 / 4] * dry;
        float second = (p[0xc8 / 4] * p[0xb8 / 4]) * weight;
        second = second * b;
        output[ch] = second + first;
    }
}

/* State defaults used by the factory design for a fresh branch. */
static inline void xps_bass_kernel_init_state(float *state) {
    memset(state, 0, 0x800);
    xps_bass_k_put_i32(state, 0x4bc, 1);
    xps_bass_k_put(state, 0x558, 0.25f);
    xps_bass_k_put(state, 0x564, 16.0f);
    xps_bass_k_put(state, 0x5c0, 0.25f);
    xps_bass_k_put(state, 0x5cc, 16.0f);
}

#endif
