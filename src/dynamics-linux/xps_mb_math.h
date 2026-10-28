/* Four-lane math backends used by the factory CMultiBand frame processor.
 *
 * The image calls two helpers (RVA 0x5ebdc0 powf, RVA 0x5ebe50 logf) that
 * lazily resolve, per CPU feature level, to one of several SIMD variants
 * (dispatch tables at RVA 0x11113a0 / 0x1111400, level at RVA 0x123c080).
 * Both helpers take four lanes packed in one xmm register.
 *
 *   powf   level 4..7  0x5ec580   level 8  0x5eca40   level 9..11 0x5ecda0
 *   logf   level 4..7  0x5ed220   level 8  0x5ed370   level 9..11 0x5ed4c0
 *
 * powf reconstructs ln(x) in double precision from the mantissa of x and a
 * 513-entry table, multiplies by the exponent and evaluates 2**z with a
 * 2048-entry table.  The implementation below is the level-4..7 variant
 * (0x5ec0c0); on 200000 random lanes in the domain the level-9 variant
 * returns the same bits.
 *
 * logf is the Cephes-style float polynomial.  The level-4..7 variant
 * (0x5ed220) evaluates it with separate multiply and add steps; the level-9
 * variant (0x5ed4c0) uses one fused multiply-add per step and therefore
 * rounds differently (447 of 200000 random lanes differ by one ulp, and the
 * difference propagates into the CMultiBand signal path).  The FMA form is
 * implemented below because the live reference capture was made on a
 * level-9 CPU.
 *
 * Both functions only cover the domain the DSP feeds them: powf with a
 * positive normal base and an exponent in roughly [-2, 2], logf with a
 * positive normal argument.  Outside that domain the image takes a scalar
 * slow path which is not reproduced here.
 */
#ifndef XPS_MB_MATH_H
#define XPS_MB_MATH_H

#include <stdint.h>
#include <string.h>
#include <math.h>

#include "xps_mb_math_tables.h"

static inline double xps_mb_dfrombits(uint64_t b)
{
    double d;
    memcpy(&d, &b, sizeof d);
    return d;
}

static inline uint64_t xps_mb_bitsof(double d)
{
    uint64_t b;
    memcpy(&b, &d, sizeof b);
    return b;
}

/* The image rounds by adding and subtracting 1.5*2**52; both operations are
 * exact IEEE double additions, so the result is round-to-nearest-even. */
static inline double xps_mb_rint(double u)
{
    return (u + 6755399441055744.0) - 6755399441055744.0;
}

/* Image backend at RVA 0x5ec0c0 (legacy SSE2 variant, four lanes). */
static inline float xps_mb_powf(float base, float expo)
{
    const double half = 0.49999999999988803;
    const double third = 0.33333333333308374;
    const double ln2 = 0.6931471805599453;
    const double scale = 2954.639443740597;             /* 2048 / ln 2 */
    const double corr = 0.00033850805268231294;         /* ln 2 / 2048 */
    uint64_t xb, valbits, qbits;
    double x = (double)base;
    double y = (double)expo;
    double v, r, t, tt, logpart, lnx, z, t2, n, frac, val;
    int64_t ni;

    memcpy(&xb, &x, sizeof xb);
    v = xps_mb_dfrombits((xb & 0x000fffffffffffffULL) | 0x3f50000000000000ULL);

    r = xps_mb_rint(1.0 / v);
    if (r < 512.0) r = 512.0;
    if (r > 1024.0) r = 1024.0;

    t = v * r - 1.0;                    /* residual of v against 1/r */
    tt = t * t;
    logpart = t + (t * third - half) * tt;

    lnx = ((double)((xb >> 52) & 0x7ff) - (r > 724.0 ? 1023.0 : 1022.0)) * ln2
          + xps_mb_dfrombits(xps_mb_log_table[(int)r - XPS_MB_LOG_TABLE_R0])
          + logpart;

    z = y * lnx;
    t2 = scale * z;
    n = xps_mb_rint(t2 - 0.5);
    frac = t2 - n;
    ni = (int64_t)n;

    val = xps_mb_dfrombits(xps_mb_exp2_table[(int)(ni & 0x7ff)]);
    val = val + (frac * corr) * val;

    qbits = (uint64_t)(ni >> 11) * 0x0010000000000000ULL;   /* 2**q */
    valbits = xps_mb_bitsof(val) + qbits;
    return (float)xps_mb_dfrombits(valbits);
}

/* logf implementations of the image.  The helper is lazily dispatched by
 * CPU feature level (table at RVA 0x1111400, level at RVA 0x123c080):
 *
 *   level 4..7   RVA 0x5ed220   separate mul/add polynomial
 *   level 8      RVA 0x5ed370
 *   level 9..11  RVA 0x5ed4c0   FMA polynomial (vfmadd213ps)
 *
 * The level-9 FMA body is transcribed below.  It evaluates the same
 * Cephes-style polynomial as 0x5ed220 but every step is one fused
 * multiply-add, so the rounding differs from the separate mul/add form by
 * up to one ulp.  That difference is audible in the factory chain: the
 * result feeds the per-band limit comparison in CMultiBand mode 0 and
 * changes the band signal path.  The live reference capture was made on a
 * level-9 CPU, so this is the variant the port must reproduce.
 *
 * Constants are the vectors at RVA 0x1111a1c0..0x1111a580 (identical in
 * every lane).  Inputs outside the fast-path range take a scalar slow path
 * in the image that is not reproduced here. */
static inline float xps_mb_logf(float x)
{
    const float c0 = -0.12891685962677002f;
    const float c1 = 0.1398443728685379f;
    const float c2 = -0.12184260785579681f;
    const float c3 = 0.140058696269989f;
    const float c4 = -0.16680487990379333f;
    const float c5 = 0.20010405778884888f;
    const float c6 = -0.24999797344207764f;
    const float c7 = 0.3333321511745453f;
    const float c8 = -0.5f;
    const float hi = 0.693145751953125f;
    const float lo = 1.428606765330187e-06f;
    int32_t ix, a;
    float z, f, p, res;

    memcpy(&ix, &x, sizeof ix);
    a = ix - 0x3f2aaaab;
    {
        int32_t mb = (a & 0x7fffff) + 0x3f2aaaab;
        float mz;
        memcpy(&mz, &mb, sizeof mz);
        z = mz - 1.0f;
    }
    f = (float)(a >> 23);

    p = c0;
    p = fmaf(p, z, c1);
    p = fmaf(p, z, c2);
    p = fmaf(p, z, c3);
    p = fmaf(p, z, c4);
    p = fmaf(p, z, c5);
    p = fmaf(p, z, c6);
    p = fmaf(p, z, c7);
    p = fmaf(p, z, c8);
    res = z * p;
    res = fmaf(res, z, z);

    res = fmaf(f, lo, res);
    res = fmaf(f, hi, res);
    return res;
}

/* Domain predicates matching the paths reproduced above. */
static inline int xps_mb_powf_domain_ok(float base)
{
    uint32_t b;
    memcpy(&b, &base, sizeof b);
    return b > 0x00800000u && b < 0x7f800000u;   /* positive normal */
}

static inline int xps_mb_logf_domain_ok(float x)
{
    uint32_t b;
    memcpy(&b, &x, sizeof b);
    return b > 0x00800000u && b < 0x7f800000u;   /* positive normal */
}

#endif /* XPS_MB_MATH_H */
