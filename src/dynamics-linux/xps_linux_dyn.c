/* Offline research LV2 adapter for the factory dynamics sub-chain.
 *
 * The aggregate's sub-chain 0x180084a60 runs, per stereo frame on one
 * in-place bus:
 *
 *   CMultiBand mode 0  (pre 0x18018d780 / call 0x180133310 / post 0x18018d7b0)
 *   CLoudnessFilter    (factory bypass: in-place copy)
 *   CPMEQ              (factory bypass: in-place copy)
 *   CIVolSt4           (default body 0x18010cf30)
 *   CMultiBand mode 1  (same bracket)
 *
 * CLoudnessFilter and CPMEQ are bypassed in both factory banks, so the
 * audible order is mode 0 -> iVolume -> mode 1.  The LV2 chain cannot
 * express that interleave with separate nodes (each node drains a whole
 * block), so the three stages live in one adapter.
 *
 * Data files (48 kHz keep the deployed names; other rates are rate-tagged and
 * never fall back to another rate's coefficients):
 *   mb-bank.bin  / mb-bank-44100.bin   CMultiBand coefficient bank (0xbef)
 *   mb-state.bin / mb-state-44100.bin  CMultiBand state block      (0xc00)
 *   mb-obj.bin   / mb-obj-44100.bin    CMultiBand object fields    (0x520)
 *   fin-eq.bin   / fin-eq-44100.bin    CFinishingEQ cascades
 *   profile.bin  / profile-44100.bin   CIVolSt4 factory profile    (7032)
 *
 * Both rates' files come from the same source: a preset-apply snapshot of the
 * original APO at that sample rate (dyn-44100-20261028).  The 44.1 kHz profile
 * therefore carries the factory CLevelerSt4 Curve2 design computed by the
 * non-48 kHz branch of the mode setter 0x180143ef0 (biquad remap with
 * k = 0.0695688916721601, not the halved seeds of the 48 kHz branch).
 *
 * Packaged for the XPS 9320 Linux playback chain.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lv2/core/lv2.h>
#include <xmmintrin.h>

#define XPS_MB_DEBUG 0
#include "xps_mb_frame_ref.h"
#include "xps_ivol_runtime.h"
#include "xps_rate.h"

/* Endpoint attenuation already baked into profile.bin: V+0x2cdc is 0 dB, i.e.
 * the captured endpoint sat at its maximum. There is no Linux-sink to Windows
 * endpoint-dB mapping, so the adapter states this value explicitly instead of
 * leaving the maximum-volume assumption implicit. */
#define XPS_IVOL_PROFILE_ATT_DB 0.0f

#ifndef XPS_URI
#define XPS_URI "urn:xps9320:research-dynamics-linux"
#endif

#define OBJ_LEN 0x520
#define BANK_LEN 0xbef
#define STATE_LEN 0xc00
#define FIN_MAX 128

/* nr.bin header, written by scripts/extract_noiserider_state_20261028.py.
 * Optional sidecar: absent means the captured factory state V+0x4f7e8 == 1
 * (NoiseRider bypassed). */
#define NR_MAGIC 0x3158524Eu
#define NR_VERSION 1u
#define NR_BYPASS 1u
#define NR_ACTIVE 2u

/* CFinishingEQ inline biquad cascades (wrapper+0x411300).  The factory
 * sub-chain applies them to the shared bus between CMultiBand mode 0 and
 * CIVolSt4, so they belong to this adapter.  Direct form I, single
 * precision, exactly the factory recurrence:
 *   y = ((c0*x + c1*x1) + c2*x2) - (a1*y1 + a2*y2) */
struct xps_fin_eq {
    uint32_t n1, n2, n3;
    float coeff[FIN_MAX][5];
    float state[FIN_MAX][4];
};

struct instance {
    xps_mb_ref *ref;
    unsigned char bank[BANK_LEN];
    unsigned char state[STATE_LEN];
    unsigned char obj[OBJ_LEN];
    struct xps_fin_eq fin, fin_fresh;
    struct xps_ivol ivol, ivol_fresh;
    struct xps_curve_input curve;
    const float *in[2];
    float *out[2];
    int failed;
    const float *att_control;
    float current_att_db;
};

static float ldf(unsigned char *b, uint64_t a)
{
    float v;
    memcpy(&v, b + a, 4);
    return v;
}

static void stf(unsigned char *b, uint64_t a, float v) { memcpy(b + a, &v, 4); }

static void pre_hook(unsigned char *base)
{
    stf(base, MB_OBJ_OFF + 0x380, ldf(base, MB_IN_OFF));
    stf(base, MB_OBJ_OFF + 0x384, ldf(base, MB_IN_OFF + 4));
    stf(base, MB_OBJ_OFF + 0x388, ldf(base, MB_IN_OFF));
    stf(base, MB_OBJ_OFF + 0x38c, ldf(base, MB_IN_OFF + 4));
}

static void post_hook(unsigned char *base)
{
    int32_t armed;
    memcpy(&armed, base + MB_OBJ_OFF + 0x3c8, 4);
    if (armed != 1) return;
    float c = ldf(base, MB_OBJ_OFF + 0x3c4);
    for (int i = 0; i < 2; i++) {
        float y = ldf(base, MB_IN_OFF + 4 * i);
        float x = ldf(base, MB_OBJ_OFF + 0x388 + 4 * i);
        float dry = (1.0f - c) * x;
        float wet = c * y;
        stf(base, MB_IN_OFF + 4 * i, wet + dry);
    }
    float t = c * ldf(base, MB_OBJ_OFF + 0x3b4);
    if (t > 1.0f) {
        stf(base, MB_OBJ_OFF + 0x3c4, 1.0f);
        memset(base + MB_OBJ_OFF + 0x3c8, 0, 4);
    } else if (1e-5f > t) {
        stf(base, MB_OBJ_OFF + 0x3c4, 1e-5f);
        memset(base + MB_OBJ_OFF + 0x3c8, 0, 4);
    } else {
        stf(base, MB_OBJ_OFF + 0x3c4, t);
    }
}

static int read_blob(const char *dir, const char *name, void *dst, size_t bytes)
{
    char filename[4096];
    int n = snprintf(filename, sizeof filename, "%s%s", dir, name);
    if (n < 0 || (size_t)n >= sizeof filename) return 0;
    FILE *f = fopen(filename, "rb");
    if (!f) return 0;
    int ok = fread(dst, 1, bytes, f) == bytes && fgetc(f) == EOF;
    fclose(f);
    return ok;
}

static int read_profile(const char *path, const char *name, struct xps_ivol *s)
{
    char filename[4096];
    int n = snprintf(filename, sizeof filename, "%s%s", path, name);
    if (n < 0 || (size_t)n >= sizeof filename) return -1;
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;
    int ok = fread(s->p, 1, 864, f) == 864 &&
             fread(s->lp, 1, 128, f) == 128 &&
             fread(s->cp, 1, 128, f) == 128 &&
             fread(s->gain_lut, 1, 4096, f) == 4096 &&
             fread(s->history, 1, 96, f) == 96 &&
             fread(s->ring, 1, 1540, f) == 1540;
    float scalar[6];
    uint32_t flags[3];
    ok = ok && fread(scalar, 1, 24, f) == 24 &&
         fread(flags, 1, 12, f) == 12 &&
         fread(s->leveler.trace, 1, 128, f) == 128 &&
         fread(&s->compressor.envelope, 1, 4, f) == 4 &&
         fread(&s->compressor.gain, 1, 4, f) == 4 &&
         fread(&s->compressor.residual, 1, 8, f) == 8;
    ok = ok && fgetc(f) == EOF;
    fclose(f);
    if (!ok) return -1;
    s->peak_rise = scalar[0]; s->peak_fall = scalar[1];
    s->gate_offset = scalar[2]; s->smooth_attack = scalar[3];
    s->smooth_release = scalar[4]; s->sizing = scalar[5];
    s->leveler_unity = (int)flags[0];
    s->compressor_unity = (int)flags[1];
    s->gate_square = (int)flags[2];
    /* profile.bin carries P330 but not V+0x4f7e8. The captured wrapper has
     * V+0x4f7e8 = 1 (NoiseRider bypassed), which is the only branch state this
     * profile was extracted with; an NR-active profile must not reuse it. */
    s->nr_state = XPS_IVOL_NR_BYPASS;
    s->nr_down = 0.f;
    s->nr_up = 0.f;
    s->nr_ramp = 1.f;
    s->nr_mirror_down = 0.f;
    s->nr_mirror_up = 0.f;
    s->lut_ready = 1;
    s->lut_index_max = 511.f;
    return 0;
}

/* Optional CNoiseRider postgain state (48 kHz only).
 *
 * The V+0x4f7e8 == 0 arm reads PN+0x00/PN+0x04 from the CNoiseRider child bank
 * plus the V+0x2d0c ramp state. profile.bin does not carry them, so a capture
 * of the NR-enabled wrapper is serialized separately as nr.bin. Absent file
 * keeps the factory-captured bypass arm. A 44.1 kHz instance with an nr.bin
 * present is refused: the sidecar is a 48 kHz capture and must not be reused
 * at another rate. */
static int read_nr(const char *path, struct xps_ivol *s, double rate)
{
    char filename[4096];
    int n = snprintf(filename, sizeof filename, "%snr.bin", path);
    if (n < 0 || (size_t)n >= sizeof filename) return -1;
    FILE *f = fopen(filename, "rb");
    if (!f) return 1;
    if (rate != 48000.0) {
        fprintf(stderr, "xps9320: %s: nr.bin is a 48000 Hz CNoiseRider sidecar "
                        "and cannot be used at %.0f Hz\n", XPS_URI, rate);
        fclose(f);
        return -1;
    }
    uint32_t head[4];
    float body[8];
    int ok = fread(head, 1, 16, f) == 16 && fread(body, 1, 32, f) == 32 &&
             fgetc(f) == EOF;
    fclose(f);
    if (!ok || head[0] != NR_MAGIC || head[1] != NR_VERSION) return -1;
    if (head[2] != NR_BYPASS && head[2] != NR_ACTIVE) return -1;
    if (head[3] != 0) return -1;
    for (int i = 0; i < 8; i++) if (!isfinite(body[i])) return -1;
    if (body[0] != 48000.f) return -1;
    if (!(body[1] > 0.f && body[1] < 4.f && body[2] > 0.f && body[2] < 4.f))
        return -1;
    if (!(body[3] > 0.f && body[3] <= 4.f)) return -1;
    s->nr_state = (int)head[2];
    s->nr_down = body[1];
    s->nr_up = body[2];
    s->nr_ramp = body[3];
    s->nr_mirror_down = body[4];
    s->nr_mirror_up = body[5];
    return 0;
}

/* fin-eq.bin: u32 version, n1, n2, n3, then (2*n1 + n2 + n3) entries of
 * 9 floats (c0..c4, x1, x2, y1, y2) in factory processing order. */
static int read_fin_eq(const char *path, const char *name, struct xps_fin_eq *e)
{
    char filename[4096];
    int n = snprintf(filename, sizeof filename, "%s%s", path, name);
    if (n < 0 || (size_t)n >= sizeof filename) return -1;
    FILE *f = fopen(filename, "rb");
    if (!f) return -1;
    uint32_t header[4];
    int ok = fread(header, 1, 16, f) == 16 && header[0] == 1;
    uint32_t total = 2 * header[1] + header[2] + header[3];
    if (!ok || header[1] > FIN_MAX || header[2] > FIN_MAX ||
        header[3] > FIN_MAX || total > FIN_MAX) {
        fclose(f);
        return -1;
    }
    e->n1 = header[1];
    e->n2 = header[2];
    e->n3 = header[3];
    for (uint32_t k = 0; k < total; k++) {
        if (fread(e->coeff[k], 1, 20, f) != 20 ||
            fread(e->state[k], 1, 16, f) != 16) {
            fclose(f);
            return -1;
        }
    }
    ok = fgetc(f) == EOF;
    fclose(f);
    return ok ? 0 : -1;
}

static float fin_biquad(const float c[5], float s[4], float x)
{
    float x1 = s[0], x2 = s[1], y1 = s[2], y2 = s[3];
    float acc = c[0] * x + c[1] * x1;
    acc = acc + c[2] * x2;
    float feedback = c[3] * y1 + c[4] * y2;
    float y = acc - feedback;
    s[1] = x1;
    s[0] = x;
    s[3] = y1;
    s[2] = y;
    return y;
}

/* Cascade 1 processes stereo pairs, 2 processes left only, 3 right only.
 * Order and per-entry op order match the factory body 0x180084d61. */
static void fin_eq_stage(struct xps_fin_eq *e, float *l, float *r)
{
    unsigned k = 0;
    for (uint32_t b = 0; b < e->n1; b++) {
        *l = fin_biquad(e->coeff[k], e->state[k], *l);
        k++;
        *r = fin_biquad(e->coeff[k], e->state[k], *r);
        k++;
    }
    for (uint32_t b = 0; b < e->n2; b++, k++)
        *l = fin_biquad(e->coeff[k], e->state[k], *l);
    for (uint32_t b = 0; b < e->n3; b++, k++)
        *r = fin_biquad(e->coeff[k], e->state[k], *r);
}

static LV2_Handle instantiate(const LV2_Descriptor *d, double rate,
                              const char *path, const LV2_Feature *const *features)
{
    (void)d;
    (void)features;
    enum xps_rate r = xps_rate_select(XPS_URI, rate, XPS_RATES_48000_ONLY,
                                      "mb-bank, mb-state, mb-obj, fin-eq and "
                                      "profile were captured from the factory "
                                      "preset-apply snapshot at 44100 and "
                                      "48000 Hz only");
    if (r == XPS_RATE_UNSUPPORTED) return NULL;
    struct instance *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    s->ref = malloc(sizeof *s->ref);
    if (!s->ref) { free(s); return NULL; }
    char bank_name[64], state_name[64], obj_name[64], prof_name[64], fin_name[64];
    if (xps_rate_blob_name(bank_name, sizeof bank_name, "mb-bank", r) < 0 ||
        xps_rate_blob_name(state_name, sizeof state_name, "mb-state", r) < 0 ||
        xps_rate_blob_name(obj_name, sizeof obj_name, "mb-obj", r) < 0 ||
        xps_rate_blob_name(prof_name, sizeof prof_name, "profile", r) < 0 ||
        xps_rate_blob_name(fin_name, sizeof fin_name, "fin-eq", r) < 0) {
        free(s->ref);
        free(s);
        return NULL;
    }
    if (!read_blob(path, bank_name, s->bank, BANK_LEN) ||
        !read_blob(path, state_name, s->state, STATE_LEN) ||
        !read_blob(path, obj_name, s->obj, OBJ_LEN) ||
        read_profile(path, prof_name, &s->ivol_fresh) ||
        read_nr(path, &s->ivol_fresh, rate) < 0 ||
        read_fin_eq(path, fin_name, &s->fin_fresh)) {
        /* No cross-rate fallback: a 44.1 kHz instance with only the 48 kHz
         * files present must fail here, naming the file it wanted. */
        fprintf(stderr, "xps9320: %s: missing or malformed %.0f Hz data "
                        "(%s, %s, %s, %s, %s); refusing to fall back to "
                        "another rate's coefficients\n", XPS_URI, rate, bank_name,
                state_name, obj_name, prof_name, fin_name);
        free(s->ref);
        free(s);
        return NULL;
    }
    /* The serialized parent bank must be exactly what the captured curve inputs
     * produce at the baked attenuation. Refuse a profile that cannot be
     * reproduced instead of silently mixing a different first bank with these
     * curve inputs or applying an unstated attenuation. */
    struct xps_ivol probe = s->ivol_fresh;
    s->curve = xps_ivol_captured_curve_input;
    if (xps_ivol_apply_attenuation(&probe, &s->curve, XPS_IVOL_PROFILE_ATT_DB) ||
        memcmp(probe.p, s->ivol_fresh.p, sizeof probe.p)) {
        free(s->ref);
        free(s);
        return NULL;
    }
    return s;
}

static void connect_port(LV2_Handle h, uint32_t port, void *data)
{
    struct instance *s = h;
    if (port < 2) s->in[port] = data;
    else if (port < 4) s->out[port - 2] = data;
    else if (port == 4) s->att_control = data;
}

static void activate(LV2_Handle h)
{
    struct instance *s = h;
    xps_mb_ref_init(s->ref, s->obj, OBJ_LEN, s->state, s->bank);
    s->ivol = s->ivol_fresh;
    s->curve = xps_ivol_captured_curve_input;
    s->fin = s->fin_fresh;
    s->current_att_db = 0.f;
    s->failed = xps_ivol_apply_attenuation(&s->ivol, &s->curve,
                                           XPS_IVOL_PROFILE_ATT_DB) != 0;
}

static void run(LV2_Handle h, uint32_t count)
{
    struct instance *s = h;
    if (!count) return;
    /* Explicit dB only; no Linux slider/Windows endpoint mapping is assumed.
     * Invalid/unvalidated requests leave the last valid curve in place.
     * Curve publication happens once at this run boundary, without state reset. */
    float target = s->att_control ? *s->att_control : 0.f;
    if (isfinite(target) && target <= 0.f && target >= -40.f &&
        target != s->current_att_db) {
        unsigned csr = _mm_getcsr();
        _mm_setcsr(0x1f80u);
        int err = xps_ivol_apply_attenuation(&s->ivol, &s->curve, target);
        _mm_setcsr(csr);
        if (!err) s->current_att_db = target;
    }
    unsigned char *base = s->ref->mem;
    unsigned saved = _mm_getcsr();
    /* The factory block processor 0x1800825e0 runs the whole chain with
     * FTZ|DAZ; the iVolume body there sees the same mode. */
    _mm_setcsr(0x9fc0u);
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
        stf(base, MB_IN_OFF, l);
        stf(base, MB_IN_OFF + 4, r);
        pre_hook(base);
        /* The factory passes the previous frame's iVolume total gain
         * (V+0x2d08) as the mode-0 volume argument; mode 1 uses 1.0. */
        xps_mb_frame_ref(s->ref, MB_OBJ_OFF, MB_IN_OFF, MB_IN_OFF, 0,
                         s->ivol.gain_metadata[5]);
        post_hook(base);
        /* CFinishingEQ cascades, then the CXTC/PMEQ stages (factory
         * bypass) leave the bus untouched. */
        {
            float lf = ldf(base, MB_IN_OFF), rf = ldf(base, MB_IN_OFF + 4);
            fin_eq_stage(&s->fin, &lf, &rf);
            stf(base, MB_IN_OFF, lf);
            stf(base, MB_IN_OFF + 4, rf);
        }
        /* iVolume on the same bus (Loudness and PMEQ are bypass copies).
         * The native powf port validates its environment and refuses FTZ|DAZ,
         * so iVolume runs with the default MXCSR.  The original body's PCM is
         * identical under 0x1f80 and 0x9fc0 on the validation fixture
         * (only denormal state bytes differ), so this preserves factory PCM. */
        float ivin[2] = {ldf(base, MB_IN_OFF), ldf(base, MB_IN_OFF + 4)};
        float ivout[2] = {0.f, 0.f};
        _mm_setcsr(0x1f80u);
        int iverr = xps_ivol_step(&s->ivol, ivin, ivout);
        _mm_setcsr(0x9fc0u);
        if (iverr) {
            s->failed = 1;
            s->out[0][n] = 0.f;
            s->out[1][n] = 0.f;
            continue;
        }
        stf(base, MB_IN_OFF, ivout[0]);
        stf(base, MB_IN_OFF + 4, ivout[1]);
        pre_hook(base);
        xps_mb_frame_ref(s->ref, MB_OBJ_OFF, MB_IN_OFF, MB_IN_OFF, 1, 1.0f);
        post_hook(base);
        s->out[0][n] = ldf(base, MB_IN_OFF);
        s->out[1][n] = ldf(base, MB_IN_OFF + 4);
    }
    _mm_setcsr(saved);
}

static void cleanup(LV2_Handle h)
{
    struct instance *s = h;
    free(s->ref);
    free(s);
}

static const LV2_Descriptor descriptor = {
    XPS_URI, instantiate, connect_port, activate, run, NULL, cleanup, NULL
};

LV2_SYMBOL_EXPORT const LV2_Descriptor *lv2_descriptor(uint32_t index)
{
    return index ? NULL : &descriptor;
}
