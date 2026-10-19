#pragma once
#include <map>
#include <memory>
#include <string>
#include <cstring>
#include <algorithm>
#include <cstdint>
using String=std::string;
struct FakeFSState {
  std::map<std::string,std::shared_ptr<std::string>> files;
  size_t writeLimit=SIZE_MAX;
  int renameCalls=0, failRenameAt=0;
  bool mount=true, removeFails=false;
};
inline FakeFSState fake;
class File {
  std::shared_ptr<std::string> data;
  size_t position=0;
 public:
  File()=default;
  explicit File(std::shared_ptr<std::string> s):data(s){}
  explicit operator bool() const {return bool(data);}
  size_t size() const {return data?data->size():0;}
  void close(){data.reset();}
  void flush(){}
  int read(){return data && position<data->size() ? uint8_t((*data)[position++]) : -1;}
  size_t readBytes(char* out,size_t n){size_t count=0;while(count<n){int c=read();if(c<0)break;out[count++]=char(c);}return count;}
  size_t write(const uint8_t* src,size_t n){if(!data)return 0;n=std::min(n,fake.writeLimit);data->append(reinterpret_cast<const char*>(src),n);return n;}
  size_t write(uint8_t c){return write(&c,1);}
  size_t print(const char* text){return write(reinterpret_cast<const uint8_t*>(text),strlen(text));}
};
struct FakeFS {
  bool begin(bool format){if(format)abort();return fake.mount;}
  bool exists(const std::string& path){return fake.files.count(path)!=0;}
  File open(const std::string& path,const char* mode){
    if(!fake.mount)return File();
    if(mode[0]=='w')fake.files[path]=std::make_shared<std::string>();
    return exists(path)?File(fake.files[path]):File();
  }
  bool rename(const std::string& from,const std::string& to){
    if(++fake.renameCalls==fake.failRenameAt || !exists(from) || exists(to))return false;
    fake.files[to]=fake.files[from];fake.files.erase(from);return true;
  }
  bool remove(const std::string& path){if(fake.removeFails)return false;return fake.files.erase(path)>0;}
};
inline FakeFS LittleFS;
