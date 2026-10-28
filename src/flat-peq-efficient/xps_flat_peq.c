/* CPeq 0x1800e3d60: preserve the original binary32 operation order even
 * when numerator == denominator. See provenance.json for captured banks.
 * One preset, 48 kHz; the identity transfer function still rounds internally. */
#include <lv2/core/lv2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>
#define URI "urn:xps9320:research-flat-peq-efficient"
typedef struct {float c[10][2][5],init[2][11][2],hist[2][11][2];const float *in[2];float *out[2];int failed;} Inst;
static LV2_Handle instantiate(const LV2_Descriptor*d,double rate,const char*dir,const LV2_Feature*const*f){
 (void)d;(void)f;if(rate!=48000)return NULL;Inst*s=calloc(1,sizeof(*s));if(!s)return NULL;
 char p[4096];int n=snprintf(p,sizeof p,"%speq.bin",dir);if(n<0||(size_t)n>=sizeof p){free(s);return NULL;}
 FILE*fp=fopen(p,"rb");if(!fp){free(s);return NULL;}
 int ok=fread(s->c,1,400,fp)==400&&fread(s->init,1,176,fp)==176&&fgetc(fp)==EOF;fclose(fp);
 if(!ok){free(s);return NULL;}for(int i=0;i<10;i++)for(int ch=0;ch<2;ch++) {
  float*c=s->c[i][ch];if(c[0]!=1||c[1]!=c[3]||c[2]!=c[4]){free(s);return NULL;}
 }
 for(int ch=0;ch<2;ch++)for(int j=0;j<11;j++)for(int k=0;k<2;k++)if(!isfinite(s->init[ch][j][k])){free(s);return NULL;}
 return s;
}
static void connect(LV2_Handle h,uint32_t p,void*d){Inst*s=h;if(p<2)s->in[p]=d;else if(p<4)s->out[p-2]=d;}
static void activate(LV2_Handle h){Inst*s=h;memcpy(s->hist,s->init,sizeof s->hist);s->failed=0;}
static void run(LV2_Handle h,uint32_t n){Inst*s=h;unsigned saved=_mm_getcsr();_mm_setcsr(0x9fc0);
 for(uint32_t k=0;k<n;k++){
  float input[2]={s->in[0][k],s->in[1][k]};
  if(!isfinite(input[0])||!isfinite(input[1]))s->failed=1;
  for(int ch=0;ch<2;ch++){
   if(s->failed){s->out[ch][k]=0;continue;}
   float x=input[ch];
   /* Histories at level j are the input to band j; level 10 is the
    * final output. Update each level only after its old values are used. */
   for(int j=0;j<10;j++){
    const float*c=s->c[j][ch];float*h0=s->hist[ch][j],*h1=s->hist[ch][j+1];
    float y=((c[0]*x)+(c[1]*h0[0]))+(c[2]*h0[1]);
    y=y-((c[3]*h1[0])+(c[4]*h1[1]));
    h0[1]=h0[0];h0[0]=x;x=y;
   }
   float*last=s->hist[ch][10];last[1]=last[0];last[0]=x;
   if(!isfinite(x)){s->failed=1;x=0;}
   s->out[ch][k]=x;
  }
  if(s->failed)s->out[0][k]=s->out[1][k]=0;
 }
 _mm_setcsr(saved);
}
static void cleanup(LV2_Handle h){free(h);}
static const LV2_Descriptor desc={URI,instantiate,connect,activate,run,NULL,cleanup,NULL};
LV2_SYMBOL_EXPORT const LV2_Descriptor*lv2_descriptor(uint32_t index){return index==0?&desc:NULL;}
