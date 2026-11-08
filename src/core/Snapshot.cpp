#include "Snapshot.h"
#include <cstring>

namespace spectrum {
namespace {
bool unpack(Reader &r,uint32_t end,uint8_t *out,size_t count) {
  size_t used=0;
  while(used<count && r.position()<end) {
    uint8_t a,b,n,v;
    if(!r.exact(&a,1)) return false;
    if(a!=0xed) {out[used++]=a;continue;}
    if(r.position()==end) {out[used++]=a;continue;}
    if(!r.exact(&b,1)) return false;
    if(b!=0xed) {
      out[used++]=a;
      if(used==count) return false;
      out[used++]=b;continue;
    }
    if(end-r.position()<2 || !r.exact(&n,1) || !r.exact(&v,1) || !n || n>count-used) return false;
    std::memset(out+used,v,n);used+=n;
  }
  return used==count;
}
}
bool loadSnapshot(Machine &m,Reader &r,SnapshotKind kind,uint8_t *work,const char *&error) {
  error="Instantanea invalida o truncada";
  if(!work) {error="Sin memoria para instantanea";return false;}
  if(!r.seek(0)) return false;
  uint8_t h[30]{};
  z80 state=m.cpu;
  state.cyc=0;state.halted=state.int_pending=state.nmi_pending=false;state.iff_delay=0;state.mem_ptr=0;
  uint8_t border=0;
  if(kind==SnapshotKind::Sna) {
    if(r.size()!=49179 || !r.exact(h,27) || h[25]>2) {error="Se requiere SNA de 48K (49179 bytes)";return false;}
    const uint16_t sp=word(h+23);
    if(sp<0x4000 || sp>0xfffe || !r.exact(work,49152)) return false;
    state.i=h[0];state.l_=h[1];state.h_=h[2];state.e_=h[3];state.d_=h[4];
    state.c_=h[5];state.b_=h[6];state.f_=h[7];state.a_=h[8];
    state.l=h[9];state.h=h[10];state.e=h[11];state.d=h[12];state.c=h[13];state.b=h[14];
    state.iy=word(h+15);state.ix=word(h+17);state.iff1=state.iff2=bool(h[19]&4);state.r=h[20];
    setFlags(state,h[21]);state.a=h[22];state.sp=uint16_t(sp+2);state.interrupt_mode=h[25];
    state.pc=word(work+sp-0x4000);border=h[26]&7;
  } else {
    if(r.size()<30 || !r.exact(h,30) || (h[29]&3)>2) return false;
    if(h[12]==255) h[12]=1;
    state.a=h[0];setFlags(state,h[1]);state.c=h[2];state.b=h[3];state.l=h[4];state.h=h[5];
    state.pc=word(h+6);state.sp=word(h+8);state.i=h[10];state.r=(h[11]&127)|((h[12]&1)<<7);
    state.e=h[13];state.d=h[14];state.c_=h[15];state.b_=h[16];state.e_=h[17];state.d_=h[18];
    state.l_=h[19];state.h_=h[20];state.a_=h[21];state.f_=h[22];state.iy=word(h+23);state.ix=word(h+25);
    state.iff1=h[27]!=0;state.iff2=h[28]!=0;state.interrupt_mode=h[29]&3;border=(h[12]>>1)&7;
    if(state.pc) {
      if(h[12]&16) {error="SamRam no implementada";return false;}
      if(h[12]&32) {
        if(!unpack(r,r.size(),work,49152)) return false;
        const uint32_t left=r.size()-r.position();
        if(left) {uint8_t tail[4];if(left!=4 || !r.exact(tail,4) || std::memcmp(tail,"\0\xed\xed\0",4)) return false;}
      } else if(r.size()!=49182 || !r.exact(work,49152)) return false;
    } else {
      uint8_t ext[55]{};
      if(!r.exact(ext,2)) return false;
      const uint16_t length=word(ext);
      if(length!=23 && length!=54 && length!=55) {error="Version Z80 no compatible";return false;}
      if(!r.exact(ext,length)) return false;
      // Hardware 0 is plain 48K in v2 and v3. Reject 128K, SamRam and expansions.
      if(ext[2]!=0 || ext[4] || (ext[5]&0x84) || (length>=54 && (ext[27] || ext[28]))) {
        error="Solo Z80 de Spectrum 48K sin expansiones";return false;
      }
      state.pc=word(ext);unsigned pages=0;
      while(r.position()<r.size()) {
        uint8_t p[3];if(!r.exact(p,3)) return false;
        const uint16_t length=word(p);
        unsigned offset,mask;
        switch(p[2]) {case 8:offset=0;mask=1;break;case 4:offset=16384;mask=2;break;case 5:offset=32768;mask=4;break;
          default:error="Pagina Z80 no compatible";return false;}
        if(pages&mask) return false;
        pages|=mask;
        if(length==0xffff) {if(!r.exact(work+offset,16384)) return false;}
        else {
          if(!length || length>r.size()-r.position()) return false;
          const uint32_t end=r.position()+length;
          if(!unpack(r,end,work+offset,16384) || r.position()!=end) return false;
        }
      }
      if(pages!=7) return false;
    }
  }
  m.reset(false);m.cpu=state;std::memcpy(m.memory+0x4000,work,49152);
  m.border=border;std::memset(m.borders,border,sizeof(m.borders));
  error=nullptr;return true;
}
}
