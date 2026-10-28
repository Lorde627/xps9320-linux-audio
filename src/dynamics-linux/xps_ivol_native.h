/* Native 48k CIVolSt4 normal path. Build with -ffp-contract=off, no fast-math.
 * Explicit parameters/state; no DLL, Windows ABI, file IO, allocation or locks.
 * Current scope: Chat bypass, normal detector, stable bank, fixed helper coeff,
 * postgain selected by V+0x4f7e8: the bypass arm (P330 direct/ramped) and the
 * NoiseRider-active arm (CNoiseRider child bank + V+0x2d0c ramp). An unstated
 * NR state is still refused. Factory initialization is supplied separately. */
#ifndef XPS_IVOL_NATIVE_H
#define XPS_IVOL_NATIVE_H
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "native_powf_sse2.h"

/* V+0x4f7e8[owner bank] selects the postgain branch pair. Zero is "not
 * stated": the step refuses instead of assuming the factory bypass branch. */
#define XPS_IVOL_NR_UNKNOWN 0
#define XPS_IVOL_NR_BYPASS 1
#define XPS_IVOL_NR_ACTIVE 2

struct xps_leveler_state {
    float trace[32];
};
struct xps_comp_state {float envelope,gain;double residual;};
struct xps_ivol {
    float p[216],lp[32],cp[32];
    float gain_lut[2][512];
    float lut_index_max;
    int lut_ready;
    uint32_t ring[385];
    unsigned char history[96];
    float gain_metadata[6];
    struct xps_leveler_state leveler;
    struct xps_comp_state compressor;
    int leveler_unity,compressor_unity,gate_square;
    int nr_state;               /* XPS_IVOL_NR_* for V+0x4f7e8[owner bank] */
    /* CNoiseRider child bank PN = *(V+0x4f450+8*read) = *(N+0x10+8*N.read).
     * The V+0x4f7e8==0 arm reads only PN+0x00 (unboost) and PN+0x04
     * (transition); the same two words are mirrored to V+0x4f024/V+0x4f020. */
    float nr_down,nr_up;        /* PN+0x00, PN+0x04 */
    float nr_ramp;              /* V+0x2d0c persistent ramp state */
    float nr_mirror_down;       /* V+0x4f024 (copy of PN+0x00) */
    float nr_mirror_up;         /* V+0x4f020 (copy of PN+0x04) */
    float peak_rise,peak_fall,gate_offset,smooth_attack,smooth_release,sizing;
};
static inline float xps_get(const unsigned char *p,unsigned o) {float x;memcpy(&x,p+o,4);return x;}
static inline void xps_put(unsigned char *p,unsigned o,float x) {memcpy(p+o,&x,4);}
static inline uint32_t xps_iget(const unsigned char *p,unsigned o) {uint32_t x;memcpy(&x,p+o,4);return x;}
static inline void xps_iput(unsigned char *p,unsigned o,uint32_t x) {memcpy(p+o,&x,4);}
static inline int32_t xps_signed(uint32_t x) {int32_t r;memcpy(&r,&x,4);return r;}
static inline float xps_rget(const struct xps_ivol *s,unsigned i) {float f;memcpy(&f,s->ring+i,4);return f;}
static inline void xps_rput(struct xps_ivol *s,unsigned i,float f) {memcpy(s->ring+i,&f,4);}
static inline unsigned xps_advance(unsigned i,unsigned k) {unsigned j=i+k;return j>=385?j-385:j;}
static inline float xps_leveler_step(struct xps_ivol *s,float x) {
    float *p=s->lp,*h=s->leveler.trace;
#define LG(o) h[((o)-0xbf0)/4]
    float u=p[1]*x;u=u+p[2]*LG(0xc3c);u=u+LG(0xc38)*p[0];
    u=u-p[3]*LG(0xc40);u=u-p[4]*LG(0xc44);u=u+u;
    float v=p[6]*u;v=v+p[7]*LG(0xc44);v=v+LG(0xc40)*p[5];
    v=v-LG(0xc4c)*p[8];v=v-p[9]*LG(0xc50);v=v+v;
    if(!isfinite(u)||!isfinite(v))return NAN;
    LG(0xc38)=LG(0xc3c);LG(0xc3c)=x;LG(0xc40)=LG(0xc44);LG(0xc44)=u;
    LG(0xc4c)=LG(0xc50);LG(0xc50)=v;
    float d=fabsf(v),old=LG(0xbf0);
    float env=d+(old-d)*p[(d>old?0x60:0x64)/4];
    if(!isfinite(env))return NAN;
    LG(0xbf0)=env;
    float normalized=env/p[0x50/4];
    if(!isfinite(normalized))return NAN;
    float n=fmaxf(normalized,0.f),g=LG(0xbfc),q=n*g;
    float num=(p[0x78/4]*q)*q+p[0x74/4]*q;num=num+p[0x70/4];
    float den=q*q+p[0x7c/4]*q;
    if(!isfinite(num)||!isfinite(den))return NAN;
    float ratio=(float)((double)num/((double)den+1.0));
    g=ratio*g;g=g*(p[0x58/4]/LG(0xc18));
    if(!isfinite(g))return NAN;
    LG(0xc18)=p[0x58/4];
    g=fmaxf(p[0x54/4],fminf(1.f,g));LG(0xbfc)=g;
#undef LG
    return s->leveler_unity?1.f:g;
}
static inline int xps_compressor_step(struct xps_ivol *s,float x,float *out) {
    if(s->compressor_unity){*out=1.f;return 0;}
    struct xps_comp_state *h=&s->compressor;float *p=s->cp;
    float c=p[x>h->envelope?7:8];h->envelope=x+c*(h->envelope-x);
    float shifted=h->envelope+p[4];
    if(!isfinite(h->envelope)||!isfinite(shifted))return -1;
    float base=fmaxf(fmaxf(-1.f,fminf(1.f,shifted)),p[3]),power;
    if(xps_powf_positive(base,-p[2],&power))return -1;
    float target=p[6]*power;double d=(double)target-(double)h->gain;
    double z=d*(double)p[d>0?1:0];z=z+(double)h->gain;z=z+h->residual;
    h->gain=(float)z;h->residual=(double)(float)(z-(double)h->gain);
    if(!isfinite(h->gain)||!isfinite(h->residual))return -1;
    *out=h->gain;return 0;
}
/* Caller supplies the reviewed 1ms coefficient; this does not reset children,
 * parameter banks or meters outside the original reset's written fields. */
static inline void xps_ivol_reset_parent(struct xps_ivol *s,float coefficient) {
    unsigned char *st=s->history;
    memset(st,0,20);xps_put(st,12,1.f);xps_put(st,20,coefficient);
    xps_put(st,24,1.f);memset(st+28,0,24);
    xps_iput(st,36,1075838976u);xps_iput(st,44,1075838976u);
    xps_put(st,52,1.f);memset(st+56,0,32);xps_put(st,88,1.f);
    for(unsigned i=0;i<385;i++)s->ring[i]=i<64?0x3f800000u:(i<129?(0x7fffffffu>>11):0);
}
static inline int xps_ivol_gain_step(struct xps_ivol *s,float g,float *result) {
    unsigned char *st=s->history;float *p=s->p;
    float old=xps_get(st,12);
    float smooth=g+(old>g?s->smooth_attack:s->smooth_release)*(old-g);
    if(!isfinite(smooth))return -1;
    xps_put(st,12,smooth);
    float value=fminf(g,smooth),prev=xps_get(st,24);
    if(!(prev>value))value=value+xps_get(st,20)*(prev-value);
    if(!isfinite(value))return -1;
    xps_put(st,24,value);
    if(xps_iget((const unsigned char *)p,0x328))xps_put(st,20,p[0x32c/4]);
    else {
        if(!s->lut_ready||!isfinite(s->lut_index_max)||s->lut_index_max>511.f)return -1;
        float first=smooth*p[1]+.5f;
        float second=((smooth+smooth)-value)*p[1]+.5f;
        if(!isfinite(first)||!isfinite(second))return -1;
        first=fminf(first,s->lut_index_max);
        second=fmaxf(fminf(second,s->lut_index_max),0.f);
        /* Original has no lower clamp on first. Reject unsafe addressing;
         * negative coordinates above -1 still truncate to the valid bin0. */
        if(first<=-1.f||second<0.f)return -1;
        unsigned i=(unsigned)(int32_t)first,j=(unsigned)(int32_t)second;
        if(i>=512||j>=512)return -1;
        float coefficient=s->gain_lut[0][i]*s->gain_lut[1][j];
        if(!isfinite(coefficient))return -1;
        xps_put(st,20,coefficient);
    }
    *result=value;return 0;
}
/* Postgain section of the default body 0x18010cf30 (10d2b7..10d3a5).
 *
 * V+0x4f7e8[owner bank] selects the branch pair:
 *   bypass (nonzero), P330==0   total = m * P24
 *   bypass (nonzero), P330!=0   total = m * ramp(P334/P338, history[88])
 *   active (zero)               total = m * ramp(CNoiseRider bank, V+0x2d0c)
 *
 * Active arm, exact original order (0x18010d334..0x18010d3a1):
 *   xmm1 = q = V+0x2d0c        xmm3 = P24
 *   xmm4 = PN+0x04 (up)        xmm2 = PN+0x00 (down)
 *   V+0x4f020 = PN+0x04        V+0x4f024 = PN+0x00   (before the compare)
 *   COMISS xmm3,xmm1; JBE -> q' = max(down*q, target)
 *                      else q' = min(up*q, target)
 *   q' = MAXSS q', 1e-5f       V+0x2d0c = q'          total = m*q'
 * The two child-bank words are not carried by profile.bin; they come from the
 * captured CNoiseRider bank (nr.bin, scripts/extract_noiserider_state_20261028.py)
 * whose coefficients are generated from public2998/public2988 with the wrapper
 * sample rate. An unstated (XPS_IVOL_NR_UNKNOWN) state is refused. */
static inline int xps_ivol_postgain(struct xps_ivol *s,float m,float *total) {
    unsigned char *st=s->history;float *p=s->p;
    if(s->nr_state==XPS_IVOL_NR_BYPASS) {
        float gain=p[0x24/4];
        if(((const unsigned char *)p)[0x330]) {
            float prev=xps_get(st,88);
            gain=p[0x24/4]>prev?fminf(prev*p[0x338/4],p[0x24/4])
                               :fmaxf(prev*p[0x334/4],p[0x24/4]);
            gain=fmaxf(gain,1e-5f);
            xps_put(st,88,gain);
        }
        *total=m*gain;
        return isfinite(*total)?0:-1;
    }
    if(s->nr_state==XPS_IVOL_NR_ACTIVE) {
        float target=p[0x24/4],prev=s->nr_ramp;
        s->nr_mirror_up=s->nr_up;
        s->nr_mirror_down=s->nr_down;
        float gain=target>prev?fminf(s->nr_up*prev,target)
                              :fmaxf(s->nr_down*prev,target);
        gain=fmaxf(gain,1e-5f);
        s->nr_ramp=gain;
        *total=m*gain;
        return isfinite(*total)?0:-1;
    }
    return -1;
}
static inline int xps_ivol_step(struct xps_ivol *s,const float in[2],float out[2]) {
    if(!isfinite(in[0])||!isfinite(in[1]))return -1;
    float left=in[0],right=in[1],peak=fmaxf(fabsf(left),fabsf(right));
    float mean=(left+right)*.5f;
    if(!isfinite(mean))return -1;
    float lg=xps_leveler_step(s,mean),cg;
    if(!isfinite(lg)||xps_compressor_step(s,peak,&cg))return -1;
    unsigned char *st=s->history;float *p=s->p;
    float old=xps_get(st,4),env=peak+(old-peak)*(peak>old?s->peak_rise:s->peak_fall);xps_put(st,4,env);
    float gated=env*p[0];
    if(!isfinite(env)||!isfinite(gated))return -1;
    float limited=fminf(1.f,gated);
    if(s->gate_square)limited=limited*limited;
    float gate=p[0]<0.f?1.f:fminf(1.f,s->gate_offset+limited);
    float a=peak+p[2],b=peak+p[3];
    if(!isfinite(a)||!isfinite(b)||!isfinite(gate))return -1;
    float g=(p[4]/fmaxf(p[4],a))*p[7]+(p[3]/fmaxf(p[3],b))*p[6];
    if(!isfinite(g))return -1;
    unsigned cursor=xps_iget(st,0);
    for(unsigned k=1;k<64;k*=2) {xps_rput(s,cursor,g);cursor=xps_advance(cursor,k);g=fminf(g,xps_rget(s,cursor));}
    float value;
    if(xps_ivol_gain_step(s,g,&value))return -1;
    float fixed_input=value*gate;
    double quant=(double)fixed_input*2147483647.0;
    if(!isfinite(quant))return -1;
    /* Match CVTTSD2SI, including its defined x86 integer-indefinite result.
     * A finite helper gain just above unity can reach this in source profiles.
     * Never use an out-of-range C float-to-int cast. MXCSR is masked by caller. */
    int32_t q=_mm_cvttsd_si32(_mm_set_sd(quant));
    /* Defined arithmetic shift for two's-complement words. */
    uint32_t uq=(uint32_t)q;uq=(uq>>11)|(q<0?0xffe00000u:0);q=xps_signed(uq);
    s->ring[cursor]=(uint32_t)q;cursor=xps_advance(cursor,27);uq=(uint32_t)q-s->ring[cursor];
    cursor=xps_advance(cursor,11);uq=uq-s->ring[cursor];cursor=xps_advance(cursor,27);uq=uq+s->ring[cursor];
    uq=uq-xps_iget(st,36);uint32_t prevq=xps_iget(st,44);xps_iput(st,36,prevq);uq=uq+2u*prevq;xps_iput(st,44,uq);
    float fixed;
    const uint64_t scale_bits=UINT64_C(0x3dfffffffffffd09);double scale;memcpy(&scale,&scale_bits,8);
    fixed=(float)((double)xps_signed(uq)*scale);
    float fixed_gain=fixed*s->sizing;xps_put(st,52,fminf(fixed,xps_get(st,52)));
    float lo=fminf(cg,lg),hi=fmaxf(cg,lg),combined=fminf(lo,fixed_gain);
    s->gain_metadata[0]=combined;s->gain_metadata[1]=fixed_gain/fmaxf(lo,fixed_gain);
    s->gain_metadata[2]=cg/hi;s->gain_metadata[3]=lg;s->gain_metadata[4]=gate;
    xps_rput(s,cursor,left);cursor=xps_advance(cursor,64);float dl=xps_rget(s,cursor);
    float total;
    if(xps_ivol_postgain(s,combined,&total))return -1;
    s->gain_metadata[5]=total;
    xps_rput(s,cursor,right);cursor=xps_advance(cursor,64);float dr=xps_rget(s,cursor);
    out[0]=fmaxf(-1.f,fminf(1.f,dl*total));out[1]=fmaxf(-1.f,fminf(1.f,dr*total));
    cursor=xps_advance(cursor,64);cursor=xps_advance(cursor,64);xps_iput(st,0,cursor);
    xps_put(st,68,fmaxf(fabsf(out[0]),xps_get(st,68)));xps_put(st,84,fmaxf(fabsf(out[1]),xps_get(st,84)));
    return isfinite(out[0])&&isfinite(out[1])?0:-1;
}
#endif
