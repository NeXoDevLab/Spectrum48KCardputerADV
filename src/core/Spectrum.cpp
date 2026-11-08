#include "Spectrum.h"
#include "Tape.h"
#include <cstring>

namespace spectrum {
namespace {
uint8_t readMemory(void *p,uint16_t addr) { return static_cast<Machine*>(p)->memory[addr]; }
void writeMemory(void *p,uint16_t addr,uint8_t v) {
  if(addr>=0x4000) static_cast<Machine*>(p)->memory[addr]=v;
}
uint8_t inputPort(z80 *cpu,uint16_t port) {return static_cast<Machine*>(cpu->userdata)->input(port);}
void outputPort(z80 *cpu,uint16_t port,uint8_t v) {static_cast<Machine*>(cpu->userdata)->output(port,v);}
const char *const matrix[8]={"^zxcv","asdfg","qwert","12345","09876","poiuy","\nlkjh"," #mnb"};
}
Machine::Machine() { reset(); }
void Machine::reset(bool clearRam) {
  if(clearRam) std::memset(memory+0x4000,0,49152);
  z80_init(&cpu);
  cpu.userdata=this;cpu.read_byte=readMemory;cpu.write_byte=writeMemory;
  cpu.port_in=inputPort;cpu.port_out=outputPort;
  frameCycle=frames=samplePhase=0;audioFilter=lastLevel=0;ula=0;border=7;
  std::memset(borders,border,sizeof(borders));releaseKeys();
}
void Machine::releaseKeys() {std::memset(rows,0x1f,sizeof(rows));joystick=0;}
void Machine::press(uint8_t row,uint8_t bit) {if(row<8 && bit<5) rows[row]&=~(1u<<bit);}
bool Machine::ascii(char ch) {
  if(ch=='\b') {press(0,0);press(4,0);return true;}
  if(ch==27) {press(0,0);press(7,0);return true;}
  if(ch>='A' && ch<='Z') {press(0,0);ch=char(ch-'A'+'a');}
  // Punctuation on a PC keyboard becomes SYMBOL SHIFT plus the Spectrum key.
  const char *symbols="!@#$%&'()_:\"?/*,.;=+-<>";
  const char *symbolKeys="1234567890zpcvbnmolkjrt";
  // '#' is a symbol, not the internal matrix placeholder for SYMBOL SHIFT.
  const char *s=std::strchr(symbols,ch);
  if(s && ch) {press(7,1);ch=symbolKeys[s-symbols];}
  for(unsigned row=0;row<8;++row) for(unsigned bit=0;bit<5;++bit)
    if(ch && matrix[row][bit]==ch) {press(row,bit);return true;}
  return false;
}
void Machine::direction(Controls mode,bool up,bool down,bool left,bool right,bool fire) {
  if(mode==Controls::Kempston) joystick=(right?1:0)|(left?2:0)|(down?4:0)|(up?8:0)|(fire?16:0);
  else if(mode==Controls::Sinclair) {
    if(left) ascii('6');if(right) ascii('7');if(down) ascii('8');if(up) ascii('9');if(fire) ascii('0');
  } else if(mode==Controls::Cursor) {
    if(up||down||left||right) press(0,0);
    if(left) ascii('5');if(down) ascii('6');if(up) ascii('7');if(right) ascii('8');if(fire) ascii('0');
  } else {if(up) ascii('q');if(down) ascii('a');if(left) ascii('o');if(right) ascii('p');if(fire) ascii(' ');}
}
uint8_t Machine::input(uint16_t port) const {
  if(!(port&1)) {
    uint8_t value=0xbf;
    for(unsigned row=0;row<8;++row) if(!(port&(1u<<(8+row)))) value&=uint8_t(rows[row]|0xe0);
    if(!tape || tape->level()) value|=0x40;
    return value;
  }
  if((port&0xff)==0x1f) return joystick;
  return 0xff;
}
void Machine::output(uint16_t port,uint8_t value) {
  if(!(port&1)) {ula=value;border=value&7;}
}
void Machine::step() {
  cpu.int_pending=frameCycle<32;cpu.int_data=0xff;
  const unsigned long before=cpu.cyc;
  z80_step(&cpu);
  const uint32_t cycles=uint32_t(cpu.cyc-before);
  if(tape) tape->advance(cycles);
  samplePhase+=cycles*kSampleRate;
  while(samplePhase>=kClock) {
    samplePhase-=kClock;
    int32_t level=(ula&16)?10000:0;
    if(tapeSound && tape && tape->playing()) level+=tape->level()?3500:-3500;
    // DC blocking keeps the speaker silent when the ULA output is steady.
    audioFilter=level-lastLevel+(audioFilter*255)/256;lastLevel=level;
    if(audio) audio(audioContext,int16_t(audioFilter));
  }
  const unsigned oldLine=frameCycle/224;
  frameCycle+=cycles;
  const unsigned endLine=frameCycle/224;
  if(oldLine<312) borders[oldLine]=border;
  if(endLine<312) borders[endLine]=border;
  if(frameCycle>=kFrameCycles) {frameCycle-=kFrameCycles;++frames;}
}
void Machine::runFrame() {const uint32_t start=frames;while(frames==start) step();}
void setFlags(z80 &c,uint8_t f) {c.sf=f&128;c.zf=f&64;c.yf=f&32;c.hf=f&16;c.xf=f&8;c.pf=f&4;c.nf=f&2;c.cf=f&1;}
uint8_t flags(const z80 &c) {return (c.sf<<7)|(c.zf<<6)|(c.yf<<5)|(c.hf<<4)|(c.xf<<3)|(c.pf<<2)|(c.nf<<1)|c.cf;}
uint8_t pixel(const uint8_t *screen,unsigned x,unsigned y,bool flash) {
  const unsigned addr=((y&0xc0)<<5)|((y&7)<<8)|((y&0x38)<<2)|(x>>3);
  const uint8_t attr=screen[6144+(y>>3)*32+(x>>3)];
  bool ink=screen[addr]&(0x80>>(x&7));if(flash && (attr&128)) ink=!ink;
  return (ink?(attr&7):((attr>>3)&7))|((attr&64)?8:0);
}
uint16_t colour(uint8_t index) {
  const unsigned value=(index&8)?255:205;
  const unsigned r=(index&2)?value:0,g=(index&4)?value:0,b=(index&1)?value:0;
  return uint16_t(((r>>3)<<11)|((g>>2)<<5)|(b>>3));
}
void renderRow(const uint8_t *screen,const uint8_t *borderLines,bool flash,bool showBorder,unsigned y,uint16_t *out) {
  const unsigned width=showBorder?320:256,height=showBorder?240:192;
  const unsigned y0=y*height,y1=(y+1)*height;
  for(unsigned x=0;x<240;++x) {
    const unsigned x0=x*width,x1=(x+1)*width;
    unsigned r=0,g=0,b=0,total=0;
    for(unsigned sy=y0/135;sy<=(y1-1)/135;++sy) {
      const unsigned wy=(y1<(sy+1)*135?y1:(sy+1)*135)-(y0>sy*135?y0:sy*135);
      for(unsigned sx=x0/240;sx<=(x1-1)/240;++sx) {
        const unsigned wx=(x1<(sx+1)*240?x1:(sx+1)*240)-(x0>sx*240?x0:sx*240);
        uint8_t index;
        if(showBorder && (sx<32 || sx>=288 || sy<24 || sy>=216)) index=borderLines[sy+40]&7;
        else index=pixel(screen,sx-(showBorder?32:0),sy-(showBorder?24:0),flash);
        const uint16_t c=colour(index);const unsigned weight=wx*wy;
        r+=((c>>11)&31)*weight;g+=((c>>5)&63)*weight;b+=(c&31)*weight;total+=weight;
      }
    }
    out[x]=uint16_t((((r+total/2)/total)<<11)|(((g+total/2)/total)<<5)|((b+total/2)/total));
  }
}
}
