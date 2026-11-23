#include "check.h"
#include "hydra_dsp.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

static hydra_dsp_handle* make(float sr, int mode, float wet=100.f) {
  hydra_dsp_handle* h=nullptr;
  CHECK(hydra_dsp_create_v1_4(sr,2000,4000,&h)==0);
  CHECK(hydra_dsp_prepare(h,128,HYDRA_DSP_CHANNELS_STEREO)==0);
  const std::pair<hydra_dsp_param_id,float> controls[]={
    {HYDRA_DSP_PARAM_SIGNAL_PATH_MODE,float(mode)}, {HYDRA_DSP_PARAM_STUDIO_WET_LEVEL,wet},
    {HYDRA_DSP_PARAM_DELAY_ACTIVE,1}, {HYDRA_DSP_PARAM_DELAY_MS,120},
    {HYDRA_DSP_PARAM_ACTIVE_HEADS,7}, {HYDRA_DSP_PARAM_FEEDBACK,85},
    {HYDRA_DSP_PARAM_NOISE,0}, {HYDRA_DSP_PARAM_DROPOUT,0}};
  for(auto c:controls) CHECK(hydra_dsp_set_parameter(h,c.first,c.second)==0);
  CHECK(hydra_dsp_commit(h)==0);
  hydra_dsp_reset(h);
  return h;
}
static void set(hydra_dsp_handle* h,hydra_dsp_param_id id,float v){
  CHECK(hydra_dsp_set_parameter(h,id,v)==0); CHECK(hydra_dsp_commit(h)==0);
}
static void sample(hydra_dsp_handle* h,float in,float& l,float& r){
  hydra_dsp_process(h,&in,&in,&l,&r,1);
}
int main(){
  CHECK(HYDRA_DSP_PARAM_SIGNAL_PATH_MODE==47 && HYDRA_DSP_PARAM_STUDIO_WET_LEVEL==48);
  CHECK(hydra_dsp_get_param_count()==49);
  for(float sr:{44100.f,48000.f}) for(int motion=0;motion<4;++motion){
    auto h=make(sr,1,0);
    set(h,HYDRA_DSP_PARAM_MOTION_MODE,float(motion));
    set(h,HYDRA_DSP_PARAM_SPRING,1); set(h,HYDRA_DSP_PARAM_REVERSE,1);
    set(h,HYDRA_DSP_PARAM_REVERSE_SMEAR,1);
    for(float db:{-24.f,-18.f,-12.f,-6.f,-3.f,-.01f}){
      float amp=std::pow(10.f,db/20.f);
      for(float hz:{100.f,1000.f,8000.f}) for(int n=0;n<4096;++n){
        float x=amp*std::sin(6.28318530718f*hz*n/sr),l,r;
        sample(h,x,l,r); CHECK(l==x && r==x);
      }
      for(float x:{amp,-amp,0.f}){float l,r;sample(h,x,l,r);CHECK(l==x && r==x);}
    }
    float l,r; sample(h,1.5f,l,r);CHECK(l==1.f && r==1.f);
    CHECK(hydra_dsp_set_parameter(h,HYDRA_DSP_PARAM_SIGNAL_PATH_MODE,std::numeric_limits<float>::quiet_NaN())<0);
    CHECK(hydra_dsp_set_parameter(h,HYDRA_DSP_PARAM_STUDIO_WET_LEVEL,std::numeric_limits<float>::infinity())<0);
    hydra_dsp_destroy(h);
  }
  // Deliberate overload exercises the independent full-scale guard.
  {
    auto h=make(48000,1,200);set(h,HYDRA_DSP_PARAM_DRIVE,100);
    int guarded=0;
    for(int n=0;n<48000;++n){
      float l,r;sample(h,.99f*std::sin(n*.13f),l,r);
      CHECK(std::isfinite(l)&&std::isfinite(r));CHECK(std::fabs(l)<=1.f&&std::fabs(r)<=1.f);
      if(std::fabs(l)==1.f||std::fabs(r)==1.f)++guarded;
    }
    CHECK(guarded>0);hydra_dsp_destroy(h);
  }
  // Changing monitor policy/trim must not change motion, history, or feedback.
  // Compare back in Classic after the finite-time 30 ms transition has ended.
  for(int motion=0;motion<4;++motion) for(int feature=0;feature<6;++feature){
    auto a=make(48000,0),b=make(48000,0);
    for(auto h:{a,b}){
      set(h,HYDRA_DSP_PARAM_MOTION_MODE,float(motion));
      if(feature==1)set(h,HYDRA_DSP_PARAM_REVERSE,1);
      if(feature==2)set(h,HYDRA_DSP_PARAM_WARP_ACTIVE,1);
      if(feature==3)set(h,HYDRA_DSP_PARAM_PING_PONG,1);
      if(feature==4)set(h,HYDRA_DSP_PARAM_ACTIVE_HEADS,4);
    }
    float prev=0,maxJump=0,tail=0;
    for(int n=0;n<48000;++n){
      if(n==8000)set(b,HYDRA_DSP_PARAM_SIGNAL_PATH_MODE,1);
      if(n==12000)set(b,HYDRA_DSP_PARAM_STUDIO_WET_LEVEL,20);
      if(n==14000)set(b,HYDRA_DSP_PARAM_STUDIO_WET_LEVEL,175);
      if(n==16000 && feature==5)for(auto h:{a,b})set(h,HYDRA_DSP_PARAM_FREEZE,1);
      if(n==22000)set(b,HYDRA_DSP_PARAM_SIGNAL_PATH_MODE,0);
      float x=n<18000?.12f*std::sin(6.28318530718f*220*n/48000):0;
      float al,ar,bl,br;sample(a,x,al,ar);sample(b,x,bl,br);
      CHECK(std::isfinite(bl)&&std::isfinite(br));CHECK(std::fabs(bl)<=1.f && std::fabs(br)<=1.f);
      if(n>24000){CHECK(al==bl && ar==br);tail+=bl*bl;}
      if(n>1)maxJump=std::max(maxJump,std::fabs(bl-prev));prev=bl;
      float ml,mr,nl,nr;
      CHECK(hydra_dsp_get_motion_telemetry(a,&ml,&mr)==0);
      CHECK(hydra_dsp_get_motion_telemetry(b,&nl,&nr)==0);CHECK(ml==nl && mr==nr);
    }
    CHECK(tail>0);CHECK(maxJump<.3f);
    hydra_dsp_destroy(a);hydra_dsp_destroy(b);
  }
  // Shared auxiliary state must also remain finite under rapid transitions.
  for(int motion=0;motion<4;++motion){
    auto h=make(48000,0);set(h,HYDRA_DSP_PARAM_MOTION_MODE,float(motion));
    set(h,HYDRA_DSP_PARAM_SPRING,1);set(h,HYDRA_DSP_PARAM_REVERSE,1);set(h,HYDRA_DSP_PARAM_REVERSE_SMEAR,1);
    float prev=0,maxJump=0;
    for(int n=0;n<96000;++n){
      if(n%2500==0)set(h,HYDRA_DSP_PARAM_SIGNAL_PATH_MODE,float((n/2500)%2));
      if(n==40000)set(h,HYDRA_DSP_PARAM_FREEZE,1);
      float l,r;sample(h,n<45000?.1f*std::sin(n*.0288f):0,l,r);
      CHECK(std::isfinite(l)&&std::isfinite(r));CHECK(std::fabs(l)<=1.f && std::fabs(r)<=1.f);
      if(n>1)maxJump=std::max(maxJump,std::fabs(l-prev));prev=l;
    }
    CHECK(maxJump<.3f);hydra_dsp_destroy(h);
  }
  for(int mode=0;mode<2;++mode){
    auto h=make(48000,mode);float in[128],l[128],r[128];
    for(int n=0;n<128;++n)in[n]=.1f*std::sin(n*.1f);
    for(int n=0;n<200;++n)hydra_dsp_process(h,in,in,l,r,128);
    auto start=std::chrono::steady_clock::now();
    for(int n=0;n<4000;++n)hydra_dsp_process(h,in,in,l,r,128);
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/4000;
    std::printf("BENCH mode=%d mean_block_ms=%.6f budget_ms=2.666667\n",mode,ms);
    hydra_dsp_destroy(h);
  }
  std::puts("Studio dry, bounds, transitions, history/feedback invariance: PASS");
}
