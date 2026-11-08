#pragma once
#include <cstdint>
extern "C" {
#include "../vendor/z80.h"
}

namespace spectrum {
class Tape;
constexpr uint32_t kClock=3500000, kFrameCycles=69888, kSampleRate=16000;
enum class Controls { Kempston, Sinclair, Cursor, Qaop };
struct Machine {
  // Fixed memory; no PSRAM and no late allocation for the emulated address space.
  uint8_t memory[65536]{};
  z80 cpu{};
  uint8_t rows[8]{}, joystick=0, border=7, ula=0;
  uint8_t borders[312]{};
  uint32_t frameCycle=0, frames=0, samplePhase=0;
  int32_t audioFilter=0, lastLevel=0;
  Tape *tape=nullptr;
  bool tapeSound=true;
  using AudioSink=void(*)(void*,int16_t);
  AudioSink audio=nullptr;
  void *audioContext=nullptr;

  Machine();
  void reset(bool clearRam=true);
  void releaseKeys();
  void press(uint8_t row,uint8_t bit);
  bool ascii(char ch);
  void direction(Controls mode,bool up,bool down,bool left,bool right,bool fire);
  uint8_t input(uint16_t port) const;
  void output(uint16_t port,uint8_t value);
  void step();
  void runFrame();
};
void setFlags(z80 &cpu,uint8_t value);
uint8_t flags(const z80 &cpu);
// Screen memory contains 6144 bitmap bytes followed by 768 attribute bytes.
uint8_t pixel(const uint8_t *screen,unsigned x,unsigned y,bool flash);
uint16_t colour(uint8_t index);
// Area filtered downscale: full active screen, or 320x240 with the border.
void renderRow(const uint8_t *screen,const uint8_t *borders,bool flash,
               bool showBorder,unsigned y,uint16_t *out);
}
