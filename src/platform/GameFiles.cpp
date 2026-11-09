#include "GameFiles.h"
#include <algorithm>
#include <utility>

namespace spectrum_files {
namespace {
bool visible(const std::string &s) {return !s.empty() && s[0]!='.' && s.find('/')==std::string::npos;}
bool inside(const std::string &s) {return s==kRoot || s.compare(0,std::string(kRoot).size()+1,std::string(kRoot)+"/")==0;}
const char *formatFolder(Kind kind) {
  switch(kind) {case Kind::tap:return "TAP";case Kind::tzx:return "TZX";
    case Kind::sna:return "SNA";case Kind::z80:return "Z80";default:return "";}
}
void insert(Listing &out,Entry entry) {
  const auto before=[](const Entry &a,const Entry &b) {return a.directory!=b.directory?a.directory:a.name<b.name;};
  const auto at=std::lower_bound(out.entries.begin(),out.entries.end(),entry,before);
  if(out.entries.size()==kCapacity) {
    out.more=true;if(at==out.entries.end()) return;
    const auto index=std::size_t(at-out.entries.begin());out.entries.pop_back();
    out.entries.insert(out.entries.begin()+index,std::move(entry));
  } else out.entries.insert(at,std::move(entry));
}
bool ensure(Storage &storage,const std::string &path) {
  return storage.exists(path.c_str())?storage.directory(path.c_str()):storage.mkdir(path.c_str());
}
}
Kind kindOf(const std::string &name) {
  const auto dot=name.find_last_of('.');if(dot==std::string::npos) return Kind::none;
  auto ext=name.substr(dot+1);
  for(auto &c:ext) if(c>='A' && c<='Z') c=char(c+'a'-'A');
  return ext=="tap"?Kind::tap:ext=="tzx"?Kind::tzx:ext=="sna"?Kind::sna:ext=="z80"?Kind::z80:Kind::none;
}
std::string baseName(const std::string &path) {
  const auto at=path.find_last_of('/');return at==std::string::npos?path:path.substr(at+1);
}
std::string child(const std::string &folder,const std::string &name) {
  if(!inside(folder) || !visible(name)) return inside(folder)?folder:kRoot;
  return folder+"/"+name;
}
std::string parent(const std::string &folder) {
  if(!inside(folder) || folder==kRoot) return kRoot;
  return folder.substr(0,folder.find_last_of('/'));
}
Listing list(Storage &storage,const std::string &folder) {
  Listing out;out.entries.reserve(kCapacity);
  if(!inside(folder)) return out;
  out.ok=storage.scan(folder.c_str(),[](void *context,const char *raw,bool directory) {
    const auto name=baseName(raw);
    if(visible(name) && (directory || kindOf(name)!=Kind::none)) insert(*static_cast<Listing*>(context),{name,directory});
  },&out);
  return out;
}
std::size_t navigate(std::size_t selected,std::size_t count,int rows,int pages) {
  if(!count) return 0;selected=std::min(selected,count-1);
  if(pages) {
    const auto page=selected/kPageRows,last=(count-1)/kPageRows;
    if((pages<0 && !page) || (pages>0 && page==last)) return selected;
    const auto next=pages>0?page+1:page-1;
    return std::min(next*kPageRows+selected%kPageRows,count-1);
  }
  if(rows<0) return (selected+count-1)%count;
  if(rows>0) return (selected+1)%count;
  return selected;
}
OrganizeResult organize(Storage &storage) {
  OrganizeResult result;
  if(!ensure(storage,kSystem) || !ensure(storage,kRoot)) {++result.failed;return result;}
  for(Kind kind:{Kind::tap,Kind::tzx,Kind::sna,Kind::z80}) {
    if(!ensure(storage,std::string(kRoot)+"/"+formatFolder(kind))) {++result.failed;return result;}
  }
  result.ready=true;
  // Keep arbitrary user subfolders. Also migrate V0.1's mixed SNAPSHOTS folder.
  for(const auto &folder:{std::string(kRoot),std::string(kRoot)+"/SNAPSHOTS"}) {
    if(!storage.directory(folder.c_str())) continue;
    std::string after;
    for(;;) {
      struct Batch {Listing files;const std::string *after;};Batch batch{{},&after};
      batch.files.entries.reserve(kCapacity);
      if(!storage.scan(folder.c_str(),[](void *raw,const char *entry,bool directory) {
        auto &batch=*static_cast<Batch*>(raw);const auto name=baseName(entry);
        if(!directory && visible(name) && name>*batch.after && kindOf(name)!=Kind::none)
          insert(batch.files,{name,false});
      },&batch)) {++result.failed;break;}
      if(batch.files.entries.empty()) break;
      for(const auto &entry:batch.files.entries) {
        after=entry.name;const auto kind=kindOf(entry.name);
        const auto source=folder+"/"+entry.name;
        const auto target=std::string(kRoot)+"/"+formatFolder(kind)+"/"+entry.name;
        if(storage.exists(target.c_str())) {++result.skipped;continue;}
        if(!storage.move(source.c_str(),target.c_str())) {++result.failed;continue;}
        switch(kind) {case Kind::tap:++result.tap;break;case Kind::tzx:++result.tzx;break;
          case Kind::sna:++result.sna;break;case Kind::z80:++result.z80;break;default:break;}
      }
    }
  }
  return result;
}
}
