#if defined(__GNUC__) && defined(ARDUINO_ARCH_ESP32)
// Keep the timing/rendering hot paths optimized for speed in Arduino IDE too.
#pragma GCC optimize("O3")
#endif
#include "Spectrum.h"
#include "Tape.h"
#include <array>
#include <algorithm>
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
  nextScanline=224;scanline=0;
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
  if(!(port&1)) {ula=value;border=value&7;borders[scanline]=border;}
}
void Machine::step() {
  cpu.int_pending=frameCycle<32;cpu.int_data=0xff;
  const unsigned long before=cpu.cyc;
  z80_step(&cpu);
  const uint32_t cycles=uint32_t(cpu.cyc-before);
  if(tape && tape->playing()) tape->advance(cycles);
  if(audioEnabled && audio) {
    // Reduce 3500000/16000 to 875/4: exact sample count without a large multiply.
    samplePhase+=cycles*4;
    while(samplePhase>=875) {
      samplePhase-=875;
      int32_t level=(ula&16)?10000:0;
      if(tapeSound && tape && tape->playing()) level+=tape->level()?3500:-3500;
      audioFilter=level-lastLevel+(audioFilter*255)/256;lastLevel=level;
      audio(audioContext,int16_t(audioFilter));
    }
  }
  frameCycle+=cycles;
  // Update at line boundaries instead of dividing and writing twice per opcode.
  while(frameCycle>=nextScanline) {
    if(++scanline==312) {frameCycle-=kFrameCycles;scanline=0;nextScanline=224;++frames;}
    else nextScanline+=224;
    borders[scanline]=border;
  }
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
namespace {
struct Axis {uint16_t first=0;uint8_t count=0;uint8_t weights[3]{};};
template<unsigned Count,unsigned Numerator,unsigned Denominator>
constexpr std::array<Axis,Count> makeAxis() {
  std::array<Axis,Count> table{};
  for(unsigned i=0;i<Count;++i) {
    const unsigned start=i*Numerator,end=(i+1)*Numerator;
    auto &a=table[i];a.first=start/Denominator;
    a.count=uint8_t((end-1)/Denominator-a.first+1);
    for(unsigned tap=0;tap<a.count;++tap) {
      const unsigned p=a.first+tap;
      a.weights[tap]=uint8_t(std::min(end,(p+1)*Denominator)-std::max(start,p*Denominator));
    }
  }
  return table;
}
constexpr auto activeX=makeAxis<240,16,15>(),borderX=makeAxis<240,4,3>();
constexpr auto activeY=makeAxis<135,64,45>(),borderY=makeAxis<135,16,9>();
constexpr std::array<uint16_t,16> makePalette() {
  std::array<uint16_t,16> palette{};
  for(unsigned i=0;i<16;++i) {
    const unsigned v=(i&8)?255:205;
    palette[i]=uint16_t(((i&2)?((v>>3)<<11):0)|((i&4)?((v>>2)<<5):0)|((i&1)?(v>>3):0));
  }
  return palette;
}
constexpr auto palette=makePalette();
template<bool Border>
void filteredRow(const uint8_t *screen,const uint8_t *borderLines,bool flash,unsigned y,uint16_t *out) {
  const auto &ay=Border?borderY[y]:activeY[y];
  struct Row {const uint8_t *bitmap=nullptr,*attributes=nullptr;uint8_t border=0,weight=0;};
  Row source[3];
  for(unsigned tap=0;tap<ay.count;++tap) {
    unsigned sy=ay.first+tap;auto &row=source[tap];row.weight=ay.weights[tap];
    if constexpr(Border) {row.border=borderLines[sy+40]&7;if(sy<24 || sy>=216) continue;sy-=24;}
    row.bitmap=screen+((sy&0xc0)<<5)+((sy&7)<<8)+((sy&0x38)<<2);
    row.attributes=screen+6144+(sy>>3)*32;
  }
  // Reduced ratios make the total area exactly 1024 or 64: shifts replace
  // per-pixel divisions while preserving every RGB565 rounding decision.
  constexpr unsigned shift=Border?6:10,rounding=1u<<(shift-1);
  for(unsigned x=0;x<240;++x) {
    const auto &ax=Border?borderX[x]:activeX[x];unsigned r=0,g=0,b=0;
    for(unsigned ty=0;ty<ay.count;++ty) for(unsigned tx=0;tx<ax.count;++tx) {
      unsigned sx=ax.first+tx;const auto &row=source[ty];uint8_t index=row.border;
      bool active=row.bitmap!=nullptr;
      if constexpr(Border) {active=active && sx>=32 && sx<288;sx-=32;}
      if(active) {
        const auto attr=row.attributes[sx>>3];bool ink=row.bitmap[sx>>3]&(0x80>>(sx&7));
        if(flash && (attr&128)) ink=!ink;
        index=(ink?(attr&7):((attr>>3)&7))|((attr&64)?8:0);
      }
      const unsigned c=palette[index],weight=ax.weights[tx]*row.weight;
      r+=((c>>11)&31)*weight;g+=((c>>5)&63)*weight;b+=(c&31)*weight;
    }
    out[x]=uint16_t((((r+rounding)>>shift)<<11)|(((g+rounding)>>shift)<<5)|((b+rounding)>>shift));
  }
}
}
void DisplayScaler::begin(const uint8_t *screen,const uint8_t *borders,bool flash,bool border) {
  screen_=screen;borders_=borders;flash_=flash;border_=border;
  for(auto &row:rows_) row.source=-1;
}
DisplayScaler::FilteredRow &DisplayScaler::horizontal(unsigned y) {
  auto &out=rows_[y%3];if(out.source==int(y)) return out;out.source=y;
  const unsigned left=border_?32:0,numerator=border_?4:16;
  unsigned sy=y;
  if(border_) {
    const auto c=palette[borders_[y+40]&7];
    const uint32_t rb=((uint32_t(c)&0xf800)<<5)|(c&31);const uint16_t green=(c>>5)&63;
    if(y<24 || y>=216) {
      std::fill(out.rb,out.rb+240,rb*numerator);std::fill(out.green,out.green+240,green*numerator);return out;
    }
    sy-=24;
    std::fill(decodedRb_,decodedRb_+32,rb);std::fill(decodedRb_+288,decodedRb_+320,rb);
    std::fill(decodedGreen_,decodedGreen_+32,green);std::fill(decodedGreen_+288,decodedGreen_+320,green);
  }
  const auto *bitmap=screen_+((sy&0xc0)<<5)+((sy&7)<<8)+((sy&0x38)<<2);
  const auto *attrs=screen_+6144+(sy>>3)*32;
  // Decode each attribute once for its eight pixels, then reuse these colours
  // for horizontal taps and for adjacent output rows.
  for(unsigned byte=0;byte<32;++byte) {
    const auto attr=attrs[byte];const unsigned bright=(attr&64)?8:0;
    uint16_t ink=palette[(attr&7)|bright],paper=palette[((attr>>3)&7)|bright];
    if(flash_ && (attr&128)) std::swap(ink,paper);
    const uint32_t inkRb=((uint32_t(ink)&0xf800)<<5)|(ink&31),paperRb=((uint32_t(paper)&0xf800)<<5)|(paper&31);
    const uint16_t inkG=(ink>>5)&63,paperG=(paper>>5)&63;const auto bits=bitmap[byte];
    for(unsigned bit=0;bit<8;++bit) {
      const unsigned x=left+byte*8+bit;const bool set=bits&(0x80>>bit);
      decodedRb_[x]=set?inkRb:paperRb;decodedGreen_[x]=set?inkG:paperG;
    }
  }
  for(unsigned x=0;x<240;++x) {
    const auto &axis=border_?borderX[x]:activeX[x];uint32_t rb=0,green=0;
    for(unsigned tap=0;tap<axis.count;++tap) {
      rb+=decodedRb_[axis.first+tap]*axis.weights[tap];green+=decodedGreen_[axis.first+tap]*axis.weights[tap];
    }
    out.rb[x]=rb;out.green[x]=green;
  }
  return out;
}
void DisplayScaler::row(unsigned y,uint16_t *out) {
  const auto &axis=border_?borderY[y]:activeY[y];const FilteredRow *source[3];
  for(unsigned tap=0;tap<axis.count;++tap) source[tap]=&horizontal(axis.first+tap);
  const unsigned shift=border_?6:10,rounding=1u<<(shift-1);
  for(unsigned x=0;x<240;++x) {
    uint32_t rb=0,green=0;
    for(unsigned tap=0;tap<axis.count;++tap) {rb+=source[tap]->rb[x]*axis.weights[tap];green+=source[tap]->green[x]*axis.weights[tap];}
    rb+=(rounding<<16)|rounding;
    out[x]=uint16_t((((rb>>(16+shift))&31)<<11)|(((green+rounding)>>shift)<<5)|((rb>>shift)&31));
  }
}
void renderRow(const uint8_t *screen,const uint8_t *borderLines,bool flash,bool showBorder,unsigned y,uint16_t *out) {
  if(showBorder) filteredRow<true>(screen,borderLines,flash,y,out);
  else filteredRow<false>(screen,borderLines,flash,y,out);
}
}
