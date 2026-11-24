// Offline probe: directly invokes the frozen canonical filter implementations.
// Never linked into firmware; no copied filter equations.
#include "../../../dsp/core/tape_core.cpp"
#include <fstream>
#include <string>
using namespace hydra::dsp;
int main(int argc,char**argv){
 if(argc!=2)return 2;
 for(int speed: {0,50,100})for(int age: {0,50,100}){
  float s=speed*.01f,a=age*.01f,fs=44100.f;
  float bump=60+140*s,cut=std::max(400.f,(6000+10000*s)*(1-.9f*a));
  float fbcut=std::max(1600.f,(9000+7000*s)*(1-.65f*a));
  for(int stage=0;stage<7;++stage){
   BiquadFilter hp,bp,pb,lp,sh; OnePoleLP gap; AllpassFilter phase;
   hp.setHighpass(fs,300,.5f); bp.setPeak(fs,bump,.8f,1.f);
   pb.setPeak(fs,bump,.9f,3.f); lp.setLowpass(fs,cut,.707f);
   sh.setHighShelf(fs,cut*2,.5f,-12); gap.setCutoff(fs,fbcut);phase.setCoeff(.3f+.4f*a);
   std::ofstream out(std::string(argv[1])+"/filter_"+std::to_string(speed)+"_"+std::to_string(age)+"_"+std::to_string(stage)+".f32",std::ios::binary);
   for(int i=0;i<44100;++i){float x=i==0?1.f:0.f,y=x;
    if(stage==0)y=hp.process(x);
    if(stage==1)y=bp.process(x);
    if(stage==2)y=gap.process(x);
    if(stage==3)y=phase.process(gap.process(bp.process(hp.process(x))));
    if(stage==4)y=lp.process(pb.process(sh.process(x)));
    if(stage==5)y=phase.process(gap.process(bp.process(hp.process(lp.process(pb.process(sh.process(x)))))));
    if(stage==6)y=pb.process(x);
    out.write(reinterpret_cast<char*>(&y),sizeof y);
   }
  }
 }
}

