#include "../src/core/Spectrum.h"
#include "../src/core/Tape.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace spectrum;
struct Bytes final : Reader {
  std::vector<uint8_t> data;uint32_t at=0;bool tzx=false;
  explicit Bytes(bool format=false):tzx(format) {
    if(tzx) data={'Z','X','T','a','p','e','!',0x1a,1,20};
  }
  size_t read(uint8_t *out,size_t n) override {
    n=std::min(n,data.size()-at);if(n) std::memcpy(out,data.data()+at,n);at+=n;return n;
  }
  bool seek(uint32_t n) override {if(n>data.size()) return false;at=n;return true;}
  uint32_t position() const override {return at;}
  uint32_t size() const override {return uint32_t(data.size());}
  void block(uint8_t flag,const std::vector<uint8_t> &payload) {
    if(tzx) data.insert(data.end(),{0x10,0xe8,3});
    const size_t length=payload.size()+2;data.push_back(length&255);data.push_back(length>>8);
    data.push_back(flag);uint8_t check=flag;
    for(uint8_t b:payload) {data.push_back(b);check^=b;}data.push_back(check);
  }
};
void readRom(Machine &m,const char *path) {
  std::ifstream f(path,std::ios::binary);assert(f);
  f.read(reinterpret_cast<char*>(m.memory),16384);assert(f.gcount()==16384 && f.peek()==EOF);
}
void loadBytes(const char *rom,bool tzx,uint8_t flag,unsigned count,bool corrupt=false) {
  Machine m;readRom(m,rom);Tape tape;Bytes input(tzx);
  std::vector<uint8_t> payload(count);for(unsigned i=0;i<payload.size();++i) payload[i]=uint8_t(i*73);
  input.block(flag,payload);if(corrupt) input.data.back()^=1;
  assert(tape.mount(input,tzx));m.tape=&tape;
  m.cpu.pc=0x0556;m.cpu.sp=0xff00;m.memory[0xff00]=0;m.memory[0xff01]=0x80;
  m.cpu.a=flag;m.cpu.d=payload.size()>>8;m.cpu.e=payload.size()&255;m.cpu.ix=0x9000;m.cpu.cf=true;
  while(m.frames<400 && m.cpu.pc!=0x8000) m.step();
  const bool ok=m.cpu.pc==0x8000 && m.cpu.cf!=corrupt && !std::memcmp(m.memory+0x9000,payload.data(),payload.size());
  std::printf("ROM LD-BYTES %s flag=%02x corrupt=%u: %s pc=%04x carry=%u ix=%04x de=%04x frames=%u pos=%u/%u\n",
    tzx?"TZX":"TAP",flag,corrupt,ok?"OK":"FAIL",m.cpu.pc,m.cpu.cf,m.cpu.ix,(m.cpu.d<<8)|m.cpu.e,m.frames,input.position(),input.size());
  assert(ok);
}
void basicLoad(const char *rom,bool tzx) {
  Machine m;readRom(m,rom);Tape tape;Bytes input(tzx);
  // 10 BORDER VAL "4": POKE VAL "32768",VAL "42": STOP
  const std::vector<uint8_t> statement={0xe7,0xb0,'"','4','"',':',0xf4,0xb0,'"','3','2','7','6','8','"',',',0xb0,'"','4','2','"',':',0xe2,13};
  std::vector<uint8_t> program={0,10,uint8_t(statement.size()),0};
  program.insert(program.end(),statement.begin(),statement.end());
  std::vector<uint8_t> header={0,'T','A','P','T','E','S','T',' ',' ',' ',uint8_t(program.size()),0,10,0,uint8_t(program.size()),0};
  input.block(0,header);input.block(255,program);assert(tape.mount(input,tzx));tape.pause();m.tape=&tape;
  unsigned calls=0;bool wasLoader=false;
  for(unsigned frame=0;frame<1800 && m.memory[0x8000]!=42;++frame) {
    m.releaseKeys();
    if(frame>=250 && frame<290) {
      const char keys[4]={'j','"','"','\n'};
      if(frame%10<5) m.ascii(keys[(frame-250)/10]);
    }
    if(frame==299) tape.resume();
    const auto start=m.frames;
    while(start==m.frames) {
      if(m.cpu.pc==0x0556 && !wasLoader) ++calls;
      wasLoader=m.cpu.pc==0x0556;m.step();
    }
    if(frame==298) {
      const uint16_t line=m.memory[0x5c59]|(m.memory[0x5c5a]<<8);
      printf("Before tape: pc=%04x E_LINE=%04x text=",m.cpu.pc,line);
      for(unsigned i=0;i<12;++i) printf("%02x ",m.memory[uint16_t(line+i)]);puts("");
    }
  }
  const bool ok=m.memory[0x8000]==42 && m.border==4 && calls==2 && m.memory[0x5c3a]==8;
  printf("ROM BASIC autoload %s: %s pc=%04x marker=%u border=%u frames=%u pos=%u/%u LD-BYTES calls=%u ERR_NR=%u\n",
    tzx?"TZX":"TAP",ok?"OK":"FAIL",m.cpu.pc,m.memory[0x8000],m.border,m.frames,input.position(),input.size(),calls,m.memory[0x5c3a]);
  assert(ok);
}
int main(int argc,char **argv) {
  assert(argc==2);
  for(bool tzx:{false,true}) {loadBytes(argv[1],tzx,0,17);loadBytes(argv[1],tzx,255,257);basicLoad(argv[1],tzx);}
  loadBytes(argv[1],false,255,257,true);
}
