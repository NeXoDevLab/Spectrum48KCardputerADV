#include "Spectrum.h"
#include "Tape.h"
#include <chrono>
#include <cstdio>
#include <cstring>

using Clock=std::chrono::steady_clock;
using namespace spectrum;
void countSample(void *p,int16_t) {++*static_cast<unsigned*>(p);}
int main() {
  Machine machine;Tape idle;machine.tape=&idle;unsigned samples=0;
  machine.audio=countSample;machine.audioContext=&samples;
  const uint8_t tone[]={0x3e,16,0xd3,0xfe,0xee,16,0x06,80,0x10,0xfe,0xc3,2,0x80};
  for(bool beep:{false,true}) {
    machine.reset();samples=0;
    if(beep) {std::memcpy(machine.memory+0x8000,tone,sizeof(tone));machine.cpu.pc=0x8000;}
    const auto start=Clock::now();
    for(unsigned frame=0;frame<500;++frame) machine.runFrame();
    const double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    std::printf("cpu_%s_ms_per_frame=%.4f samples=%u\n",beep?"beeper":"nop",ms/500,samples);
  }
  for(unsigned i=0;i<6912;++i) machine.memory[0x4000+i]=uint8_t((i*73)^(i>>3));
  for(unsigned i=0;i<312;++i) machine.borders[i]=i&7;
  for(bool border:{false,true}) {
    uint16_t line[240];uint32_t hash=2166136261u;
    const auto start=Clock::now();
    for(unsigned frame=0;frame<100;++frame) {
#ifdef SPECTRUM_SCALER_WORKSPACE
      DisplayScaler scaler;scaler.begin(machine.memory+0x4000,machine.borders,frame&1,border);
#endif
      for(unsigned y=0;y<135;++y) {
#ifdef SPECTRUM_SCALER_WORKSPACE
      scaler.row(y,line);
#else
      renderRow(machine.memory+0x4000,machine.borders,frame&1,border,y,line);
#endif
      for(auto pixel:line) {hash^=pixel;hash*=16777619u;}
      }
    }
    const double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    std::printf("video_%s_ms_per_frame=%.4f checksum=%08x\n",border?"border":"active",ms/100,hash);
  }
}
