#include "../src/core/Spectrum.h"
#include "../src/core/Tape.h"
#include "../src/core/Snapshot.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace spectrum;
struct MemoryReader final : Reader {
  std::vector<uint8_t> data;uint32_t at=0;
  MemoryReader(std::vector<uint8_t> bytes):data(std::move(bytes)) {}
  size_t read(uint8_t *out,size_t n) override {
    n=std::min(n,data.size()-at);std::memcpy(out,data.data()+at,n);at+=n;return n;
  }
  bool seek(uint32_t n) override {if(n>data.size()) return false;at=n;return true;}
  uint32_t position() const override {return at;}
  uint32_t size() const override {return uint32_t(data.size());}
};
void put(std::vector<uint8_t> &v,unsigned at,uint16_t n) {v[at]=n&255;v[at+1]=n>>8;}
std::vector<uint8_t> tzx(std::initializer_list<uint8_t> blocks) {
  std::vector<uint8_t> v={'Z','X','T','a','p','e','!',0x1a,1,20};v.insert(v.end(),blocks);return v;
}
void keyboardAndCpu() {
  Machine m;m.memory[0]=0x3e;m.memory[1]=0x42;
  m.memory[2]=0x32;m.memory[3]=0;m.memory[4]=0;
  m.memory[5]=0x32;m.memory[6]=0;m.memory[7]=0x80;m.memory[8]=0x76;
  for(unsigned i=0;i<5;++i) m.step();
  assert(m.memory[0]==0x3e && m.memory[0x8000]==0x42);
  m.releaseKeys();m.ascii('a');assert((m.input(0xfdfe)&31)==30);assert((m.input(0xfbfe)&31)==31);
  m.ascii('q');assert((m.input(0xf9fe)&31)==30);
  m.releaseKeys();m.ascii('"');assert((m.rows[7]&2)==0 && (m.rows[5]&1)==0);
  m.releaseKeys();m.ascii('#');assert((m.rows[7]&2)==0 && (m.rows[3]&4)==0);
  m.releaseKeys();m.ascii(';');assert((m.rows[7]&2)==0 && (m.rows[5]&2)==0);
  m.releaseKeys();m.ascii('\b');assert(!(m.rows[0]&1) && !(m.rows[4]&1));
  m.releaseKeys();m.direction(Controls::Kempston,true,false,false,true,true);assert(m.input(0x1f)==25);
  m.releaseKeys();m.direction(Controls::Sinclair,true,false,true,false,true);assert((m.rows[4]&0x13)==0);
  m.releaseKeys();m.direction(Controls::Cursor,false,true,false,false,false);assert(!(m.rows[0]&1) && !(m.rows[4]&16));
  m.releaseKeys();m.ascii('a');
  // IN A,(n) uses A as the high byte; IN r,(C) uses B as the high byte.
  const uint8_t program[]={0x3e,0xfd,0xdb,0xfe,0x32,0,0x90,0x01,0xfe,0xfb,0xed,0x50,0x7a,0x32,1,0x90,0x76};
  memcpy(m.memory+0x8000,program,sizeof(program));m.cpu.pc=0x8000;m.cpu.halted=false;
  for(unsigned i=0;i<12;++i) m.step();
  assert((m.memory[0x9000]&31)==30 && (m.memory[0x9001]&31)==31);
  m.reset();m.runFrame();assert(m.frames==1 && m.frameCycle<32);
  // A HALTed CPU accepts a PAL interrupt and stores the return PC on its stack.
  m.cpu.pc=0x8000;m.memory[0x8000]=0x76;m.cpu.sp=0xff00;m.cpu.iff1=true;m.cpu.interrupt_mode=1;
  m.frameCycle=0;m.step();assert(m.cpu.pc==0x38 && m.cpu.sp==0xfefe);
  puts("OK ROM protection, 16-bit I/O, keyboard matrix, joystick and PAL interrupt");
}
void snapshots() {
  Machine m;std::vector<uint8_t> work(49152);const char *error;
  std::vector<uint8_t> sna(49179,0);sna[0]=0x3f;sna[19]=4;sna[21]=0xa5;sna[22]=0x12;sna[25]=1;sna[26]=6;
  put(sna,23,0x8000);put(sna,27+0x4000,0x8123);
  MemoryReader sr(sna);assert(loadSnapshot(m,sr,SnapshotKind::Sna,work.data(),error));
  assert(m.cpu.pc==0x8123 && m.cpu.sp==0x8002 && flags(m.cpu)==0xa5 && m.border==6 && m.cpu.iff1);
  auto bad=sna;put(bad,23,0xffff);MemoryReader br(bad);
  m.memory[0x9000]=77;assert(!loadSnapshot(m,br,SnapshotKind::Sna,work.data(),error));assert(m.cpu.pc==0x8123 && m.memory[0x9000]==77);
  std::vector<uint8_t> z(30+49152,0x55);std::fill(z.begin(),z.begin()+30,0);put(z,6,0x9000);z[29]=2;z[12]=5;
  MemoryReader zr(z);assert(loadSnapshot(m,zr,SnapshotKind::Z80,work.data(),error));
  assert(m.cpu.pc==0x9000 && m.cpu.interrupt_mode==2 && m.cpu.r==128 && m.memory[0xffff]==0x55);
  z.resize(30);z[12]=32;
  unsigned left=49152;while(left) {const unsigned n=std::min(255u,left);z.insert(z.end(),{0xed,0xed,uint8_t(n),0x69});left-=n;}
  z.insert(z.end(),{0,0xed,0xed,0});MemoryReader cr(z);assert(loadSnapshot(m,cr,SnapshotKind::Z80,work.data(),error));
  assert(m.memory[0x4000]==0x69 && m.memory[0xffff]==0x69);
  z[32]=0;MemoryReader bomb(z);assert(!loadSnapshot(m,bomb,SnapshotKind::Z80,work.data(),error));assert(m.memory[0xffff]==0x69);
  for(unsigned extLength:{23,54,55}) {
    std::vector<uint8_t> v(32+extLength,0);put(v,30,extLength);put(v,32,0x8123);v[29]=1;
    if(extLength>=54) {v[61]=255;v[62]=255;}
    for(uint8_t page:{8,4,5}) {v.insert(v.end(),{255,255,page});v.insert(v.end(),16384,page);}
    MemoryReader pr(v);assert(loadSnapshot(m,pr,SnapshotKind::Z80,work.data(),error));
    assert(m.memory[0x4000]==8 && m.memory[0x8000]==4 && m.memory[0xc000]==5);
    v[34]=4;MemoryReader large(v);assert(!loadSnapshot(m,large,SnapshotKind::Z80,work.data(),error));
    v[34]=0;v.pop_back();MemoryReader cut(v);assert(!loadSnapshot(m,cut,SnapshotKind::Z80,work.data(),error));
  }
  // Truncated headers and inputs must never reach outside their stream.
  for(unsigned n=0;n<30;++n) {MemoryReader cut(std::vector<uint8_t>(n,0));assert(!loadSnapshot(m,cut,SnapshotKind::Z80,work.data(),error));}
  puts("OK SNA, Z80 v1/v2/v3, RLE, transactional failure and 128K rejection");
}
void tapes() {
  MemoryReader tap({2,0,0,0});Tape t;assert(t.mount(tap,false));assert(!t.level());
  t.advance(2167);assert(!t.level());t.advance(1);assert(t.level());t.advance(2168);assert(!t.level());
  t.pause();const auto at=t.position();t.advance(100000);assert(t.position()==at && !t.playing());t.resume();assert(t.playing());
  t.advance(2168u*8061);assert(t.level());t.advance(667);assert(!t.level());t.advance(735);assert(t.level());
  t.advance(855);assert(!t.level());t.advance(855);assert(t.level());
  // The final checksum edge must survive the start of the inter-block pause.
  assert(t.mount(tap,false));
  t.advance(8063u*2168+667+735+2u*16*855);
  assert(t.playing() && t.level());t.advance(3499);assert(t.level());
  t.advance(1);assert(!t.level());
  MemoryReader noPause(tzx({0x10,0,0,2,0,0,0}));
  assert(t.mount(noPause,true));t.advance(8063u*2168+667+735+2u*16*855);
  assert(!t.playing() && t.level());
  MemoryReader pure(tzx({0x14,10,0,20,0,1,0,0,1,0,0,0x80}));
  assert(t.mount(pure,true));t.advance(19);assert(!t.level());t.advance(1);assert(t.level());t.advance(20);assert(!t.playing() && !t.error());
  MemoryReader loop(tzx({0x24,2,0,0x12,10,0,2,0,0x25}));
  assert(t.mount(loop,true));t.advance(39);assert(t.playing());t.advance(1);assert(!t.playing() && !t.error());
  MemoryReader direct(tzx({0x15,10,0,0,0,2,1,0,0,0x80}));
  assert(t.mount(direct,true));assert(t.level());t.advance(10);assert(!t.level());t.advance(10);assert(!t.playing());
  MemoryReader stopped(tzx({0x12,10,0,1,0,0x20,0,0,0x12,10,0,2,0}));
  assert(t.mount(stopped,true));t.advance(10);assert(!t.playing());t.resume();t.advance(19);assert(t.playing());t.advance(1);assert(!t.playing());
  MemoryReader unsupported(tzx({0x18}));assert(!t.mount(unsupported,true) && t.error());
  MemoryReader truncated({3,0,0});assert(!t.mount(truncated,false) && t.error());
  MemoryReader zero(tzx({0x12,0,0,1,0}));assert(!t.mount(zero,true));
  MemoryReader startsPaused(tzx({0x20,0,0,0x12,10,0,1,0}));
  assert(t.mount(startsPaused,true) && !t.playing());t.resume();t.advance(10);assert(!t.playing() && !t.error());
  MemoryReader turbo(tzx({0x11,10,0,20,0,30,0,40,0,50,0,2,0,1,0,0,1,0,0,0}));
  assert(t.mount(turbo,true));t.advance(10);assert(t.level());t.advance(10);assert(!t.level());
  t.advance(20);assert(t.level());t.advance(30);assert(!t.level());t.advance(40);assert(t.level());
  t.advance(40);assert(!t.playing() && !t.error());
  // CPU reads a generated tape signal and produces real ULA border changes.
  Machine m;MemoryReader tone(tzx({0x12,100,0,200,0}));assert(t.mount(tone,true));m.tape=&t;
  const uint8_t loader[]={0xdb,0xfe,0xe6,0x40,0x0f,0x0f,0x0f,0x0f,0xd3,0xfe,0xc3,0,0x80};
  memcpy(m.memory+0x8000,loader,sizeof(loader));m.cpu.pc=0x8000;
  unsigned high=0,low=0;
  for(unsigned i=0;i<1200;++i) {m.step();if(m.border==4) ++high;if(m.border==0) ++low;}
  assert(high>10 && low>10);
  puts("OK TAP pilot/sync/data, TZX pulses/data/loops/direct/pause, malformed media and CPU EAR input");
}
void video(const char *path) {
  Machine m;
  for(unsigned y=0;y<192;++y) for(unsigned x=0;x<32;++x) {
    const unsigned at=((y&0xc0)<<5)|((y&7)<<8)|((y&0x38)<<2)|x;
    m.memory[0x4000+at]=(y%8==0)?0xff:0x81;
  }
  for(unsigned i=0;i<768;++i) m.memory[0x5800+i]=uint8_t((i%8)|((7-i/32%8)<<3)|64);
  uint8_t *screen=m.memory+0x4000;
  screen[0]=0x80;screen[6144]=0x80|64|2|(1<<3);
  assert(pixel(screen,0,0,false)==10 && pixel(screen,0,0,true)==9);
  assert(colour(0)==0 && colour(8)==0 && colour(15)==0xffff && colour(9)==0x1f);
  FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n240 135\n255\n");
  for(unsigned y=0;y<135;++y) {
    uint16_t line[240];renderRow(screen,m.borders,false,false,y,line);
    for(uint16_t c:line) {const uint8_t rgb[]={uint8_t(((c>>11)&31)*255/31),uint8_t(((c>>5)&63)*255/63),uint8_t((c&31)*255/31)};fwrite(rgb,1,3,f);}
    renderRow(screen,m.borders,true,true,y,line);
    if(y==0) assert(line[0]==colour(7));
  }
  fclose(f);puts("OK ULA bitmap layout, BRIGHT/FLASH, RGB565, filtered LCD and border rendering");
}
int main(int argc,char **argv) {
  assert(argc==2);keyboardAndCpu();snapshots();tapes();video(argv[1]);
  puts("All host integration checks passed (AddressSanitizer + UBSan).");
}
