/* Offline research LV2 adapter for the validated CMaxxHfSt stage.
 *
 * hf.bin / poly.bin are the 48 kHz preset banks extracted from the
 * preset-injected wrapper snapshot. hf-44100.bin / poly-44100.bin are the
 * 44.1 kHz banks captured by the same wrapper constructor with the host media
 * type set to 44100 Hz and the same MFX preset applied. The two sets are
 * rate-specific: there is no cross-rate fallback, and a missing or malformed
 * file fails instantiate with the file name in the diagnostic. Not installed
 * or enabled anywhere. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <lv2/core/lv2.h>
#include <xmmintrin.h>
#include "xps_hf_kernel.h"
#include "xps_rate.h"

#ifndef XPS_URI
#define XPS_URI "urn:xps9320:research-hf-stage"
#endif

struct instance {
    float hf[0x20 / 4];
    float poly[0x20 / 4];
    xps_hf_state state;
    xps_hf_cfg cfg;
    const float *in[2];
    float *out[2];
    int failed;
};

static int read_bank(const char *path, const char *name, float *dst, size_t bytes,
                     double rate) {
    char filename[4096];
    int n = snprintf(filename, sizeof(filename), "%s%s", path, name);
    if (n < 0 || (size_t)n >= sizeof(filename)) {
        fprintf(stderr, "xps9320: " XPS_URI ": bank name %s is too long; refusing\n",
                name);
        return 0;
    }
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "xps9320: " XPS_URI ": %s missing at %.0f Hz; refusing to "
                        "fall back to another rate's coefficients\n",
                name, rate);
        return 0;
    }
    int ok = fread(dst, 1, bytes, f) == bytes && fgetc(f) == EOF;
    fclose(f);
    if (!ok)
        fprintf(stderr, "xps9320: " XPS_URI ": %s malformed at %.0f Hz; refusing to "
                        "fall back to another rate's coefficients\n",
                name, rate);
    return ok;
}

static void set_cfg(struct instance *s) {
    s->cfg.c = s->hf[0];
    s->cfg.side_l = s->hf[1];
    s->cfg.side_r = s->hf[2];
    s->cfg.out_l = s->hf[3];
    s->cfg.out_r = s->hf[4];
    s->cfg.attack = s->poly[1];
    s->cfg.release = s->poly[0];
    s->cfg.bias = s->poly[5];
}

static void reset(struct instance *s) {
    s->state.split_r = 0.0f;
    s->state.split_l = 0.0f;
    s->state.gain = 1.0f;
    s->failed = 0;
}

static LV2_Handle instantiate(const LV2_Descriptor *d, double rate, const char *path,
                              const LV2_Feature *const *features) {
    (void)d; (void)features;
    enum xps_rate rate_id = xps_rate_select(
            XPS_URI, rate, XPS_RATES_48000_AND_44100,
            "hf-44100.bin/poly-44100.bin are the 44100 Hz factory captures and "
            "hf.bin/poly.bin are the 48000 Hz captures from the same preset path");
    if (rate_id == XPS_RATE_UNSUPPORTED) return NULL;
    struct instance *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    char hf_name[64], poly_name[64];
    int hf_named = xps_rate_blob_name(hf_name, sizeof(hf_name), "hf", rate_id);
    int poly_named = xps_rate_blob_name(poly_name, sizeof(poly_name), "poly", rate_id);
    if (hf_named < 0 || (size_t)hf_named >= sizeof(hf_name) ||
        poly_named < 0 || (size_t)poly_named >= sizeof(poly_name)) {
        fprintf(stderr, "xps9320: " XPS_URI ": cannot name %s rate banks\n", "HF");
        free(s);
        return NULL;
    }
    if (!read_bank(path, hf_name, s->hf, sizeof(s->hf), rate) ||
        !read_bank(path, poly_name, s->poly, sizeof(s->poly), rate)) {
        free(s);
        return NULL;
    }
    set_cfg(s);
    reset(s);
    return s;
}

static void connect_port(LV2_Handle h, uint32_t port, void *data) {
    struct instance *s = h;
    if (port < 2) s->in[port] = data;
    else if (port < 4) s->out[port - 2] = data;
}

static void activate(LV2_Handle h) { reset(h); }

static void run(LV2_Handle h, uint32_t count) {
    struct instance *s = h;
    if (!count) return;
    if (s->failed) {
        for (uint32_t n = 0; n < count; n++) { s->out[0][n] = 0.f; s->out[1][n] = 0.f; }
        return;
    }
    unsigned saved = _mm_getcsr();
    _mm_setcsr(0x1f80u);
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
        float ol, orr;
        xps_hf_process(&s->state, &s->cfg, l, r, &ol, &orr);
        s->out[0][n] = ol;
        s->out[1][n] = orr;
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
