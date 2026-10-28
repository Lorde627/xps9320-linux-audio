/* Native port of the original CIVolSt4 virtual +0x68 curve generator
 * (0x18010be30). Inputs mirror the fields the original reads; output is the
 * serialized parameter block consumed by 0x10cf30. Validated bitwise against
 * the original under isolated execution. No hardware or playback here. */
#ifndef XPS_IVOL_CURVE_H
#define XPS_IVOL_CURVE_H

#include <math.h>
#include <stdint.h>

#include "native_powf_sse2.h"

struct xps_curve_input {
    float low;          /* V+0x2ca8 */
    float ac;           /* V+0x2cac */
    float gain;         /* V+0x2cb0 */
    float ceiling;      /* V+0x2ccc */
    float att;          /* V+0x2cdc, negative dB */
    float e0;           /* V+0x2ce0 */
    int32_t hardknee;   /* V+0x2cd8, any nonzero */
    int32_t flag30;     /* V+0x30c0[owner bank] */
    int32_t flag_k;     /* K+0x3a8[owner bank] */
    int32_t nr_bypass;  /* V+0x4f7e8[owner bank] */
    int32_t nr_mode;    /* V+0x4f958 */
    float v391c;        /* V+0x391c, base route trim */
    float v3924;        /* V+0x3924, NoiseRider mode trim */
    float v392c;        /* V+0x392c, second route trim */
    float k434;         /* compressor reference */
    float k444;         /* compressor makeup-ish */
    float k43c;         /* compressor curve reference */
    float b308[2];      /* persistent block +0x308/+0x30c */
    float b320[2];      /* persistent block +0x320/+0x324 */
};

/* Writes block[0x04..0x31c] and block[0x300..0x307]. Returns 0 on success,
 * -1 if any written value is non-finite (the original would propagate it). */
static inline int xps_ivol_curve(const struct xps_curve_input *in, float *block) {
    float att = in->att, low = in->low, ac = in->ac, gain = in->gain, ceiling = in->ceiling;
    float maxatt = (att > in->e0) ? att : in->e0;   /* MAXSS att, e0 */
    maxatt = (maxatt > ac) ? maxatt : ac;           /* MAXSS, ac */
    float diff = low - maxatt;
    if ((double)diff > 40.0) {
        float t = (float)((double)low - 40.0);
        maxatt = t;
        diff = low - maxatt;
    }
    float x9 = (float)pow(10.0, (double)diff / 20.0);
    if (0.f > x9) x9 = 0.f;                         /* COMISS/JBE: NaN keeps x9 */
    float m = (1e-10f > x9) ? 1e-10f : x9;          /* MAXSS 1e-10, x9 */
    float l10 = log10f(m);
    float x7 = (float)((double)l10 * 20.0);
    float gf = (float)((double)gain + (double)att * 0.5);
    float x8 = (0.f > gf) ? 0.f : gf;               /* MAXSS 0, gf */
    float x11 = log10f(in->k434) * 20.0f;
    float x12 = in->v391c;
    if (!in->nr_bypass) {
        if (in->nr_mode == 1) x12 += in->v3924;
        else if (in->nr_mode == 2) x11 += log10f(in->k444) * 20.0f;
        else if (in->nr_mode == 3) { x12 += in->v3924; x11 += log10f(in->k444) * 20.0f; }
    }
    if (in->flag30) x12 = 0.f;
    if (in->flag_k) x11 = 0.f;
    x12 += x11;
    x12 += in->flag30 ? 0.f : in->v392c;
    x12 += x8;
    float x15 = (float)pow(10.0, (double)x12 / 20.0);
    float t1 = ceiling - att;
    float t2 = (ac + ceiling) - att;
    float mn = (0.f < t1) ? 0.f : t1;               /* MINSS 0, t1 */
    float p1 = (float)pow(10.0, (double)mn / 20.0);
    float p7 = (float)pow(10.0, (double)x7 / 20.0);
    float t14 = t2 - x7;
    float delta = p7 - x15;
    float v11 = (0.f > delta) ? 0.f : delta;        /* MAXSS 0, delta */
    float p14 = (float)pow(10.0, (double)t14 / 20.0);
    double qd = (double)p1 * 0.99;
    qd /= (double)v11;
    float x0 = (float)(((double)p14 < qd) ? (double)p14 : qd); /* MINSD p14, qd */
    float x4 = v11 + x15;
    float x5 = (float)(1.0 / (double)x4);
    float x3 = v11 * x0;
    float x12b = p1 - x3;
    v11 *= x5;
    x12b /= x15;
    x15 *= x5;
    block[0x1c / 4] = x15;
    block[0x10 / 4] = x12b;
    /* Hard knee keeps the zeroed register: block[0x08] is 0, not the dB value. */
    float x8b = in->hardknee ? 0.f : x12b;
    block[0x18 / 4] = v11;
    double r = 1.0 / (double)x15;
    double r14 = (1.0 > r) ? 1.0 : r;               /* MAXSD 1.0, r */
    double r11 = 1.0 / (double)x5;
    block[0x24 / 4] = (float)r11;
    block[0x14 / 4] = (float)r14;
    block[0x08 / 4] = x8b;
    block[0x0c / 4] = x0;
    float p8 = (float)pow(10.0, (double)x8 / 20.0);
    float xs = x9 * 511.f;
    block[0x04 / 4] = (511.f > xs) ? 511.f : xs;    /* MAXSS 511, xs */
    float pc = (float)pow(10.0, (double)ceiling / 20.0);
    block[0x2c / 4] = pc / p8;
    block[0x30 / 4] = p8;
    block[0x28 / 4] = 511.f;
    float pa = (float)pow(10.0, (double)att / 20.0);
    block[0x31c / 4] = pa;
    float bb8 = block[0x08 / 4], bbc = block[0x0c / 4], bb10 = block[0x10 / 4];
    float u3 = 1.f + bbc;
    float u1 = bb8 + 1.f;
    float d1 = (bbc > u3) ? bbc : u3;
    float d2 = (bb10 > u1) ? bb10 : u1;
    float q1 = bbc / d1;
    float q2 = bb10 / d2;
    q2 *= block[0x1c / 4];
    q1 *= block[0x18 / 4];
    float s = q2 + q1;
    s *= block[0x24 / 4];
    float v0 = pa * in->b320[0] * s;
    float v1 = pa * in->b320[1] * s;
    block[0x300 / 4] = v0 / in->b308[0];
    block[0x304 / 4] = v1 / in->b308[1];
    return 0;
}

/* Side effect of the original call into 0x180136f00: refresh K+0x0c/+0x10,
 * then recompute kparams[0x18] with the image's powf backend. The caller is
 * responsible for the K+0x434 = K+0x438 copy made by the same routine. */
static inline int xps_ivol_curve_comp_update(float *kparams, float k43c, int k460) {
    float kx = log10f(k43c) * 20.0f;
    float v = (float)pow(10.0, (double)kx / 20.0);
    kparams[0x0c / 4] = v;
    kparams[0x10 / 4] = k460 ? 0.f : v;
    float e;
    int status = xps_powf_positive(kparams[0x0c / 4], kparams[0x08 / 4], &e);
    if (status == XPS_POWF_OK) kparams[0x18 / 4] = e;
    return (status == XPS_POWF_OK && isfinite(v)) ? 0 : -1;
}

#endif
