/* Native port of the live CMaxx3DSt selector-0 frame processor at 0x180138740.
 *
 * Layout and parameter meaning were recovered from the disassembly and the
 * preset-injected MFX snapshot.  All single-precision steps keep the original
 * operation order; the three envelope smoothers use binary64 intermediates and
 * round back to float32 exactly like the original cvtsd2ss stores.
 */
#ifndef XPS_3D_KERNEL_H
#define XPS_3D_KERNEL_H

#include <math.h>
#include <stdint.h>
#include <string.h>

typedef struct {
    int32_t write_index;   /* object +0x418 */
    int32_t read_index;    /* object +0x41c */
    int32_t delay_index;   /* object +0x420 */
    float buf_a[256];      /* object +0x428 */
    float buf_b[256];      /* object +0x828 */
    float bq1[4];          /* object +0x104c: in, prev-in, out, prev-out */
    float bq2[4];          /* object +0x1454 */
    float env_mid;         /* object +0x1484 */
    float env_side;        /* object +0x1488 */
    float env_ratio;       /* object +0x1490 */
} xps_3d_state;

typedef struct {
    float env_attack;      /* bank A +0x08 */
    float env_release;     /* bank A +0x0c */
    float ratio_bias;      /* bank A +0x10 */
    float env3_release;    /* bank A +0x1c */
    float env3_attack;     /* bank A +0x20 */
    float bq1[5];          /* bank B +0x00..0x10 */
    float bq2[5];          /* bank C +0x00..0x10 */
    float comb_gain;       /* bank A +0xe8 */
    float feedback;        /* bank A +0xf0 */
    float delay_gain;      /* bank A +0xf4 */
    int32_t delay_add;     /* bank A +0xfc */
    int32_t delay_samples; /* bank A +0x100 */
    float width_m;         /* bank A +0x108 */
    float width_s;         /* bank A +0x10c */
    float width_l;         /* bank A +0x110 */
    float width_r;         /* bank A +0x114 */
    int32_t width_enable;  /* bank A +0x118 */
    int32_t ring_size;     /* object +0x3e8 */
    int32_t ring_mask;     /* object +0x3ec */
} xps_3d_cfg;

static inline void xps_3d_load_cfg(const unsigned char *bank_a,
                                   const unsigned char *bank_b,
                                   const unsigned char *bank_c,
                                   int32_t ring_size, int32_t ring_mask,
                                   xps_3d_cfg *c) {
    float a[0x120 / 4], b[0x20 / 4], d[0x20 / 4];
    memcpy(a, bank_a, sizeof a);
    memcpy(b, bank_b, sizeof b);
    memcpy(d, bank_c, sizeof d);
    c->env_attack = a[0x08 / 4];
    c->env_release = a[0x0c / 4];
    c->ratio_bias = a[0x10 / 4];
    c->env3_release = a[0x1c / 4];
    c->env3_attack = a[0x20 / 4];
    c->comb_gain = a[0xe8 / 4];
    c->feedback = a[0xf0 / 4];
    c->delay_gain = a[0xf4 / 4];
    memcpy(&c->delay_add, (const unsigned char *)a + 0xfc, 4);
    memcpy(&c->delay_samples, (const unsigned char *)a + 0x100, 4);
    c->width_m = a[0x108 / 4];
    c->width_s = a[0x10c / 4];
    c->width_l = a[0x110 / 4];
    c->width_r = a[0x114 / 4];
    memcpy(&c->width_enable, (const unsigned char *)a + 0x118, 4);
    memcpy(c->bq1, b, sizeof(c->bq1));
    memcpy(c->bq2, d, sizeof(c->bq2));
    c->ring_size = ring_size;
    c->ring_mask = ring_mask;
}

static inline float xps_3d_biquad(const float *coef, float x, float *state) {
    const float x1 = state[0], x2 = state[1];
    const float y1 = state[2], y2 = state[3];
    float t = coef[0] * x;
    t += coef[1] * x1;
    t += coef[2] * x2;
    float fb = coef[3] * y1;
    fb += coef[4] * y2;
    const float y = t - fb;
    state[0] = x;
    state[1] = x1;
    state[2] = y;
    state[3] = y1;
    return y;
}

static inline void xps_3d_process(xps_3d_state *s, const xps_3d_cfg *c,
                                  const float in[8], float out[2]) {
    const double HALF = 0.5, ONE = 1.0;
    const double a = (double)in[2] * HALF;
    const double b = (double)in[6] * HALF;
    const float mid = (float)(a + b);
    const float side = (float)(a - b);

    const float abs_mid = fabsf(mid);
    double env_mid = (double)s->env_mid;
    float coef = env_mid > (double)abs_mid ? c->env_release : c->env_attack;
    env_mid = env_mid * (double)coef + (double)abs_mid * (ONE - (double)coef);
    s->env_mid = (float)env_mid;

    const float abs_side = fabsf(side);
    double env_side = (double)s->env_side;
    coef = env_side > (double)abs_side ? c->env_release : c->env_attack;
    env_side = env_side * (double)coef + (double)abs_side * (ONE - (double)coef);
    s->env_side = (float)env_side;

    const float em = s->env_mid;
    const float es = s->env_side;
    const float max_env = em > es ? em : es;
    const float bias = c->ratio_bias;
    float ratio = (bias + es) / (max_env + bias);
    ratio *= ratio;

    double env_ratio = (double)s->env_ratio;
    float coef3 = env_ratio > (double)ratio ? c->env3_release : c->env3_attack;
    env_ratio = env_ratio * (double)coef3 + (double)ratio * (ONE - (double)coef3);
    s->env_ratio = (float)env_ratio;
    const float env3 = s->env_ratio;

    const float y = xps_3d_biquad(c->bq1, side, s->bq1);
    const float z = xps_3d_biquad(c->bq2, y, s->bq2);

    const int32_t idx = (s->write_index + 1) & c->ring_mask;
    s->write_index = idx;
    s->read_index = idx;

    const float old_a = s->buf_a[idx];
    const float old_b = s->buf_b[idx];
    float new_a = old_a * c->feedback;
    new_a += mid;
    float new_b = z + (c->comb_gain * z) * 2.0f;
    new_b += c->feedback * old_b;

    const int32_t store = (idx + c->delay_add) & c->ring_mask;
    s->read_index = store;
    s->buf_a[store] = new_a;
    s->buf_b[store] = new_b;

    const int32_t delayed = (c->ring_size - c->delay_samples + store) & c->ring_mask;
    s->delay_index = delayed;
    float wet_a = new_a + s->buf_a[delayed] * c->delay_gain;
    float wet_b = new_b - c->delay_gain * s->buf_b[delayed];
    wet_a *= env3;
    wet_a += mid * (1.0f - env3);

    float l = wet_a + wet_b;
    l += in[3];
    l += in[1];
    float r = wet_a - wet_b;
    r += in[7];
    r += in[5];

    if (c->width_enable != 0) {
        const float m = (float)((double)(l + r) * HALF);
        const float sd = (float)((double)(l - r) * HALF);
        const float km = env3 * m;
        const float d = m - km;
        const float d2 = d * c->width_m;
        const float sl = (sd + km) * c->width_s;
        const float sr = (km - sd) * c->width_s;
        l = sl + d * c->width_l;
        r = sr + d2 * c->width_r;
    }

    out[0] += l;
    out[1] += r;
}

#endif
