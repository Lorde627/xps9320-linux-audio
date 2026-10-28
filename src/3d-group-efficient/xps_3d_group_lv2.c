/* Offline research LV2 adapter for the factory CMaxx3DSt group.
 *
 * The bundle carries the live banks from the preset-injected wrapper snapshot:
 *   pbank.bin       four-band producer bank (0x48)
 *   abank.bin       3D main bank (0x120)
 *   bbank.bin       3D biquad-1 bank (0x20)
 *   cbank.bin       3D biquad-2 bank (0x20)
 *   bass.bin        top-level MaxxBass processing bank (0x100)
 *   bass-state.bin  its live state block (0x800, state at +0x4bc)
 *
 * Per frame the aggregate does: producer(r14) -> bus, MaxxBass(bus) -> r14,
 * 3D(bus) -> r14 +=.  Only the combination reconstructs the full band.
 *
 * Packaged for the XPS 9320 Linux playback chain.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lv2/core/lv2.h>
#include <xmmintrin.h>

#include "xps_3d_stage.h"
#include "xps_bass_kernel.h"
#include "xps_rate.h"

#ifndef XPS_3D_URI
#define XPS_3D_URI "urn:xps9320:research-3d-group-efficient"
#endif

struct instance {
    xps_3d_stage_state stage;
    xps_3d_stage_cfg cfg;
    float bass_params[0x100 / 4];
    float bass_state[0x800 / 4];
    float bass_state0[0x800 / 4];
    const float *in[2];
    float *out[2];
    int failed;
};

static int load(const char *dir, const char *name, void *dst, size_t bytes)
{
    char path[4096];
    int n = snprintf(path, sizeof path, "%s%s", dir, name);
    if (n < 0 || (size_t)n >= sizeof path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = fread(dst, 1, bytes, f) == bytes && fgetc(f) == EOF;
    fclose(f);
    return ok;
}

/* Load one rate-tagged blob.  There is deliberately no cross-rate fallback:
 * a 44.1 kHz instance never opens a 48 kHz file, and a missing or wrong-sized
 * file is reported by name before instantiate returns NULL. */
static int load_rate_blob(const char *dir, const char *base, enum xps_rate rate,
                          void *dst, size_t bytes)
{
    char name[256];
    if (xps_rate_blob_name(name, sizeof name, base, rate) < 0 ||
        !load(dir, name, dst, bytes)) {
        fprintf(stderr,
                "xps9320: %s: %s missing or malformed at %.0f Hz; refusing to "
                "fall back to another rate's data\n",
                XPS_3D_URI, name, rate == XPS_RATE_44100 ? 44100. : 48000.);
        return 0;
    }
    return 1;
}

/* The 44.1 kHz set is a factory capture, not a derivation, and its integer
 * delay fields are part of the identity of that capture.  Checking them here
 * means a 48 kHz abank copied under a -44100 name is rejected as malformed
 * instead of silently playing 48 kHz sample counts at 44.1 kHz. */
static int validate_44100_abank(const unsigned char *abank)
{
    int32_t v_b8, v_bc, v_fc, v_100, v_e0;
    memcpy(&v_fc, abank + 0x0fc, 4);
    memcpy(&v_100, abank + 0x100, 4);
    memcpy(&v_b8, abank + 0x0b8, 4);
    memcpy(&v_bc, abank + 0x0bc, 4);
    memcpy(&v_e0, abank + 0x0e0, 4);
    if (v_fc != 6 || v_100 != 3 || v_b8 != 529 || v_bc != 558 || v_e0 != 1190) {
        fprintf(stderr,
                "xps9320: %s: abank-44100.bin is not the validated 44.1 kHz "
                "capture (delay fields 0xfc=%d 0x100=%d 0xb8=%d 0xbc=%d "
                "0xe0=%d); refusing a wrong-rate or corrupt bank\n",
                XPS_3D_URI, v_fc, v_100, v_b8, v_bc, v_e0);
        return 0;
    }
    return 1;
}

static LV2_Handle instantiate(const LV2_Descriptor *d, double rate,
                              const char *path, const LV2_Feature *const *features)
{
    (void)d;
    (void)features;
    enum xps_rate selected = xps_rate_select(
        XPS_3D_URI, rate, XPS_RATES_48000_AND_44100,
        "the six group blobs are captured per rate; abank.bin also carries "
        "sample-count delay fields, so a bank from another rate is never used");
    if (selected == XPS_RATE_UNSUPPORTED) return NULL;
    struct instance *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    unsigned char pbank[0x48], abank[0x120], bbank[0x20], cbank[0x20];
    unsigned char bassp[0x100];
    if (!load_rate_blob(path, "pbank", selected, pbank, sizeof pbank) ||
        !load_rate_blob(path, "abank", selected, abank, sizeof abank) ||
        !load_rate_blob(path, "bbank", selected, bbank, sizeof bbank) ||
        !load_rate_blob(path, "cbank", selected, cbank, sizeof cbank) ||
        !load_rate_blob(path, "bass", selected, bassp, sizeof bassp) ||
        !load_rate_blob(path, "bass-state", selected, s->bass_state0,
                        sizeof s->bass_state0)) {
        free(s);
        return NULL;
    }
    if (selected == XPS_RATE_44100 && !validate_44100_abank(abank)) {
        free(s);
        return NULL;
    }
    memcpy(s->bass_params, bassp, sizeof bassp);
    xps_3d_producer_load_cfg(pbank, 2, &s->cfg.producer);
    xps_3d_load_cfg(abank, bbank, cbank, 256, 255, &s->cfg.dsp);
    return s;
}

static void connect_port(LV2_Handle h, uint32_t port, void *data)
{
    struct instance *s = h;
    if (port < 2) s->in[port] = data;
    else if (port < 4) s->out[port - 2] = data;
}

static void activate(LV2_Handle h)
{
    struct instance *s = h;
    memset(&s->stage, 0, sizeof s->stage);
    memcpy(s->bass_state, s->bass_state0, sizeof s->bass_state);
    s->failed = 0;
}

static void run(LV2_Handle h, uint32_t count)
{
    struct instance *s = h;
    if (!count) return;
    unsigned saved = _mm_getcsr();
    _mm_setcsr(0x9fc0u);
    for (uint32_t n = 0; n < count; n++) {
        float l = s->in[0][n], r = s->in[1][n];
        if (s->failed || !isfinite(l) || !isfinite(r)) {
            s->failed = 1;
            s->out[0][n] = 0.f;
            s->out[1][n] = 0.f;
            continue;
        }
        float in[2] = {l, r};
        float bus[8];
        xps_3d_producer_process(&s->stage.producer, &s->cfg.producer, in, bus);
        xps_bass_kernel_ctx k = {s->bass_state, s->bass_params,
                                 ((unsigned char *)s->bass_params)[0xda] == 1, 0};
        float out[2] = {0.f, 0.f};
        xps_bass_kernel_process(&k, bus, out);
        if (k.error) {
            s->failed = 1;
            s->out[0][n] = 0.f;
            s->out[1][n] = 0.f;
            continue;
        }
        xps_3d_process(&s->stage.dsp, &s->cfg.dsp, bus, out);
        if (!isfinite(out[0]) || !isfinite(out[1])) {
            s->failed = 1;
            s->out[0][n] = 0.f;
            s->out[1][n] = 0.f;
            continue;
        }
        s->out[0][n] = out[0];
        s->out[1][n] = out[1];
    }
    _mm_setcsr(saved);
}

static void cleanup(LV2_Handle h) { free(h); }

static const LV2_Descriptor descriptor = {
    XPS_3D_URI, instantiate, connect_port, activate, run, NULL, cleanup, NULL
};

LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index)
{
    return index ? NULL : &descriptor;
}
