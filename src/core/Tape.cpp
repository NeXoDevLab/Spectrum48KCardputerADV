#include "Tape.h"
#include <cstring>

namespace spectrum {
bool Tape::fail(const char *message) {error_=message;playing_=false;remaining_=0;return false;}
bool Tape::get(uint8_t *p,size_t n) {return reader_->exact(p,n) || fail("Cinta truncada / error SD");}
bool Tape::skip(uint32_t n) {return reader_->skip(n) || fail("Longitud de bloque invalida");}
void Tape::eject() {*this=Tape{};}
bool Tape::mount(Reader &reader,bool tzx) {
  eject();reader_=&reader;tzx_=tzx;
  if(!reader.seek(0)) return fail("No se puede leer la cinta");
  if(tzx) {
    uint8_t h[10];
    if(!get(h,10)) return false;
    if(std::memcmp(h,"ZXTape!\x1a",8) || h[8]!=1) return fail("Cabecera TZX no compatible");
  }
  if(reader.position()==reader.size()) return fail("Cinta vacia");
  playing_=true;
  next();
  return !error_;
}
bool Tape::data(uint32_t length,uint8_t bits,uint16_t pause,Phase phase) {
  if(!length || bits<1 || bits>8 || length>reader_->size()-reader_->position()) return fail("Datos de cinta invalidos");
  bytes_=length;lastBits_=bits;pauseMs_=pause;bit_=half_=0;phase_=phase;
  return true;
}
bool Tape::block() {
  uint8_t h[20]{};
  if(reader_->position()==reader_->size()) {playing_=false;level_=false;return false;}
  if(!tzx_) {
    if(!get(h,2)) return false;
    pilot_=2168;sync1_=667;sync2_=735;zero_=855;one_=1710;
    if(!data(word(h),8,1000,Phase::Pilot)) return false;
    const auto at=reader_->position();
    if(!get(h,1) || !reader_->seek(at)) return fail("No se puede leer la cinta");
    count_=h[0]<128?8063:3223;return true;
  }
  if(!get(h,1)) return false;
  switch(h[0]) {
    case 0x10: {
      if(!get(h,4)) return false;
      pilot_=2168;sync1_=667;sync2_=735;zero_=855;one_=1710;
      if(!data(word(h+2),8,word(h),Phase::Pilot)) return false;
      const auto at=reader_->position();
      if(!get(h,1) || !reader_->seek(at)) return fail("No se puede leer la cinta");
      count_=h[0]<128?8063:3223;return true;
    }
    case 0x11:
      if(!get(h,18)) return false;
      pilot_=word(h);sync1_=word(h+2);sync2_=word(h+4);zero_=word(h+6);one_=word(h+8);count_=word(h+10);
      if((count_ && !pilot_) || !sync1_ || !sync2_ || !zero_ || !one_) return fail("Pulso TZX nulo");
      return data(triple(h+15),h[12],word(h+13),Phase::Pilot);
    case 0x12:
      if(!get(h,4)) return false;
      pilot_=word(h);count_=word(h+2);phase_=Phase::Tone;
      return pilot_ && count_ ? true:fail("Tono TZX invalido");
    case 0x13:
      if(!get(h,1)) return false;
      count_=h[0];phase_=Phase::Pulses;return true;
    case 0x14:
      if(!get(h,10)) return false;
      zero_=word(h);one_=word(h+2);
      if(!zero_ || !one_) return fail("Pulso TZX nulo");
      return data(triple(h+7),h[4],word(h+5),Phase::Data);
    case 0x15:
      if(!get(h,8)) return false;
      zero_=word(h);if(!zero_) return fail("Muestreo TZX nulo");
      return data(triple(h+5),h[4],word(h+2),Phase::Direct);
    case 0x20:
      if(!get(h,2)) return false;
      pauseMs_=word(h);phase_=Phase::Pause;
      if(!pauseMs_) {phase_=Phase::Block;playing_=false;return false;}
      return true;
    case 0x21:case 0x30:
      if(!get(h,1)) return false;return skip(h[0]);
    case 0x22:return true;
    case 0x24:
      if(!get(h,2)) return false;
      if(depth_==4 || !word(h)) return fail("Bucle TZX invalido");
      loops_[depth_++]={reader_->position(),word(h)};return true;
    case 0x25:
      if(!depth_) return fail("Fin de bucle sin inicio");
      if(--loops_[depth_-1].count) return reader_->seek(loops_[depth_-1].offset) || fail("Error SD en bucle");
      --depth_;return true;
    case 0x2a:
      if(!get(h,4) || !skip(dword(h))) return false;
      playing_=false;return false;
    case 0x2b:
      if(!get(h,5)) return false;
      if(dword(h)!=1 || h[4]>1) return fail("Nivel TZX invalido");
      level_=h[4];return true;
    case 0x31:
      if(!get(h,2)) return false;return skip(h[1]);
    case 0x32:
      if(!get(h,2)) return false;return skip(word(h));
    case 0x33:
      if(!get(h,1)) return false;return skip(3u*h[0]);
    case 0x35:
      if(!get(h,20)) return false;return skip(dword(h+16));
    case 0x5a:return skip(9);
    default:return fail("Bloque TZX no implementado");
  }
}
bool Tape::next() {
  // Bound metadata-only loops to avoid freezing the hardware on malformed files.
  for(unsigned budget=0;budget<4096 && playing_;++budget) {
    toggle_=true;
    switch(phase_) {
      case Phase::Block:if(!block()) return false;break;
      case Phase::Pilot:
        if(count_) {--count_;remaining_=pilot_;return true;}
        phase_=Phase::Sync1;break;
      case Phase::Sync1:phase_=Phase::Sync2;remaining_=sync1_;return true;
      case Phase::Sync2:phase_=Phase::Data;remaining_=sync2_;return true;
      case Phase::Data:case Phase::Direct: {
        const bool direct=phase_==Phase::Direct;
        if(!bit_ && !half_) {
          if(!bytes_) {phase_=Phase::Pause;break;}
          if(!get(&current_,1)) return false;
          --bytes_;
        }
        const bool high=current_&(0x80>>bit_);
        remaining_=direct?zero_:(high?one_:zero_);
        if(direct) {level_=high;toggle_=false;}
        if(direct || ++half_==2) {
          half_=0;
          if(++bit_==(bytes_?8:lastBits_)) bit_=0;
        }
        return true;
      }
      case Phase::Pause:
        phase_=Phase::Block;
        // Hold low through the inter-block pause. Zero means concatenate.
        if(pauseMs_) {level_=false;toggle_=false;remaining_=pauseMs_*3500u;return true;}
        break;
      case Phase::Tone:
        if(count_) {--count_;remaining_=pilot_;return true;}
        phase_=Phase::Block;break;
      case Phase::Pulses: {
        if(!count_) {phase_=Phase::Block;break;}
        uint8_t h[2];if(!get(h,2)) return false;
        --count_;remaining_=word(h);if(!remaining_) return fail("Pulso TZX nulo");return true;
      }
    }
  }
  return fail("Demasiados metadatos / bucle TZX");
}
void Tape::advance(uint32_t cycles) {
  if(!playing_) return;
  if(!remaining_ && !next()) return;
  while(playing_ && cycles>=remaining_) {
    cycles-=remaining_;remaining_=0;
    if(toggle_) level_=!level_;
    if(!next()) return;
  }
  if(playing_) remaining_-=cycles;
}
}
