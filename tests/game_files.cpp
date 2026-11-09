#include "../src/platform/GameFiles.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace fs=std::filesystem;
struct HostStorage final : spectrum_files::Storage {
  fs::path root;bool failMove=false;
  explicit HostStorage(fs::path path):root(std::move(path)) {}
  fs::path real(const char *path) const {return root/fs::path(path).relative_path();}
  bool scan(const char *path,spectrum_files::Visitor visitor,void *context) override {
    if(!fs::is_directory(real(path))) return false;
    std::vector<fs::directory_entry> files;
    for(const auto &item:fs::directory_iterator(real(path))) files.push_back(item);
    // Simulate directories arriving after hundreds of files in FAT order.
    std::sort(files.begin(),files.end(),[](const auto &a,const auto &b) {return a.path()>b.path();});
    for(const auto &file:files) visitor(context,file.path().filename().string().c_str(),file.is_directory());
    return true;
  }
  bool exists(const char *path) override {return fs::exists(real(path));}
  bool directory(const char *path) override {return fs::is_directory(real(path));}
  bool mkdir(const char *path) override {return fs::create_directory(real(path));}
  bool move(const char *from,const char *to) override {
    if(failMove || exists(to)) return false;
    std::error_code error;fs::rename(real(from),real(to),error);return !error;
  }
  void put(const std::string &path,const char *value="game") {
    const auto to=real(path.c_str());fs::create_directories(to.parent_path());std::ofstream(to,std::ios::binary)<<value;
  }
  std::string read(const char *path) {
    std::ifstream file(real(path),std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
  }
};
int main(int argc,char **argv) {
  assert(argc==2);using namespace spectrum_files;
  const fs::path temp=argv[1];fs::remove_all(temp);fs::create_directories(temp);
  HostStorage storage(temp/"card");fs::create_directory(storage.root);
  // A completely empty card still gets all parents and all format directories.
  auto result=organize(storage);assert(result.ready && !result.failed);
  for(const char *format:{"TAP","TZX","SNA","Z80"}) assert(storage.directory((std::string(kRoot)+"/"+format).c_str()));
  storage.put("/SPECTRUM/48.ROM","keep ROM");storage.put(std::string(kRoot)+"/zeta.TaP","root tape");
  storage.put(std::string(kRoot)+"/alpha.TzX");storage.put(std::string(kRoot)+"/snap.SNA");storage.put(std::string(kRoot)+"/snap.Z80");
  storage.put(std::string(kRoot)+"/._zeta.tap","AppleDouble");storage.put(std::string(kRoot)+"/readme.txt","keep text");
  storage.put(std::string(kRoot)+"/TAP/zeta.TaP","existing tape");
  storage.put(std::string(kRoot)+"/SNAPSHOTS/legacy.sna","legacy SNA");
  storage.put(std::string(kRoot)+"/SNAPSHOTS/legacy.z80","legacy Z80");
  assert(kindOf("tape.TaP")==Kind::tap && kindOf("tape.tzx")==Kind::tzx && kindOf("disk.dsk")==Kind::none);
  result=organize(storage);assert(result.ready && result.tap==0 && result.tzx==1 && result.sna==2 && result.z80==2 && result.skipped==1 && !result.failed);
  assert(storage.read("/SPECTRUM/GAMES/zeta.TaP")=="root tape");
  assert(storage.read("/SPECTRUM/GAMES/TAP/zeta.TaP")=="existing tape");
  assert(storage.read("/SPECTRUM/GAMES/SNA/legacy.sna")=="legacy SNA");
  assert(storage.read("/SPECTRUM/GAMES/Z80/legacy.z80")=="legacy Z80");
  assert(storage.read("/SPECTRUM/48.ROM")=="keep ROM" && storage.read("/SPECTRUM/GAMES/._zeta.tap")=="AppleDouble");
  for(unsigned i=0;i<270;++i) {
    char name[80];std::snprintf(name,sizeof(name),"%s/tape%03u.tap",kRoot,i);storage.put(name);
  }
  auto listing=list(storage,kRoot);assert(listing.ok && listing.more && listing.entries.size()==kCapacity);
  for(unsigned i=0;i<5;++i) assert(listing.entries[i].directory);
  result=organize(storage);assert(result.tap==270 && result.skipped==1 && !result.failed);
  result=organize(storage);assert(!result.tap && !result.tzx && !result.sna && !result.z80 && result.skipped==1 && !result.failed);
  storage.put(std::string(kRoot)+"/TAP/aventuras/acción.TAP");
  assert(list(storage,child(child(kRoot,"TAP"),"aventuras")).entries.size()==1);
  assert(parent(child(child(kRoot,"TAP"),"aventuras"))==child(kRoot,"TAP"));
  assert(parent(child(kRoot,"TAP"))==kRoot && parent(kRoot)==kRoot);
  assert(parent("/SPECTRUM/GAMES_OTHER/child")==kRoot && child(kRoot,"../escape")==kRoot);
  assert(!list(storage,"/SPECTRUM/OTHER").ok && !list(storage,child(kRoot,"missing")).ok);
  assert(navigate(0,27,-1,0)==26 && navigate(26,27,1,0)==0);
  assert(navigate(5,27,0,1)==11 && navigate(23,27,0,1)==26 && navigate(26,27,0,-1)==20);
  storage.put(std::string(kRoot)+"/failed.tap","keep on failure");storage.failMove=true;
  result=organize(storage);assert(result.failed==1 && storage.read("/SPECTRUM/GAMES/failed.tap")=="keep on failure");
  HostStorage blocked(temp/"blocked");blocked.put(std::string(kRoot)+"/TAP","not a folder");blocked.put(std::string(kRoot)+"/safe.tap","safe");
  result=organize(blocked);assert(!result.ready && result.failed==1 && blocked.read("/SPECTRUM/GAMES/safe.tap")=="safe");
  std::puts("OK format folders, legacy migration, collision/error preservation, large directories and navigation");
}
