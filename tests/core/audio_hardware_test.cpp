#include "hydra_audio_hardware.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace hydra::hardware;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main() {
 struct Vector { uint32_t slot; int32_t value; };
 const Vector vectors[]={{0,0},{0x00000100,1},{0xffffff00,-1},{0x7fffff00,8388607},{0x80000000,-8388608},{0x12345600,1193046},{0xedcbaa00,-1193046},{0x000001ff,1},{0xffffff7f,-1}};
 for(auto v:vectors) { CHECK(pcm24Signed(v.slot)==v.value); CHECK(pcm24Float(v.slot)==v.value/8388608.0f); }
 CHECK(pcm32(1)==INT32_MAX); CHECK(pcm32(-1)==INT32_MIN);
 CHECK(pcm32(2)==INT32_MAX); CHECK(pcm32(-2)==INT32_MIN);
 CHECK(pcm32(std::numeric_limits<float>::quiet_NaN())==0);
 CHECK(pcm32(pcm24Float(0x00000100))==256);
 CHECK(pcm32(pcm24Float(0xffffff00))==-256);
 Capture c; c.add(0x40000000,0xe0000000); c.add(0xc0000000,0x20000000);
 CHECK(c.frames==2 && c.peak[0]==0.5f && c.peak[1]==0.25f);
 CHECK(c.sum[0]==0 && c.sum[1]==0 && c.squares[0]==0.5f && c.squares[1]==0.125f);
 c.add(0x7fffff00,0x80000000); CHECK(c.clips[0]==1 && c.clips[1]==1);
 uint32_t phase=0; float peak=0; double sum=0,energy=0;
 for(unsigned i=0;i<44100;++i) { float t=tone(phase); CHECK(std::isfinite(t) && std::fabs(t)<=0.10001f); peak=std::fmax(peak,std::fabs(t));sum+=t;energy+=t*t; }
 CHECK(peak>0.0999f && std::fabs(sum/44100)<1e-6 && std::fabs(std::sqrt(energy/44100)-0.07071)<0.0001);
 int32_t l=1,r=1;
 diagnosticFrame(Route::Silence,0.5f,-0.5f,phase,l,r); CHECK(l==0 && r==0);
 diagnosticFrame(Route::Monitor,0.5f,-0.25f,phase,l,r); CHECK(l==268435456 && r==-134217728);
 phase=0; diagnosticFrame(Route::Left,0,0,phase,l,r); CHECK(l!=0 && r==0);
 phase=0; diagnosticFrame(Route::Right,0,0,phase,l,r); CHECK(l==0 && r!=0);
 phase=0; diagnosticFrame(Route::Tone,0,0,phase,l,r); CHECK(l!=0 && l==r);
 const int pins[]={mclkPin,bclkPin,wsPin,doutPin,dinPin,1,2,4,5,6,7,48,43,44,19,20};
 for(unsigned i=0;i<sizeof(pins)/sizeof(*pins);++i) for(unsigned j=i+1;j<sizeof(pins)/sizeof(*pins);++j) CHECK(pins[i]!=pins[j]);
 CHECK(mclkPin!=0 && mclkPin!=3 && mclkPin!=45 && mclkPin!=46 && !(mclkPin>=26 && mclkPin<=37));
 CHECK(160000000ULL*441==11289600ULL*(14*441+76));
 CHECK(sampleRate*mclkMultiple==11289600 && sampleRate*64==2822400);
 std::puts("PCM24, PCM32, stereo statistics, oscillator and Live GPIO contracts PASS");
}
