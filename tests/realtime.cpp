#include "../src/platform/AudioQueue.h"
#include "../src/core/Spectrum.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <thread>
#include <vector>

using spectrum_audio::Queue;
void queueOwnership() {
  Queue queue;
  for(unsigned i=0;i<(Queue::kPackets+1)*Queue::kSamples;++i) queue.push(int16_t(i));
  assert(queue.dropped()==Queue::kSamples && queue.packets()==Queue::kPackets);
  int16_t packet[Queue::kSamples];
  for(unsigned block=0;block<Queue::kPackets;++block) {
    assert(queue.pop(packet));
    for(unsigned i=0;i<Queue::kSamples;++i) assert(packet[i]==int16_t(block*Queue::kSamples+i));
  }
  assert(!queue.pop(packet));queue.push(123);queue.clear();assert(!queue.buffered());
  constexpr unsigned count=12000;
  std::thread consumer([&] {
    int16_t buffer[Queue::kSamples];
    for(unsigned block=0;block<count;++block) {
      while(!queue.pop(buffer)) std::this_thread::yield();
      for(unsigned i=0;i<Queue::kSamples;++i) assert(buffer[i]==int16_t(block*Queue::kSamples+i));
    }
  });
  for(unsigned block=0;block<count;++block) {
    while(queue.packets()>=Queue::kPackets-1) std::this_thread::yield();
    for(unsigned i=0;i<Queue::kSamples;++i) queue.push(int16_t(block*Queue::kSamples+i));
  }
  consumer.join();assert(!queue.buffered() && queue.dropped()==Queue::kSamples);
  puts("OK audio FIFO wraparound, packet ownership, full queue and concurrent producer/consumer");
}
void jitter() {
  Queue queue;unsigned samplePhase=0,nextRead=0,starved=0,played=0,produced=0;
  bool started=false;
  for(unsigned ms=0;ms<60000;++ms) {
    // A 25 ms blocking read/input stall once a second, as well as 20 ms frame bursts.
    const bool producerStalled=ms%1000>=700 && ms%1000<725;
    if(!producerStalled && queue.buffered()<640) {
      samplePhase+=spectrum::kFrameCycles*4;
      while(samplePhase>=875) {samplePhase-=875;queue.push(int16_t(produced++));}
    }
    if(!started && queue.packets()>=4) {started=true;nextRead=ms+10;}
    if(started && ms>=nextRead) {
      int16_t packet[Queue::kSamples];
      if(queue.pop(packet)) {
        for(auto sample:packet) assert(sample==int16_t(played++));
      } else ++starved;
      nextRead+=10;
    }
  }
  assert(starved==0 && queue.dropped()==0 && played>950000);
  printf("OK 60 s audio pacing with PAL sample fractions and 25 ms stalls: %u samples, no gaps or drops\n",played);
}
void samplesAndRaster() {
  spectrum::Machine m;unsigned count=0;
  m.audio=[](void *p,int16_t) {++*static_cast<unsigned*>(p);};m.audioContext=&count;
  for(unsigned frame=0;frame<500;++frame) {
    m.runFrame();assert(m.frameCycle<spectrum::kFrameCycles && m.scanline==m.frameCycle/224);
    assert(m.nextScanline==(m.scanline+1)*224);
  }
  assert(count==159744);
  m.audioEnabled=false;m.runFrame();assert(count==159744);
  puts("OK exact 16 kHz sample clock, PAL lines and fast-mode audio suppression");
}
// Independent reference: the original rational-area implementation, deliberately
// kept here to verify identical pixels after the arithmetic optimization.
void referenceRow(const uint8_t *screen,const uint8_t *borders,bool flash,bool border,unsigned y,uint16_t *out) {
  const unsigned width=border?320:256,height=border?240:192,y0=y*height,y1=(y+1)*height;
  for(unsigned x=0;x<240;++x) {
    const unsigned x0=x*width,x1=(x+1)*width;unsigned r=0,g=0,b=0,total=0;
    for(unsigned sy=y0/135;sy<=(y1-1)/135;++sy) for(unsigned sx=x0/240;sx<=(x1-1)/240;++sx) {
      const unsigned wy=std::min(y1,(sy+1)*135)-std::max(y0,sy*135);
      const unsigned wx=std::min(x1,(sx+1)*240)-std::max(x0,sx*240),weight=wx*wy;
      const auto index=border && (sx<32 || sx>=288 || sy<24 || sy>=216)?borders[sy+40]&7:
        spectrum::pixel(screen,sx-(border?32:0),sy-(border?24:0),flash);
      const auto c=spectrum::colour(index);
      r+=((c>>11)&31)*weight;g+=((c>>5)&63)*weight;b+=(c&31)*weight;total+=weight;
    }
    out[x]=uint16_t((((r+total/2)/total)<<11)|(((g+total/2)/total)<<5)|((b+total/2)/total));
  }
}
void scaler() {
  uint8_t screen[6912],borders[312];uint32_t random=0x654321;
  for(unsigned pattern=0;pattern<5;++pattern) {
    for(auto &pixel:screen) {random=random*1664525+1013904223;pixel=random>>24;}
    for(auto &c:borders) {random=random*1664525+1013904223;c=random&7;}
    for(bool border:{false,true}) for(bool flash:{false,true}) {
      spectrum::DisplayScaler scaler;scaler.begin(screen,borders,flash,border);
      for(unsigned y=0;y<135;++y) {
      uint16_t expected[240],actual[240];referenceRow(screen,borders,flash,border,y,expected);
      spectrum::renderRow(screen,borders,flash,border,y,actual);
      assert(std::equal(expected,expected+240,actual));
      scaler.row(y,actual);assert(std::equal(expected,expected+240,actual));
      }
    }
  }
  puts("OK optimized LCD filter matches all reference pixels, with border and FLASH");
}
int main() {queueOwnership();jitter();samplesAndRaster();scaler();}
