/* Native port of the CMaxxHfSt frame processor 0x180126590.
 *
 * Live layout (aggregate this + 0x1b28):
 *   +0x10/+0x24  processing bank pointer array / index (selector table entry 0
 *                reaches 0x180126590 through aggregate+0x2390)
 *   +0x3f0       right-channel allpass state (float)
 *   +0x3f4       left-channel allpass state (float)
 *   +0x3f8       embedded CPolyCompSt (the dynamics helper's "this")
 *   +0x408/+0x41c  that member's bank pointer array / index
 *   +0x7d8       dynamics gain state (polycomp + 0x3e0), live initial 1.0
 *
 * Bank fields used by the processor (HF bank at +0x10):
 *   +0x00 allpass coefficient, +0x04 left side gain, +0x08 right side gain,
 *   +0x0c left output gain, +0x10 right output gain.
 * Dynamics bank (embedded polycomp at +0x3f8):
 *   +0x04 upward (attack) coefficient, +0x00 downward (release) coefficient,
 *   +0x14 threshold bias.
 *
 * Arithmetic follows the disassembly: the allpass section promotes the
 * coefficient and old state to binary64 and narrows the new state, mid/side,
 * the detector and the mix stay binary32. Run with the caller's MXCSR. */
#ifndef XPS_HF_KERNEL_H
#define XPS_HF_KERNEL_H

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    float split_r;   /* object 0x3f0 */
    float split_l;   /* object 0x3f4 */
    float gain;      /* embedded polycomp 0x3e0 */
} xps_hf_state;

typedef struct {
    float c;
    float side_l;
    float side_r;
    float out_l;
    float out_r;
    float attack;
    float release;
    float bias;
} xps_hf_cfg;

static inline float xps_hf_get(const float *s, unsigned off) {
    float v;
    memcpy(&v, (const unsigned char *)s + off, 4);
    return v;
}

static inline void xps_hf_put(float *s, unsigned off, float v) {
    memcpy((unsigned char *)s + off, &v, 4);
}

/* The embedded member is addressed relative to the stage object base. */
static inline void xps_hf_load_cfg(const float *stage, xps_hf_cfg *c) {
    const unsigned char *b = (const unsigned char *)stage;
    const float *hf, *poly;
    memcpy(&hf, b + 0x10, sizeof hf);
    memcpy(&poly, b + 0x408, sizeof poly);
    c->c = hf[0];
    c->side_l = hf[1];
    c->side_r = hf[2];
    c->out_l = hf[3];
    c->out_r = hf[4];
    c->attack = poly[1];
    c->release = poly[0];
    c->bias = poly[5];
}

static inline void xps_hf_load_state(const float *stage, xps_hf_state *s) {
    s->split_r = xps_hf_get(stage, 0x3f0);
    s->split_l = xps_hf_get(stage, 0x3f4);
    s->gain = xps_hf_get(stage, 0x7d8);
}

static inline void xps_hf_store_state(float *stage, const xps_hf_state *s) {
    xps_hf_put(stage, 0x3f0, s->split_r);
    xps_hf_put(stage, 0x3f4, s->split_l);
    xps_hf_put(stage, 0x7d8, s->gain);
}

static inline float xps_hf_allpass(float x, float *state, float c) {
    const double old = (double)*state;
    const double t = (double)x - (double)c * old;
    *state = (float)t;
    return (float)((double)c * t + old);
}

static inline float xps_hf_detector(float m, xps_hf_state *s, const xps_hf_cfg *c) {
    const float x = (1.0f - c->bias) * m + c->bias;
    const float g = 1.0f / x;
    const float d = g - s->gain;
    s->gain += (d > 0.0f ? c->attack : c->release) * d;
    return s->gain;
}

static inline void xps_hf_process(xps_hf_state *s, const xps_hf_cfg *c,
                                  float in_l, float in_r,
                                  float *out_l, float *out_r) {
    const float yl = xps_hf_allpass(in_l, &s->split_l, c->c);
    const float yr = xps_hf_allpass(in_r, &s->split_r, c->c);
    const float ml = (in_l + yl) * 0.5f;
    const float sl = (in_l - yl) * 0.5f;
    const float mr = (in_r + yr) * 0.5f;
    const float sr = (in_r - yr) * 0.5f;
    const float al = fabsf(sl);
    const float ar = fabsf(sr);
    const float m = al > ar ? al : ar;
    const float g = xps_hf_detector(m, s, c);
    *out_l = ((c->side_l * g) * sl + ml) * c->out_l;
    *out_r = ((c->side_r * g) * sr + mr) * c->out_r;
}

#endif
