#pragma once
#include "Reader.h"

namespace spectrum {
// Pulse stream, clocked in Z80 T-states. File data stays on the SD card.
class Tape {
public:
  bool mount(Reader &reader,bool tzx);
  void eject();
  void advance(uint32_t cycles);
  void pause() {playing_=false;}
  void resume() {if(reader_ && !error_) playing_=true;}
  bool playing() const {return playing_;}
  bool level() const {return level_;}
  const char *error() const {return error_;}
  uint32_t position() const {return reader_?reader_->position():0;}
private:
  enum class Phase { Block,Pilot,Sync1,Sync2,Data,Pause,Tone,Pulses,Direct };
  Reader *reader_=nullptr;
  Phase phase_=Phase::Block;
  bool tzx_=false,playing_=false,level_=false,toggle_=false;
  const char *error_=nullptr;
  uint32_t remaining_=0,bytes_=0,count_=0,pauseMs_=0;
  uint16_t pilot_=2168,sync1_=667,sync2_=735,zero_=855,one_=1710;
  uint8_t lastBits_=8,current_=0,bit_=0,half_=0;
  struct Loop {uint32_t offset;uint16_t count;} loops_[4]{};
  unsigned depth_=0;
  bool fail(const char *message);
  bool get(uint8_t *p,size_t n);
  bool skip(uint32_t n);
  bool next();
  bool block();
  bool data(uint32_t length,uint8_t bits,uint16_t pause,Phase phase);
};
}
