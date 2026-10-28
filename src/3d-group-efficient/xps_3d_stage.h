/* Full live CMaxx3DSt stage: bus producer 0x18008ca30 followed by the
 * selector-0 frame processor 0x180138740.  The producer writes four floats per
 * channel into the eight-float bus; the frame processor consumes the bus and
 * adds its result to the output accumulator. */
#ifndef XPS_3D_STAGE_H
#define XPS_3D_STAGE_H

#include "xps_3d_kernel.h"
#include "xps_3d_producer.h"

typedef struct {
    xps_3d_producer_state producer;
    xps_3d_state dsp;
} xps_3d_stage_state;

typedef struct {
    xps_3d_producer_cfg producer;
    xps_3d_cfg dsp;
} xps_3d_stage_cfg;

static inline void xps_3d_stage_process(xps_3d_stage_state *s,
                                         const xps_3d_stage_cfg *c,
                                         float in_l, float in_r,
                                         float *out_l, float *out_r) {
    float in[2] = {in_l, in_r};
    float bus[8] = {0.0f};
    xps_3d_producer_process(&s->producer, &c->producer, in, bus);
    float out[2] = {0.0f, 0.0f};
    xps_3d_process(&s->dsp, &c->dsp, bus, out);
    *out_l = out[0];
    *out_r = out[1];
}

#endif
