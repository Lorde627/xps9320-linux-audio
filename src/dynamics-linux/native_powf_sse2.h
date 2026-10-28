/* Standalone positive-normal finite SSE2 powf transcription, C11.
 *
 * API: xps_powf_positive(x, y, &result) returns XPS_POWF_OK (0) on success;
 * every failure leaves *result unchanged. No allocation, I/O, PE loading, or
 * libm calls. Compile with -ffp-contract=off -fno-fast-math, SSE2 evaluation.
 * Caller must use masked SSE2 exceptions, round-to-nearest-even, FTZ/DAZ off;
 * the function checks MXCSR but never changes it (FP status flags may accrue).
 *
 * Domain: positive normal binary32 base, finite binary32 exponent, recovered
 * exponent guard/scale paths, normal finite binary32 result. x==1 and y==0
 * return 1 after input/environment validation. Other exceptional paths fail
 * closed. This is not a general powf replacement. As in the Python source,
 * BSR(0) is explicitly assumed to yield zero for the reduced integer exponent;
 * original x86 BSR does not architecturally define its destination for zero.
 * Original-backed validation covers x in [1e-5,8], y in [-1,0.5], 27846 pairs;
 * broader Python comparisons do not establish original behavior outside that.
 *
 * Provenance (no original executable code is included/executed):
 * Python: scripts/native_powf_sse2.py
 * SHA256 f2ab1835b8a41ca100a3f6e9d59f63bf81e5567ecc82aa2508ea3a1bd64d5969
 * Original image: artifacts/MaxxAudioRender64.dll
 * SHA256 131ea4476391b8f12dfc04991137e1c4d86d17dabd643cfcef02f4e5a6ce0065
 * Backend VA 0x1805d1a90; read-only table VA 0x1810e8f00, length 0x560.
 * Table saved at powf-finite-probe-20261027-a/table.bin, little-endian words.
 * SHA256 3035fba996e10fa7573ce1d2f5e0b185eb41dbaefcb49c2ea0c92d577a5892ce
 * Accepted inputs: powf-finite-probe-20261027-a/inputs.bin (27846 <2f pairs)
 * SHA256 d0a30afc909572df42cbc23480927cbc07fed35e1d835dfa0e0c07525845dcbd
 * Accepted native.bin and capture/result.bin (27846 <f outputs), both SHA256
 * c542f5171e0039a40a8469fe6cced251b849c67c460a39298bddbc9dd3f888ac.
 */
#ifndef XPS_NATIVE_POWF_SSE2_H
#define XPS_NATIVE_POWF_SSE2_H

#include <float.h>
#include <stdint.h>
#include <string.h>
#include <xmmintrin.h>
#include <emmintrin.h>

#if !defined(__SSE2__) || defined(__FAST_MATH__)
#error "xps_powf_positive requires SSE2 and -fno-fast-math -ffp-contract=off"
#endif
#if FLT_RADIX != 2 || FLT_MANT_DIG != 24 || DBL_MANT_DIG != 53 || FLT_EVAL_METHOD != 0
#error "xps_powf_positive requires binary32/binary64 without excess precision"
#endif
_Static_assert(sizeof(float) == 4 && sizeof(double) == 8, "IEEE storage required");

enum xps_powf_status {
    XPS_POWF_OK = 0,
    XPS_POWF_INVALID_ARGUMENT = 1,
    XPS_POWF_UNSUPPORTED_INPUT = 2,
    XPS_POWF_UNSUPPORTED_ENVIRONMENT = 3,
    XPS_POWF_UNSUPPORTED_GUARD = 4,
    XPS_POWF_UNSUPPORTED_SCALE = 5,
    XPS_POWF_UNSUPPORTED_RESULT = 6
};

/* Generated from the checked saved table; comments are byte offsets. */
static const uint64_t xps_powf_table_words[0x560 / 8] = {
    UINT64_C(0x3ff71547652b82fe), UINT64_C(0x0000000000000000), UINT64_C(0x3ff6ba5ded75ac4d), UINT64_C(0x3f96e79685c2c000), /* 0x000 */
    UINT64_C(0x3ff66235b77002e7), UINT64_C(0x3fa6bad3758f0000), UINT64_C(0x3ff60caf2ef7e44b), UINT64_C(0x3fb0eb389fa2a000), /* 0x020 */
    UINT64_C(0x3ff5b9ac9b743f0d), UINT64_C(0x3fb663f6fac91000), UINT64_C(0x3ff56911fd6002c7), UINT64_C(0x3fbbc84240adb000), /* 0x040 */
    UINT64_C(0x3ff51ac4eec8b247), UINT64_C(0x3fc08c588cda7800), UINT64_C(0x3ff4ceac86768bb6), UINT64_C(0x3fc32ae9e278b000), /* 0x060 */
    UINT64_C(0x3ff484b13d7c02a9), UINT64_C(0x3fc5c01a39fbd800), UINT64_C(0x3ff43cbcd6f18b64), UINT64_C(0x3fc84c2bd02f0000), /* 0x080 */
    UINT64_C(0x3ff3f6ba49a91758), UINT64_C(0x3fcacf5e2db4f000), UINT64_C(0x3ff3b295abaa3ffe), UINT64_C(0x3fcd49ee4c325800), /* 0x0a0 */
    UINT64_C(0x3ff3703c1f4d0ffe), UINT64_C(0x3fcfbc16b9026800), UINT64_C(0x3ff32f9bc1cdb958), UINT64_C(0x3fd11307dad30c00), /* 0x0c0 */
    UINT64_C(0x3ff2f0a39b3764eb), UINT64_C(0x3fd24407ab0e0800), UINT64_C(0x3ff2b3438f87b4a7), UINT64_C(0x3fd37124cea4cc00), /* 0x0e0 */
    UINT64_C(0x3ff2776c50ef9bfe), UINT64_C(0x3fd49a784bcd1c00), UINT64_C(0x3ff23d0f5318e5ec), UINT64_C(0x3fd5c01a39fbd800), /* 0x100 */
    UINT64_C(0x3ff2041ebf5a27cd), UINT64_C(0x3fd6e221cd9d0c00), UINT64_C(0x3ff1cc8d69c50564), UINT64_C(0x3fd800a563161c00), /* 0x120 */
    UINT64_C(0x3ff1964ec6fc9491), UINT64_C(0x3fd91bba891f1800), UINT64_C(0x3ff16156e2c365a4), UINT64_C(0x3fda33760a7f6000), /* 0x140 */
    UINT64_C(0x3ff12d9a57323dc3), UINT64_C(0x3fdb47ebf7388400), UINT64_C(0x3ff0fb0e4489f08c), UINT64_C(0x3fdc592fad295c00), /* 0x160 */
    UINT64_C(0x3ff0c9a84994022d), UINT64_C(0x3fdd6753e032ec00), UINT64_C(0x3ff0995e7c86d702), UINT64_C(0x3fde726aa1e75400), /* 0x180 */
    UINT64_C(0x3ff06a2764633554), UINT64_C(0x3fdf7a8568cb0800), UINT64_C(0x3ff03bf9f2c1c437), UINT64_C(0x3fe03fda8b979a00), /* 0x1a0 */
    UINT64_C(0x3ff00ecd7e080215), UINT64_C(0x3fe0c10500d63a00), UINT64_C(0x3fefc53377f9d292), UINT64_C(0x3fe140c9faa1e600), /* 0x1c0 */
    UINT64_C(0x3fef6ead796c4570), UINT64_C(0x3fe1bf311e95d000), UINT64_C(0x3fef19f9cbae7ffd), UINT64_C(0x3fe23c41d4272800), /* 0x1e0 */
    UINT64_C(0x3feec709dc3a03fd), UINT64_C(0xbfda8ff971810c00), UINT64_C(0x3fee75cfb25e5dae), UINT64_C(0xbfd99b072a96c800), /* 0x200 */
    UINT64_C(0x3fee263de767da1d), UINT64_C(0xbfd8a8980abfbc00), UINT64_C(0x3fedd8479f4003df), UINT64_C(0xbfd7b89f02cf2c00), /* 0x220 */
    UINT64_C(0x3fed8be0817f5ffe), UINT64_C(0xbfd6cb0f6865c800), UINT64_C(0x3fed40fcb2e891bc), UINT64_C(0xbfd5dfdcf1eeb000), /* 0x240 */
    UINT64_C(0x3fecf790cf45a967), UINT64_C(0xbfd4f6fbb2cec400), UINT64_C(0x3fecaf91e3a0f252), UINT64_C(0xbfd4106017c3ec00), /* 0x260 */
    UINT64_C(0x3fec68f568d31760), UINT64_C(0xbfd32bfee370f000), UINT64_C(0x3fec23b13e60edb5), UINT64_C(0xbfd249cd2b13cc00), /* 0x280 */
    UINT64_C(0x3febdfbba5a3a303), UINT64_C(0xbfd169c05363f000), UINT64_C(0x3feb9d0b3d3671a3), UINT64_C(0xbfd08bce0d95fc00), /* 0x2a0 */
    UINT64_C(0x3feb5b96fca558e1), UINT64_C(0xbfcf5fd8a9064000), UINT64_C(0x3feb1b563058ac9d), UINT64_C(0xbfcdac22d3e44000), /* 0x2c0 */
    UINT64_C(0x3feadc4075b99d15), UINT64_C(0xbfcbfc67a7fff800), UINT64_C(0x3fea9e4db78c1f20), UINT64_C(0xbfca5094b54d2800), /* 0x2e0 */
    UINT64_C(0x3fea61762a7aded9), UINT64_C(0xbfc8a8980abfc000), UINT64_C(0x3fea25b249d2231b), UINT64_C(0xbfc7046031c7a000), /* 0x300 */
    UINT64_C(0x3fe9eafad466bffe), UINT64_C(0xbfc563dc29ffb000), UINT64_C(0x3fe9b148c9a669bb), UINT64_C(0xbfc3c6fb650ce000), /* 0x320 */
    UINT64_C(0x3fe9789566cee8d2), UINT64_C(0xbfc22dadc2ab3800), UINT64_C(0x3fe940da2449dbe4), UINT64_C(0xbfc097e38ce60800), /* 0x340 */
    UINT64_C(0x3fe90a10b32adc32), UINT64_C(0xbfbe0b1ae8f30000), UINT64_C(0x3fe8d432facdfeeb), UINT64_C(0xbfbaed391ab66000), /* 0x360 */
    UINT64_C(0x3fe89f3b1694cffe), UINT64_C(0xbfb7d60496cfc000), UINT64_C(0x3fe86b2353c0032a), UINT64_C(0xbfb4c560fe68b000), /* 0x380 */
    UINT64_C(0x3fe837e62f643580), UINT64_C(0xbfb1bb32a6005000), UINT64_C(0x3fe8057e54783511), UINT64_C(0xbfad6ebd1f1fe000), /* 0x3a0 */
    UINT64_C(0x3fe7d3e699fb5dee), UINT64_C(0xbfa77394c9d96000), UINT64_C(0x3fe7a31a0132b331), UINT64_C(0xbfa184b8e4c56000), /* 0x3c0 */
    UINT64_C(0x3fe77313b3fb70c1), UINT64_C(0xbf9743ee861f4000), UINT64_C(0x3fe743cf0331e6cc), UINT64_C(0xbf872c7ba20f8000), /* 0x3e0 */
    UINT64_C(0x3fe71547652b82fe), UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000), UINT64_C(0x0000000000000000), /* 0x400 */
    UINT64_C(0x3ff0000000000000), UINT64_C(0x3ff059b0d3158574), UINT64_C(0x3ff0b5586cf9890f), UINT64_C(0x3ff11301d0125b51), /* 0x420 */
    UINT64_C(0x3ff172b83c7d517b), UINT64_C(0x3ff1d4873168b9aa), UINT64_C(0x3ff2387a6e756238), UINT64_C(0x3ff29e9df51fdee1), /* 0x440 */
    UINT64_C(0x3ff306fe0a31b715), UINT64_C(0x3ff371a7373aa9cb), UINT64_C(0x3ff3dea64c123422), UINT64_C(0x3ff44e086061892d), /* 0x460 */
    UINT64_C(0x3ff4bfdad5362a27), UINT64_C(0x3ff5342b569d4f82), UINT64_C(0x3ff5ab07dd485429), UINT64_C(0x3ff6247eb03a5585), /* 0x480 */
    UINT64_C(0x3ff6a09e667f3bcd), UINT64_C(0x3ff71f75e8ec5f74), UINT64_C(0x3ff7a11473eb0187), UINT64_C(0x3ff82589994cce13), /* 0x4a0 */
    UINT64_C(0x3ff8ace5422aa0db), UINT64_C(0x3ff93737b0cdc5e5), UINT64_C(0x3ff9c49182a3f090), UINT64_C(0x3ffa5503b23e255d), /* 0x4c0 */
    UINT64_C(0x3ffae89f995ad3ad), UINT64_C(0x3ffb7f76f2fb5e47), UINT64_C(0x3ffc199bdd85529c), UINT64_C(0x3ffcb720dcef9069), /* 0x4e0 */
    UINT64_C(0x3ffd5818dcfba487), UINT64_C(0x3ffdfc97337b9b5f), UINT64_C(0x3ffea4afa2a490da), UINT64_C(0x3fff50765b6e4540), /* 0x500 */
    UINT64_C(0x3fa7a3341fac5e29), UINT64_C(0x3fc47fd3ffac83b4), UINT64_C(0xbfb55046a1437890), UINT64_C(0xbfd62e42fefa39ef), /* 0x520 */
    UINT64_C(0x3f83b2ab6fba4e77), UINT64_C(0x3fcebfbdff82c58e), UINT64_C(0x3fac6b08d704a0bf), UINT64_C(0x3fe62e42fefa39ef), /* 0x540 */
};

static inline double xps_powf_from64(uint64_t bits)
{
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline uint64_t xps_powf_bits64(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline uint32_t xps_powf_bits32(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline double xps_powf_table(unsigned byte_offset)
{
    return xps_powf_from64(xps_powf_table_words[byte_offset / 8]);
}

static inline int xps_powf_positive(float base, float power, float *result)
{
    const uint32_t bx = xps_powf_bits32(base);
    const uint32_t by = xps_powf_bits32(power);
    unsigned upper, index, magnitude, bsr = 0, yexp;
    int exponent, guard;
    double mant, reciprocal, logarithm, center, reduced, low, high, square;
    double main, correction, rounded, grid, residual, scale, exp_table;
    double squared, product, polynomial, value;
    uint32_t word, output_bits;
    int64_t signedword, scale_exponent, scale_word;
    float narrowed;

    if (!result)
        return XPS_POWF_INVALID_ARGUMENT;
    if (bx < UINT32_C(0x00800000) || bx > UINT32_C(0x7f7fffff) ||
        (by & UINT32_C(0x7fffffff)) >= UINT32_C(0x7f800000))
        return XPS_POWF_UNSUPPORTED_INPUT;
    if ((_mm_getcsr() & 0xffc0u) != 0x1f80u)
        return XPS_POWF_UNSUPPORTED_ENVIRONMENT;
    if (bx == UINT32_C(0x3f800000) || (by & UINT32_C(0x7fffffff)) == 0) {
        *result = 1.0f;
        return XPS_POWF_OK;
    }

    mant = xps_powf_from64(UINT64_C(0x3ff0000000000000) | ((uint64_t)bx << 29));
    upper = bx >> 16;
    index = ((upper & 0x7fu) + 1u) & 0xfeu;
    reciprocal = xps_powf_table(index * 8);
    logarithm = xps_powf_table(index * 8 + 8);
    exponent = (int)((upper + 0x41u) >> 7) - 0x7f;
    center = xps_powf_from64((uint64_t)(0x1ff80u + index) << 45);
    reduced = (mant - center) * reciprocal;
    logarithm = logarithm + reduced;
    logarithm = logarithm + (double)exponent;
    low = xps_powf_table(0x520) * reduced + xps_powf_table(0x530);
    high = xps_powf_table(0x528) * reduced + xps_powf_table(0x538);
    square = reduced * reduced;
    magnitude = (unsigned)(exponent < 0 ? -exponent : exponent);
    while (magnitude > 1) {
        ++bsr;
        magnitude >>= 1;
    }
    yexp = (unsigned)((xps_powf_bits64((double)power) >> 52) & 0x7ffu);
    guard = (int)bsr - 0x3e1 + (int)yexp;
    if ((uint32_t)guard >= 0x23u) {
        if (guard < 0 || guard >= 0x26)
            return XPS_POWF_UNSUPPORTED_GUARD;
        high = high * square;
        low = low * (square * square);
        logarithm = logarithm + high;
        main = logarithm * (double)power;
        correction = (double)power * low;
    } else {
        double fourth;
        main = logarithm * (double)power;
        fourth = square * square;
        low = low * fourth;
        high = high * square;
        correction = (double)power * (low + high);
    }
    rounded = main + xps_powf_from64(UINT64_C(0x42e8000000000000));
    word = (uint32_t)xps_powf_bits64(rounded);
    grid = rounded - xps_powf_from64(UINT64_C(0x42e8000000000000));
    residual = main - grid;
    residual = residual + correction;
    signedword = word < UINT32_C(0x80000000) ? (int64_t)word :
                 (int64_t)word - INT64_C(0x100000000);
    /* floor(signedword / 32) * 16 matches (signedword >> 1) & ~15
       without implementation-defined signed shifts or negative bit masks. */
    scale_exponent = signedword >= 0 ? signedword / 32 : -((-signedword + 31) / 32);
    scale_word = 0x3ff0 + scale_exponent * 16;
    if (scale_word <= 0 || scale_word >= 0x7ff0)
        return XPS_POWF_UNSUPPORTED_SCALE;
    scale = xps_powf_from64((uint64_t)scale_word << 48);
    exp_table = xps_powf_table(0x420 + (word & 31u) * 8);
    low = xps_powf_table(0x540) * residual + xps_powf_table(0x550);
    high = xps_powf_table(0x548) * residual + xps_powf_table(0x558);
    squared = residual * residual;
    product = residual * exp_table;
    exp_table = exp_table * scale;
    polynomial = squared * low;
    product = product * scale;
    value = high + polynomial;
    value = value * product;
    value = exp_table + value;
    /* SSE2 narrowing has defined masked overflow behavior; C's out-of-range
       double-to-float cast is avoided. Reject zero/subnormal/inf/NaN outputs. */
    narrowed = _mm_cvtss_f32(_mm_cvtsd_ss(_mm_setzero_ps(), _mm_set_sd(value)));
    output_bits = xps_powf_bits32(narrowed);
    if (output_bits < UINT32_C(0x00800000) || output_bits > UINT32_C(0x7f7fffff))
        return XPS_POWF_UNSUPPORTED_RESULT;
    *result = narrowed;
    return XPS_POWF_OK;
}
#endif
