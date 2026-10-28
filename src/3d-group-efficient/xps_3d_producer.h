/* Native port of the live CMaxx3DSt bus producer at 0x18008ca30.
 *
 * The producer runs once per channel per frame.  It emits four floats per
 * channel into the aggregate's eight-float bus; CMaxx3DSt 0x180138740 then
 * consumes that bus.  Coefficients come from the stage object's bank (index 1
 * in the live MFX preset).  All arithmetic follows the original instruction
 * order; lattice sections use binary64 intermediates rounded back to float32.
 */
#ifndef XPS_3D_PRODUCER_H
#define XPS_3D_PRODUCER_H

#include <math.h>
#include <stdint.h>
#include <string.h>

#define XPS_3D_MAX_CH 8

typedef struct {
    /* [channel][0..2] main splitter; order is object 0x3e8, 0x3f0, 0x3ec */
    float main_s[XPS_3D_MAX_CH][3];
    /* [channel][0..2] object 0x400, 0x404, 0x408 */
    float q_s[XPS_3D_MAX_CH][3];
    /* [channel][0..2] object 0x418, 0x41c, 0x420 */
    float r_s[XPS_3D_MAX_CH][3];
    float s2_s[XPS_3D_MAX_CH];      /* object 0x430 + 8*ch */
    float t_s[XPS_3D_MAX_CH][2];    /* object 0x440 / 0x444 + 8*ch */
} xps_3d_producer_state;

typedef struct {
    float main_c[3];   /* bank +0x00, +0x04, +0x08 */
    float q_c[3];      /* bank +0x0c, +0x10, +0x14 */
    float r_c[3];      /* bank +0x18, +0x1c, +0x20 */
    float t_c[2];      /* bank +0x2c, +0x30 */
    float gain[4];     /* bank +0x34, +0x38, +0x3c, +0x40 */
    int32_t channels;  /* object +0x46c */
} xps_3d_producer_cfg;

static inline void xps_3d_producer_load_cfg(const unsigned char *bank,
                                            int32_t channels,
                                            xps_3d_producer_cfg *c) {
    float f[0x48 / 4];
    memcpy(f, bank, sizeof f);
    c->main_c[0] = f[0x00 / 4];
    c->main_c[1] = f[0x04 / 4];
    c->main_c[2] = f[0x08 / 4];
    c->q_c[0] = f[0x0c / 4];
    c->q_c[1] = f[0x10 / 4];
    c->q_c[2] = f[0x14 / 4];
    c->r_c[0] = f[0x18 / 4];
    c->r_c[1] = f[0x1c / 4];
    c->r_c[2] = f[0x20 / 4];
    c->t_c[0] = f[0x2c / 4];
    c->t_c[1] = f[0x30 / 4];
    c->gain[0] = f[0x34 / 4];
    c->gain[1] = f[0x38 / 4];
    c->gain[2] = f[0x3c / 4];
    c->gain[3] = f[0x40 / 4];
    c->channels = channels;
}

static inline void xps_3d_producer_process(xps_3d_producer_state *s,
                                           const xps_3d_producer_cfg *c,
                                           const float *in, float bus[8]) {
    for (int ch = 0; ch < c->channels && ch < XPS_3D_MAX_CH; ch++) {
        float *m = s->main_s[ch];
        float *q = s->q_s[ch];
        float *r = s->r_s[ch];
        float *t = s->t_s[ch];
        const double x = (double)in[ch];

        /* Main lattice: object 0x3e8/0x3f0/0x3ec. */
        const double mc0 = (double)c->main_c[0];
        const double mc1 = (double)c->main_c[1];
        const double mc2 = (double)c->main_c[2];
        const double m0 = (double)m[0], mB = (double)m[1], mC = (double)m[2];
        const double y = x - mc0 * m0;
        m[0] = (float)y;
        const double z = y * mc0 + m0;
        const double x2 = x - mc1 * mC - mc2 * mB;
        m[1] = (float)mC;
        m[2] = (float)x2;
        const double w = x2 * mc2 + mc1 * mC + mB;
        float out4 = ((float)w + (float)z) * 0.5f;
        float outc = ((float)w - (float)z) * 0.5f;

        /* One-state section on out4: object 0x430 + 8*ch. */
        double sc = (double)c->r_c[0];
        double sv = (double)s->s2_s[ch];
        double sy = (double)out4 - sc * sv;
        s->s2_s[ch] = (float)sy;
        double svout = sy * sc + sv;
        out4 = (float)svout;

        /* Two-state section on outc: object 0x440 / 0x444 + 8*ch. */
        const double tc0 = (double)c->t_c[0];
        const double tc1 = (double)c->t_c[1];
        const double t0 = (double)t[0], t1 = (double)t[1];
        double ty = (double)outc - tc0 * t0 - tc1 * t1;
        t[1] = (float)t0;
        t[0] = (float)ty;
        double tout = ty * tc1 + tc0 * t0 + t1;
        outc = (float)tout;

        /* Three-state lattice on out4: object 0x400/0x404/0x408. */
        const double qc0 = (double)c->q_c[0];
        const double qc1 = (double)c->q_c[1];
        const double qc2 = (double)c->q_c[2];
        const double q0 = (double)q[0], q1 = (double)q[1], q2 = (double)q[2];
        double qx = (double)out4;
        double qy = qx - qc0 * q0;
        q[0] = (float)qy;
        double qz = qy * qc0 + q0;
        double qx2 = qx - qc1 * q1 - qc2 * q2;
        q[2] = (float)q1;
        q[1] = (float)qx2;
        double qw = qx2 * qc2 + qc1 * q1 + q2;
        float out0 = ((float)qw + (float)qz) * 0.5f;
        out4 = ((float)qw - (float)qz) * 0.5f;

        /* Three-state lattice on outc: object 0x418/0x41c/0x420. */
        const double rc0 = (double)c->r_c[0];
        const double rc1 = (double)c->r_c[1];
        const double rc2 = (double)c->r_c[2];
        const double r0 = (double)r[0], r1 = (double)r[1], r2 = (double)r[2];
        double rx = (double)outc;
        double ry = rx - rc0 * r0;
        r[0] = (float)ry;
        double rz = ry * rc0 + r0;
        double rx2 = rx - rc1 * r1 - rc2 * r2;
        r[2] = (float)r1;
        r[1] = (float)rx2;
        double rw = rx2 * rc2 + rc1 * r1 + r2;
        float out8 = ((float)rw + (float)rz) * 0.5f;
        outc = -(((float)rw - (float)rz) * 0.5f);

        bus[4 * ch + 0] = out0 * c->gain[0];
        bus[4 * ch + 1] = out4 * c->gain[1];
        bus[4 * ch + 2] = out8 * c->gain[2];
        bus[4 * ch + 3] = outc * c->gain[3];
    }
}

#endif
