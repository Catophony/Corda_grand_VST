#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <fstream>

static constexpr float PI = 3.14159265358979323846f;
struct Delay {
    std::vector<float> d; int w=0;
    void init(int n){ d.assign(n,0.f); w=0; }
    float read(float delay) const {
        float r = (float)w - delay;
        while (r < 0) r += (float)d.size();
        int i=(int)r; float f=r-(float)i; int j=(i+1)%(int)d.size();
        return d[i]*(1.f-f)+d[j]*f;
    }
    void write(float x){ d[w]=x; if(++w>=(int)d.size()) w=0; }
    void clear(){ std::fill(d.begin(),d.end(),0.f); w=0; }
};
struct AP { float a=0,z=0; float process(float x){ float y=a*x+z; z=x-a*y; return y; } };
struct LP { float a=0,z=0; float process(float x){ z=(1.f-a)*x+a*z; return z; } };

static float apPhaseDelay(float a, float w){
    // phase of (a + e^-jw)/(1 + a e^-jw), return -phase/w
    float nr=a+std::cos(w), ni=-std::sin(w);
    float dr=1+a*std::cos(w), di=-a*std::sin(w);
    float ph=std::atan2(ni,nr)-std::atan2(di,dr);
    return -ph/w;
}
static float lpPhaseDelay(float a, float w){
    // (1-a)/(1-a e^-jw)
    float dr=1-a*std::cos(w), di=a*std::sin(w);
    float ph=-std::atan2(di,dr);
    return -ph/w;
}

int main(){
    const float fs=48000.f;
    const float f0=261.6256f;
    const float B=0.0003f;
    const float T60=8.f;
    const float p=0.125f;
    const float contact=0.002f;
    const float m=0.008f;
    const float K=m*std::pow(PI/contact,2);
    const float pnl=2.5f;
    const float R=2.f*0.2f*std::sqrt(K*m);
    const float v0=3.0f;
    // dispersion: two allpasses, negative a -> sharp high partials
    const int M=2; const float apc=-0.20f;
    const float aLP=0.22f;
    const float w0=2*PI*f0/fs;
    float pd = M*apPhaseDelay(apc,w0) + lpPhaseDelay(aLP,w0);
    float L = fs/f0 - pd;
    float g = std::pow(10.f, -3.f/(T60*f0));
    printf("L %.4f pd %.4f g %.7f K %.1f R %.2f\n",L,pd,g,K,R);
    Delay line, forceLine; line.init(4096); forceLine.init(4096);
    AP ap[M]; for(auto& x:ap)x.a=apc;
    LP lp; lp.a=aLP;
    float h=0, v=v0, yprev=0;
    const int n=48000*6;
    std::vector<float> out(n);
    float peak=0;
    for(int i=0;i<n;i++){
        // string displacement at strike: tap near p*L
        float ys = line.read(p*L);
        float comp = h - ys*0.02f;
        float F=0;
        if(i<20000 && comp>0){
            F=K*std::pow(comp,pnl)+R*v;
            if(F<0)F=0;
            v -= F/m/fs;
            h += v/fs;
        } else if(i>=20000) { /* force ends */ }
        else { h += v/fs; }
        // strike comb
        forceLine.write(F);
        float fd=forceLine.read(2*p*L);
        float exc=(F-fd)*2.0e-4f;
        float y=line.read(L);
        float loop=lp.process(y);
        for(int k=0;k<M;k++) loop=ap[k].process(loop);
        loop*=g;
        line.write(loop+exc);
        out[i]=y;
        peak=std::max(peak,std::abs(y));
    }
    printf("raw peak %.5f\n",peak);
    // highpass and normalize to -3 dB
    float hp=0, prev=0;
    for(int i=0;i<n;i++){
        float x=out[i];
        hp=0.995f*(hp+x-prev); prev=x; out[i]=hp;
    }
    float mx=0; for(float x:out) mx=std::max(mx,std::abs(x));
    if(mx>0) for(float& x:out) x*=0.7f/mx;
    std::ofstream f("build/wg_test.wav",std::ios::binary);
    auto u32=[&](uint32_t v){f.write((char*)&v,4);}; auto u16=[&](uint16_t v){f.write((char*)&v,2);};
    uint32_t data=n*2*3; f.write("RIFF",4);u32(36+data);f.write("WAVEfmt ",8);u32(16);u16(1);u16(2);u32(48000);u32(48000*6);u16(6);u16(24);
    f.write("data",4);u32(data);
    for(int i=0;i<n;i++){ int32_t s=(int32_t)std::lround(std::clamp(out[i],-1.f,1.f)*8388607.f); for(int c=0;c<2;c++){f.put(s&255);f.put((s>>8)&255);f.put((s>>16)&255);} }
}
