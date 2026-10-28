/* Runtime (post-snapshot) inputs for the native CIVolSt4 model.
 *
 * profile.bin is the result of running the actual CIVolSt4 curve (0x18010be30)
 * once at the captured endpoint attenuation V+0x2cdc = 0 dB. The curve inputs
 * themselves live in the wrapper, not in the serialized profile, so a runtime
 * attenuation change needs them restated. xps_ivol_captured_curve_input holds
 * the values read from preset-apply-20261027/mfx_apply.bin.heap at
 * V = wrapper + 0x267f8; applying it at att = 0 reproduces every curve-owned
 * word of profile.bin bit for bit (see ivol-postgain-att-20261028-a).
 *
 * The Windows endpoint value is binary32(GetMasterVolumeLevel() -
 * GetVolumeRange.maximum) dB, so it is <= 0 and mode 1 stores it in V+0x2cdc.
 * The endpoint range/volume were never captured: there is no mapping from a
 * Linux sink volume or LV2 control to this field, and the adapter therefore
 * takes an explicit dB value instead of assuming the endpoint maximum.
 */
#ifndef XPS_IVOL_RUNTIME_H
#define XPS_IVOL_RUNTIME_H

#include "xps_ivol_native.h"
#include "xps_ivol_curve.h"

/* V+0x2ca8 low, +0x2cac ac, +0x2cb0 gain, +0x2ccc ceiling, +0x2cd8 hardknee,
 * +0x2cdc att, +0x2ce0 e0, +0x30c0 flag30, K+0x3a8 flag_k, V+0x4f7e8
 * nr_bypass, V+0x4f958 nr_mode, V+0x391c/+0x3924/+0x392c route trims,
 * K+0x434/+0x444/+0x43c compressor values, bank +0x308..+0x324. */
static const struct xps_curve_input xps_ivol_captured_curve_input = {
    .low = 3.0f,
    .ac = -80.0f,
    .gain = 0.0f,
    .ceiling = 0.0f,
    .att = 0.0f,
    .e0 = 0.0f,
    .hardknee = 1,
    .flag30 = 0,
    .flag_k = 0,
    .nr_bypass = 1,
    .nr_mode = 0,
    .v391c = 1.0f,
    .v3924 = 0.0f,
    .v392c = 0.0f,
    .k434 = 1.0f,
    .k444 = 0.0f,
    .k43c = 0.37583741545677185f,
    .b308 = {1.0f, 1.0f},
    .b320 = {1.0f, 1.0f},
};

/* Re-run the curve for one endpoint attenuation and rewrite the parent words it
 * owns in s->p. `in` is updated so repeated calls start from the last value.
 * Rejects non-finite values and positive attenuation (the endpoint cannot be
 * above its own maximum); the curve itself handles the low-40 dB guard, so no
 * extra clamp is applied here. Returns 0 on success, -1 otherwise. */
static inline int xps_ivol_apply_attenuation(struct xps_ivol *s,
                                             struct xps_curve_input *in,
                                             float att_db) {
    if (!in || !isfinite(att_db) || att_db > 0.f) return -1;
    struct xps_curve_input next = *in;
    next.att = att_db;
    float block[0x340];
    memcpy(block, s->p, sizeof block);
    if (xps_ivol_curve(&next, block)) return -1;
    memcpy(s->p, block, sizeof block);
    in->att = att_db;
    return 0;
}

#endif
