/* Offline research LV2 adapter for the factory CVarislopeXover step.
 *
 * The top-level MFX block processor 0x1800825e0 runs, for mode 0/2, an input
 * copy (bus x wrapper gain) followed by one call per frame to the object at
 * wrapper+0x40b710, dispatched through the 0x40bb90 table to 0x180107230.
 * That step keeps double-precision state per band and channel and applies up
 * to four bands, each a cascade of two direct-form-I biquads, in sequence.
 *
 * xover-bank.bin is the live bank (0x2c0 bytes) taken from the preset-injected
 * wrapper snapshot; band 0 is the only enabled band and is a two-section
 * high-pass. Not installed or enabled anywhere.
 *
 * Every section in the bank is a bilinear design (band 0: 99 Hz 4th-order
 * Butterworth high-pass, the only enabled band; band 1: 16 kHz 2nd-order
 * low-pass, disabled; bands 2-3: identity), so the bank is rate-specific: 48 kHz
 * comes from the capture, 44.1 kHz is derived from it by
 * scripts/derive_xover_bank_44100.py. This is the only stage in the chain whose
 * 44.1 kHz data exists today; every other adapter is still 48 kHz only.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lv2/core/lv2.h>
#include <xmmintrin.h>
#include "xps_rate.h"

#ifndef XPS_URI
#define XPS_URI "urn:xps9320:research-input-xover"
#endif

#define BAND_COUNT 4
#define BAND_BYTES 0xa0
#define BANK_BYTES 0x2c0
#define BAND_STATE 8 /* s0,s1 per channel x two sections */

struct instance {
    unsigned char bank[BANK_BYTES];
    double state[BAND_COUNT * BAND_STATE];
    const float *in[2];
    float *out[2];
    int failed;
};

static int read_bank(const char *path, const char *name, unsigned char *dst, size_t bytes) {
    char filename[4096];
    int n = snprintf(filename, sizeof(filename), "%s%s", path, name);
    if (n < 0 || (size_t)n >= sizeof(filename)) return 0;
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;
    int ok = fread(dst, 1, bytes, f) == bytes && fgetc(f) == EOF;
    fclose(f);
    return ok;
}

static void reset(struct instance *s) {
    for (unsigned i = 0; i < BAND_COUNT * BAND_STATE; i++) s->state[i] = 0.0;
    s->failed = 0;
}

static LV2_Handle instantiate(const LV2_Descriptor *d, double rate, const char *path,
                              const LV2_Feature *const *features) {
    (void)d; (void)features;
    enum xps_rate rate_id = xps_rate_select(
            XPS_URI, rate, XPS_RATES_48000_AND_44100,
            "xover-bank.bin is the 48000 Hz capture and xover-bank-44100.bin is "
            "its derived 44100 Hz counterpart; other rates would need a new bank");
    if (rate_id == XPS_RATE_UNSUPPORTED) return NULL;
    struct instance *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    char bank_name[64];
    int named = xps_rate_blob_name(bank_name, sizeof(bank_name), "xover-bank", rate_id);
    if (named < 0 || (size_t)named >= sizeof(bank_name)) {
        free(s);
        return NULL;
    }
    if (!read_bank(path, bank_name, s->bank, sizeof(s->bank))) {
        fprintf(stderr, "xps9320: " XPS_URI ": %s missing or malformed at %.0f Hz; "
                        "refusing to fall back to another rate's coefficients\n",
                bank_name, rate);
        free(s);
        return NULL;
    }
    reset(s);
    return s;
}

static void connect_port(LV2_Handle h, uint32_t port, void *data) {
    struct instance *s = h;
    if (port < 2) s->in[port] = data;
    else if (port < 4) s->out[port - 2] = data;
}

static void activate(LV2_Handle h) { reset(h); }

/* One frame, mirroring 0x180107230: bands run in sequence, each takes the
 * previous band's output, a band runs only when its enable word is set and its
 * selector equals the caller's channel argument (always 0 here). */
static void frame(struct instance *s, const float *in, float *out) {
    const unsigned char *bank = s->bank;
    double v[2] = {in[0], in[1]};
    for (int band = 0; band < BAND_COUNT; band++) {
        const unsigned char *block = bank + BAND_BYTES * band;
        double *st = s->state + BAND_STATE * band;
        int enabled, selector;
        memcpy(&enabled, bank + 0x280 + 4 * band, 4);
        memcpy(&selector, bank + 0x290 + 4 * band, 4);
        if (enabled == 0 || selector != 0) continue;
        for (int stage = 0; stage < 2; stage++) {
            double c[5], next[2];
            for (int i = 0; i < 5; i++)
                memcpy(&c[i], block + 0x50 * stage + 0x10 * i, 8);
            for (int lane = 0; lane < 2; lane++) {
                double s0 = st[4 * stage + lane];
                double s1 = st[4 * stage + 2 + lane];
                double u = v[lane] - (c[3] * s1 + c[4] * s0);
                next[lane] = ((c[0] * u) + c[1] * s1) + c[2] * s0;
                st[4 * stage + lane] = s1;
                st[4 * stage + 2 + lane] = u;
            }
            v[0] = next[0]; v[1] = next[1];
        }
    }
    out[0] = (float)v[0];
    out[1] = (float)v[1];
}

static void run(LV2_Handle h, uint32_t count) {
    struct instance *s = h;
    if (!count) return;
    if (s->failed) {
        for (uint32_t n = 0; n < count; n++) { s->out[0][n] = 0.f; s->out[1][n] = 0.f; }
        return;
    }
    unsigned saved = _mm_getcsr();
    _mm_setcsr(saved | 0x8040u); /* the factory enables FTZ and DAZ here */
    for (uint32_t n = 0; n < count; n++) {
        if (s->failed) {
            s->out[0][n] = 0.f;
            s->out[1][n] = 0.f;
            continue;
        }
        float l = s->in[0][n], r = s->in[1][n];
        if (!isfinite(l) || !isfinite(r)) {
            s->failed = 1;
            s->out[0][n] = 0.f;
            s->out[1][n] = 0.f;
            continue;
        }
        float in[2] = {l, r}, o[2];
        frame(s, in, o);
        s->out[0][n] = o[0];
        s->out[1][n] = o[1];
    }
    _mm_setcsr(saved);
}

static void cleanup(LV2_Handle h) { free(h); }

static const LV2_Descriptor descriptor = {
    XPS_URI, instantiate, connect_port, activate, run, NULL, cleanup, NULL
};

LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index) {
    return index ? NULL : &descriptor;
}
