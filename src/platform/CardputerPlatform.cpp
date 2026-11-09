#include "CardputerPlatform.h"
#include "GameFiles.h"
#include "AudioQueue.h"
#include "../core/Spectrum.h"
#include "../core/Snapshot.h"
#include "../core/Tape.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <esp_ota_ops.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <new>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

namespace {
using namespace spectrum;
constexpr const char *kRoot=spectrum_files::kRoot;
Machine machine;
Tape tape;
bool ready=false,fast=false,border=false,game=false,tapeSound=true;
Controls controls=Controls::Kempston;
uint8_t volume=77;
uint32_t deadline=0,volumeAt=0,noticeUntil=0,presentAt=0;
uint32_t cpuUs=0,cpuMaxUs=0,inputUs=0,inputMaxUs=0,sdReads=0,sdMaxUs=0,audioPaceTimeouts=0;
String notice;
unsigned autoTicks=0;
bool autoLoad=false,lastTab=false,shortcuts[6]{};
String folder=kRoot;
struct SdStorage final : spectrum_files::Storage {
  bool scan(const char *path,spectrum_files::Visitor visitor,void *context) override {
    File dir=SD.open(path);if(!dir || !dir.isDirectory()) return false;
    for(;;) {
      File item=dir.openNextFile();if(!item) break;
      const String name=item.name();const bool directory=item.isDirectory();
      item.close();visitor(context,name.c_str(),directory);
    }
    dir.close();return true;
  }
  bool exists(const char *path) override {return SD.exists(path);}
  bool directory(const char *path) override {File file=SD.open(path);return file && file.isDirectory();}
  bool mkdir(const char *path) override {return SD.mkdir(path);}
  bool move(const char *from,const char *to) override {return !SD.exists(to) && SD.rename(from,to);}
} storage;

// Buffered SD access avoids one filesystem call for every tape byte.
class SdReader final : public Reader {
  File file_;
  uint8_t buffer_[2048]{};
  uint32_t at_=0,length_=0,start_=0;
  size_t buffered_=0;
public:
  bool open(const String &path) {
    close();file_=SD.open(path,FILE_READ);
    if(!file_ || file_.isDirectory()) {close();return false;}
    length_=file_.size();return true;
  }
  void close() {file_.close();at_=length_=start_=0;buffered_=0;}
  size_t read(uint8_t *out,size_t count) override {
    size_t copied=0;
    while(copied<count && at_<length_) {
      if(at_<start_ || at_>=start_+buffered_) {
        const uint32_t started=micros();
        if(file_.position()!=at_ && !file_.seek(at_)) break;
        start_=at_;buffered_=file_.read(buffer_,sizeof(buffer_));
        ++sdReads;sdMaxUs=std::max(sdMaxUs,uint32_t(micros()-started));
        if(!buffered_) break;
      }
      const size_t n=std::min(count-copied,size_t(start_+buffered_-at_));
      memcpy(out+copied,buffer_+at_-start_,n);copied+=n;at_+=n;
    }
    return copied;
  }
  bool seek(uint32_t n) override {if(n>length_) return false;at_=n;return true;}
  uint32_t position() const override {return at_;}
  uint32_t size() const override {return length_;}
} tapeFile;

uint8_t picture[6912]{},borderPicture[312]{};
bool pictureFlash=false,pictureBorder=false;
String pictureNotice;
std::atomic<bool> lcdBusy{false};
TaskHandle_t lcdTaskHandle=nullptr;
SemaphoreHandle_t displayMutex=nullptr;
spectrum_audio::Queue audioQueue;
constexpr unsigned kAudioPrefill=4,kAudioTarget=640;
TaskHandle_t audioTaskHandle=nullptr;
SemaphoreHandle_t audioMutex=nullptr;
std::atomic<bool> audioActive{false};
std::atomic<uint32_t> audioPackets{0},audioUnderruns{0},audioErrors{0},audioMaxGapUs{0};
std::atomic<uint32_t> lcdFrames{0},lcdFilterUs{0},lcdPushUs{0};
// Only the playback worker writes these; stop/reset also holds audioMutex.
int16_t speakerPackets[3][spectrum_audio::Queue::kSamples]{};
unsigned speakerPacket=0;
bool audioStarted=false;
uint32_t audioLastSubmit=0,audioEmptySince=0;

const char *controlName() {
  switch(controls) {case Controls::Kempston:return "Kempston";case Controls::Sinclair:return "Sinclair 1";
    case Controls::Cursor:return "Cursor";default:return "QAOP+espacio";}
}
void notify(const String &text) {notice=text;noticeUntil=millis()+2000;Serial.println(text);}
void silence() {
  audioActive.store(false,std::memory_order_release);
  if(audioMutex) xSemaphoreTake(audioMutex,portMAX_DELAY);
  M5.Speaker.stop();
  const uint32_t start=millis();
  while(M5.Speaker.isPlaying(0) && millis()-start<100) delay(1);
  if(M5.Speaker.isPlaying(0)) M5.Speaker.end();
  audioQueue.clear();speakerPacket=0;audioStarted=false;audioLastSubmit=audioEmptySince=0;
  if(audioMutex) xSemaphoreGive(audioMutex);
}
void audioSink(void*,int16_t sample) {
  audioQueue.push(sample);
}
void audioWorker(void*) {
  for(;;) {
    if(!audioActive.load(std::memory_order_acquire)) {ulTaskNotifyTake(pdTRUE,portMAX_DELAY);continue;}
    xSemaphoreTake(audioMutex,portMAX_DELAY);
    if(audioActive.load(std::memory_order_acquire)) {
      if(!audioStarted && audioQueue.packets()>=kAudioPrefill) audioStarted=true;
      if(audioStarted && M5.Speaker.isPlaying(0)<2) {
        if(audioQueue.pop(speakerPackets[speakerPacket])) {
          const bool ok=M5.Speaker.playRaw(speakerPackets[speakerPacket],spectrum_audio::Queue::kSamples,kSampleRate,false,1,0,false);
          const uint32_t now=micros();audioEmptySince=0;
          if(ok) {
            if(audioLastSubmit) audioMaxGapUs.store(std::max(audioMaxGapUs.load(),uint32_t(now-audioLastSubmit)));
            audioLastSubmit=now;++audioPackets;speakerPacket=(speakerPacket+1)%3;
          } else {++audioErrors;audioStarted=false;}
        } else if(!M5.Speaker.isPlaying(0)) {
          // Do not mistake the DMA prefetch window for a starved stream.
          const uint32_t now=micros();if(!audioEmptySince) audioEmptySince=now;
          if(uint32_t(now-audioEmptySince)>20000) {++audioUnderruns;audioStarted=false;audioEmptySince=0;audioLastSubmit=0;}
        }
      }
    }
    xSemaphoreGive(audioMutex);vTaskDelay(1);
  }
}
void paceAudio() {
  const uint32_t waitAt=millis();
  while(audioQueue.buffered()>=kAudioTarget && audioActive.load()) {
    delay(1);
    if(uint32_t(millis()-waitAt)>120) {++audioPaceTimeouts;silence();break;}
  }
}
void drawPicture() {
  xSemaphoreTake(displayMutex,portMAX_DELAY);
  static uint16_t tile[240*4];
  static DisplayScaler scaler;
  scaler.begin(picture,borderPicture,pictureFlash,pictureBorder);
  uint32_t filterUs=0,pushUs=0;
  for(unsigned y=0;y<135;y+=4) {
    const unsigned rows=std::min(4u,135-y);
    uint32_t started=micros();
    for(unsigned row=0;row<rows;++row)
      scaler.row(y+row,tile+240*row);
    filterUs+=micros()-started;started=micros();
    M5.Display.pushImage(0,y,240,rows,tile);
    pushUs+=micros()-started;
  }
  lcdFilterUs=filterUs;lcdPushUs=pushUs;++lcdFrames;
  if(pictureNotice.length()) {
    M5.Display.fillRect(0,122,240,13,TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);M5.Display.setTextSize(1);
    M5.Display.drawString(pictureNotice,3,124);
  }
  xSemaphoreGive(displayMutex);
}
void lcdWorker(void*) {
  for(;;) {
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    drawPicture();lcdBusy.store(false,std::memory_order_release);
  }
}
void present() {
  if(lcdBusy.load(std::memory_order_acquire)) return;
  const uint32_t now=micros();if(uint32_t(now-presentAt)<(fast?100000u:35000u)) return;
  if(!fast && audioActive.load() && audioQueue.buffered()<320) return;
  memcpy(picture,machine.memory+0x4000,sizeof(picture));memcpy(borderPicture,machine.borders,sizeof(borderPicture));
  pictureFlash=(machine.frames/16)&1;pictureBorder=border || tape.playing();
  pictureNotice=millis()<noticeUntil?notice:String();presentAt=now;
  if(lcdTaskHandle) {lcdBusy.store(true,std::memory_order_release);xTaskNotifyGive(lcdTaskHandle);}
  else drawPicture();
}
void waitDisplay() {while(lcdBusy.load(std::memory_order_acquire)) delay(1);}
void rainbow() {
  const uint16_t colours[6]={TFT_RED,TFT_YELLOW,TFT_GREEN,TFT_CYAN,TFT_BLUE,TFT_MAGENTA};
  for(unsigned i=0;i<6;++i) M5.Display.fillRect(180+10*i,0,10,4,colours[i]);
}
void message(const String &title,const String &detail) {
  waitDisplay();M5.Display.fillScreen(TFT_BLACK);rainbow();
  M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);M5.Display.setTextSize(1);
  M5.Display.drawString(title,8,16,2);M5.Display.drawString(detail,8,49);
  Serial.printf("ZX %s: %s\n",title.c_str(),detail.c_str());
}
bool held(char key) {
  return M5Cardputer.Keyboard.isKeyPressed(key) ||
    (key>='a' && key<='z' && M5Cardputer.Keyboard.isKeyPressed(char(key-'a'+'A')));
}
void resetBasic() {
  tape.eject();tapeFile.close();autoLoad=false;game=false;machine.reset();silence();deadline=micros();
}
bool loadFile(const String &path) {
  String lower=path;lower.toLowerCase();
  if(lower.endsWith(".sna") || lower.endsWith(".z80")) {
    SdReader input;
    std::unique_ptr<uint8_t[]> work(new(std::nothrow) uint8_t[49152]);
    const char *error=nullptr;
    if(!input.open(path)) {notify("No se puede abrir el archivo");return false;}
    if(!loadSnapshot(machine,input,lower.endsWith(".sna")?SnapshotKind::Sna:SnapshotKind::Z80,work.get(),error)) {
      notify(error?error:"Error de instantanea");return false;
    }
    tape.eject();tapeFile.close();autoLoad=false;game=true;
  } else {
    tape.eject();autoLoad=false;
    if(!tapeFile.open(path) || !tape.mount(tapeFile,lower.endsWith(".tzx"))) {
      notify(tape.error()?tape.error():"No se puede abrir la cinta");return false;
    }
    tape.pause();machine.reset();autoLoad=true;autoTicks=0;game=true;
  }
  silence();notify("Cargado: "+path.substring(path.lastIndexOf('/')+1));return true;
}
struct Entry {String name;bool directory;};
std::vector<Entry> scan(bool &more) {
  const auto files=spectrum_files::list(storage,folder.c_str());
  std::vector<Entry> list;list.reserve(files.entries.size()+1);more=files.more;
  if(!files.ok) {notify("Carpeta no disponible");return list;}
  if(folder!=kRoot) list.insert(list.begin(),{"[..] Volver",true});
  for(const auto &entry:files.entries) list.push_back({entry.name.c_str(),entry.directory});
  return list;
}
String clipped(String text) {
  while(text.length() && M5.Display.textWidth(text)>220) {
    unsigned end=text.length()-1;while(end && (uint8_t(text[end])&0xc0)==0x80) --end;
    text.remove(end);
  }
  return text;
}
void drawMenu(const std::vector<Entry> &list,size_t selected,bool more) {
  M5.Display.fillScreen(TFT_BLACK);rainbow();M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);
  M5.Display.drawString("ZX Spectrum 48K",6,5,2);
  M5.Display.drawString(clipped(folder),6,24);
  const size_t start=selected/6*6;
  for(size_t i=start;i<list.size() && i<start+6;++i) {
    const int y=38+12*(i-start);
    M5.Display.fillRect(3,y-1,234,12,i==selected?TFT_NAVY:TFT_BLACK);
    M5.Display.setTextColor(i==selected?TFT_YELLOW:TFT_WHITE,i==selected?TFT_NAVY:TFT_BLACK);
    M5.Display.drawString(clipped((list[i].directory?"/ ":"  ")+list[i].name),5,y);
  }
  if(list.empty()) {M5.Display.setTextColor(TFT_YELLOW,TFT_BLACK);M5.Display.drawString("Copia TAP/TZX/SNA/Z80 en SD",6,48);}
  M5.Display.setTextColor(TFT_LIGHTGREY,TFT_BLACK);
  M5.Display.drawString(String(fast?"R:rapido ":"O:original ")+(tapeSound?"S:sonido ":"S:mudo ")+"Vol:"+String(unsigned(volume)*100/255)+"%",5,113);
  M5.Display.drawString(more?"128 entradas; usa subcarpetas":"Enter:cargar  Tab:salir  O/R/S",5,125);
}
void menu() {
  silence();waitDisplay();machine.releaseKeys();
  bool more=false;auto list=scan(more);size_t selected=0;
  drawMenu(list,selected,more);
  bool previousEnter=true,previousTab=true,previousBack=true;
  int previousMove=0;uint32_t moveAt=0;
  while(true) {
    M5Cardputer.update();const auto &k=M5Cardputer.Keyboard.keysState();
    if((k.tab && !previousTab) || M5.BtnA.wasPressed()) break;
    previousTab=k.tab;
    const int movement=(k.up||held(';'))?-1:(k.down||held('.'))?1:(k.left||held(','))?-6:(k.right||held('/'))?6:0;
    if(movement && (movement!=previousMove || millis()-moveAt>180)) {
      selected=spectrum_files::navigate(selected,list.size(),movement==-1||movement==1?movement:0,movement==-6?-1:movement==6?1:0);
      moveAt=millis();drawMenu(list,selected,more);
    }
    previousMove=movement;
    bool redraw=false;
    if(!k.fn && held('o') && fast) {fast=false;redraw=true;}
    if(!k.fn && held('r') && !fast) {fast=true;redraw=true;}
    static bool lastSound=false;const bool sound=!k.fn && held('s');
    if(sound && !lastSound) {tapeSound=!tapeSound;machine.tapeSound=tapeSound;redraw=true;}lastSound=sound;
    if(k.fn && millis()-volumeAt>140) {
      const int delta=(held('=')||held('+'))?13:held('-')?-13:0;
      if(delta) {volume=uint8_t(std::max(0,std::min(255,int(volume)+delta)));M5.Speaker.setVolume(volume);volumeAt=millis();redraw=true;}
    }
    const bool back=k.backspace||k.del;
    if(back && !previousBack && folder!=kRoot) {
      folder=spectrum_files::parent(folder.c_str()).c_str();list=scan(more);selected=0;redraw=true;
    }
    previousBack=back;
    if(k.enter && !previousEnter && !list.empty()) {
      if(list[selected].directory) {
        if(list[selected].name=="[..] Volver") folder=spectrum_files::parent(folder.c_str()).c_str();
        else folder=spectrum_files::child(folder.c_str(),list[selected].name.c_str()).c_str();
        list=scan(more);selected=0;redraw=true;
      } else {
        message("Leyendo archivo...",clipped(list[selected].name));
        if(loadFile(folder+"/"+list[selected].name)) break;
        message("No se ha cargado",notice);delay(1800);redraw=true;
      }
    }
    previousEnter=k.enter;
    if(redraw) drawMenu(list,selected,more);
    delay(5);
  }
  machine.releaseKeys();lastTab=true;deadline=micros();presentAt=0;
}
bool shortcut(unsigned index,char key,bool fn) {
  const bool down=fn && held(key);const bool edge=down && !shortcuts[index];shortcuts[index]=down;return edge;
}
void keyboard() {
  M5Cardputer.update();const auto &k=M5Cardputer.Keyboard.keysState();
  const bool open=k.tab && !lastTab;lastTab=k.tab;
  if(open || M5.BtnA.wasPressed()) {menu();return;}
  if(shortcut(0,'f',k.fn)) {fast=!fast;silence();notify(fast?"Modo rapido (sin audio)":"Velocidad original");}
  if(shortcut(1,'b',k.fn)) {border=!border;notify(border?"Borde: ON":"Borde: automatico");}
  if(shortcut(2,'j',k.fn)) {game=!game;notify(game?String("Juego: ")+controlName():"Teclado BASIC");}
  if(shortcut(3,'k',k.fn)) {controls=Controls((unsigned(controls)+1)%4);notify(controlName());}
  if(shortcut(4,'r',k.fn)) {resetBasic();notify("Reiniciado en BASIC");}
  if(shortcut(5,'t',k.fn)) {if(tape.playing()) tape.pause();else tape.resume();notify(tape.playing()?"Cinta: PLAY":"Cinta: PAUSA");}
  if(k.fn && millis()-volumeAt>140) {
    const int delta=(held('=')||held('+'))?13:held('-')?-13:0;
    if(delta) {volume=uint8_t(std::max(0,std::min(255,int(volume)+delta)));M5.Speaker.setVolume(volume);volumeAt=millis();notify(String("Volumen ")+String(unsigned(volume)*100/255)+"%");}
  }
  machine.releaseKeys();
  if(autoLoad) {
    // Boot ROM, then press the Spectrum LOAD token (J), two quotes and Enter.
    if(autoTicks>=250 && autoTicks<290) {
      const char keys[4]={'j','"','"','\n'};
      if(autoTicks%10<5) machine.ascii(keys[(autoTicks-250)/10]);
    }
    if(++autoTicks>=300) {autoLoad=false;tape.resume();}
    return;
  }
  if(!game && (k.up || k.down || k.left || k.right))
    machine.direction(Controls::Cursor,k.up,k.down,k.left,k.right,false);
  if(k.fn) return;
  for(char ch:k.word) {
    if(game && (ch==';'||ch=='.'||ch==','||ch=='/'||ch==' ')) continue;
    // M5Cardputer also uppercases letters under Ctrl. SYMBOL SHIFT alone
    // must not introduce CAPS SHIFT unless the physical Shift key is held.
    if((k.ctrl || k.alt) && !k.shift && ch>='A' && ch<='Z') ch=char(ch-'A'+'a');
    machine.ascii(ch);
  }
  if(k.enter) machine.ascii('\n');
  if(k.backspace || k.del) machine.ascii('\b');
  if(k.esc) machine.ascii(27);
  // ascii() already translates uppercase and shifted punctuation. Adding
  // CAPS SHIFT to a PC punctuation character would enter extended mode.
  if(k.shift && (k.word.empty() || k.ctrl || k.alt)) machine.press(0,0);
  if(k.ctrl || k.alt) machine.press(7,1);
  if(game) machine.direction(controls,k.up||held(';'),k.down||held('.'),k.left||held(','),k.right||held('/'),held(' '));
}
void capture() {
  silence();waitDisplay();
  Serial.setTxTimeoutMs(100);
  // Little-endian RGB565, framed with a distinctive header for a host receiver.
  Serial.println("ZXFRAME 240 135 RGB565LE");
  uint16_t line[240];
  static DisplayScaler scaler;
  scaler.begin(machine.memory+0x4000,machine.borders,(machine.frames/16)&1,border||tape.playing());
  for(unsigned y=0;y<135;++y) {
    scaler.row(y,line);
    size_t sent=0;const auto *bytes=reinterpret_cast<const uint8_t*>(line);
    const uint32_t start=millis();
    while(sent<sizeof(line) && uint32_t(millis()-start)<1000) sent+=Serial.write(bytes+sent,sizeof(line)-sent);
  }
  Serial.println("\nZXFRAME END");Serial.setTxTimeoutMs(2);deadline=micros();
}
void audioProbe() {
  // Use a separate synthetic machine: the current game, ROM and tape state
  // remain paused in the main machine and resume after the five-second probe.
  silence();waitDisplay();
  std::unique_ptr<Machine> probe(new(std::nothrow) Machine);
  if(!probe) {notify("Sin memoria para prueba de audio");return;}
  const uint8_t tone[]={0x3e,16,0xd3,0xfe,0xee,16,0x06,80,0x10,0xfe,0xc3,2,0x80};
  memcpy(probe->memory+0x8000,tone,sizeof(tone));probe->cpu.pc=0x8000;
  probe->audio=audioSink;
  const auto packetsBefore=audioPackets.load(),gapsBefore=audioUnderruns.load(),errorsBefore=audioErrors.load();
  const auto dropsBefore=audioQueue.dropped(),timeoutsBefore=audioPaceTimeouts;
  audioMaxGapUs=0;
  audioActive.store(true,std::memory_order_release);xTaskNotifyGive(audioTaskHandle);
  uint32_t totalUs=0,maxUs=0,inputMax=0;const auto start=millis();
  for(unsigned frame=0;frame<250;++frame) {
    uint32_t at=micros();M5Cardputer.update();inputMax=std::max(inputMax,uint32_t(micros()-at));
    at=micros();probe->runFrame();const uint32_t elapsed=micros()-at;totalUs+=elapsed;maxUs=std::max(maxUs,elapsed);
    present();paceAudio();
  }
  silence();Serial.setTxTimeoutMs(100);
  Serial.printf("ZX AUDIO TEST frames=250 ms=%u cpu_avg_us=%u cpu_max_us=%u input_max_us=%u packets=%u underruns=%u dropped=%u errors=%u timeouts=%u gap_us=%u filter_us=%u spi_us=%u\n",
    millis()-start,totalUs/250,maxUs,inputMax,audioPackets.load()-packetsBefore,audioUnderruns.load()-gapsBefore,
    audioQueue.dropped()-dropsBefore,audioErrors.load()-errorsBefore,audioPaceTimeouts-timeoutsBefore,audioMaxGapUs.load(),lcdFilterUs.load(),lcdPushUs.load());
  Serial.setTxTimeoutMs(2);deadline=micros();
}
}

namespace spectrum_platform {
void begin() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(2);
  auto config=M5.config();config.clear_display=true;M5Cardputer.begin(config,true);
  M5.Display.setRotation(1);M5.Display.setSwapBytes(true);M5.Display.setBrightness(180);
  displayMutex=xSemaphoreCreateMutex();
  if(!displayMutex) {Serial.println("ZX ERROR: sin mutex LCD");return;}
  message("Sinclair ZX Spectrum 48K","Montando microSD...");
  M5.Speaker.end();auto speaker=M5.Speaker.config();speaker.task_priority=4;speaker.task_pinned_core=0;
  speaker.dma_buf_len=128;speaker.dma_buf_count=4;
  M5.Speaker.config(speaker);M5.Speaker.setVolume(volume);M5.Speaker.setChannelVolume(0,255);
  SPI.begin(40,39,14,12);
  if(!SD.begin(12,SPI,25000000)) {message("SD no disponible","Comprueba FAT32 y reinicia");return;}
  File rom=SD.open("/SPECTRUM/48.ROM",FILE_READ);
  if(!rom || rom.size()!=16384 || rom.read(machine.memory,16384)!=16384) {
    message("Falta la ROM de 48K","/SPECTRUM/48.ROM: 16384 bytes");return;
  }
  rom.close();
  const auto organized=spectrum_files::organize(storage);
  Serial.printf("ZX folders ready=%u TAP=%u TZX=%u SNA=%u Z80=%u collisions=%u failed=%u\n",
    organized.ready,organized.tap,organized.tzx,organized.sna,organized.z80,organized.skipped,organized.failed);
  if(organized.failed) notify("Revisar carpetas SD (ver USB)");
  machine.tape=&tape;machine.audio=audioSink;machine.reset();
  audioMutex=xSemaphoreCreateMutex();
  if(!audioMutex || xTaskCreatePinnedToCore(audioWorker,"zx_audio",4096,nullptr,3,&audioTaskHandle,0)!=pdPASS) {
    message("Sin memoria para audio","Reinicia el Cardputer");return;
  }
  if(xTaskCreatePinnedToCore(lcdWorker,"zx_lcd",4096,nullptr,1,&lcdTaskHandle,0)!=pdPASS) lcdTaskHandle=nullptr;
  ready=true;deadline=micros();
  Serial.printf("ZX-ADV V0.3 ready heap=%u PSRAM=%u ROM=SD cpu_mhz=%u\n",ESP.getFreeHeap(),ESP.getPsramSize(),getCpuFrequencyMhz());
  const auto *partition=esp_ota_get_running_partition();
  if(partition) Serial.printf("ZX partition=%s offset=0x%08x size=%u\n",partition->label,partition->address,partition->size);
}
void tick() {
  if(!ready) {M5Cardputer.update();delay(20);return;}
  uint32_t started=micros();keyboard();inputUs=micros()-started;inputMaxUs=std::max(inputMaxUs,inputUs);
  while(Serial.available()) {
    const char cmd=Serial.read();
    if(cmd=='?') Serial.printf("ZX-ADV V0.3 pc=%04x frames=%u ms=%u tape=%u pos=%u fast=%u heap=%u cpu_us=%u cpu_max_us=%u input_us=%u input_max_us=%u audio_samples=%u audio_packets=%u underruns=%u dropped=%u audio_errors=%u gap_us=%u pace_timeouts=%u lcd=%u filter_us=%u spi_us=%u sd_reads=%u sd_max_us=%u\n",
      machine.cpu.pc,machine.frames,millis(),tape.playing(),tape.position(),fast,ESP.getFreeHeap(),cpuUs,cpuMaxUs,inputUs,inputMaxUs,
      audioQueue.buffered(),audioPackets.load(),audioUnderruns.load(),audioQueue.dropped(),audioErrors.load(),audioMaxGapUs.load(),audioPaceTimeouts,
      lcdFrames.load(),lcdFilterUs.load(),lcdPushUs.load(),sdReads,sdMaxUs);
    if(cmd=='r') resetBasic();
    if(cmd=='m') menu();
    if(cmd=='c') capture();
    if(cmd=='b') audioProbe();
  }
  machine.audioEnabled=!fast;
  if(!fast && !audioActive.exchange(true,std::memory_order_acq_rel)) xTaskNotifyGive(audioTaskHandle);
  started=micros();machine.runFrame();cpuUs=micros()-started;cpuMaxUs=std::max(cpuMaxUs,cpuUs);
  static const char *lastError=nullptr;
  if(tape.error()!=lastError) {lastError=tape.error();if(lastError) notify(lastError);}
  present();
  if(!fast) {
    // The 16 kHz playback clock paces the 3.5 MHz CPU. Keep 40 ms in the
    // software queue instead of sleeping at every 20 ms emulated frame.
    paceAudio();
  } else {deadline=micros();delay(1);}
}
}
