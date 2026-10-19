#pragma once
#include <LittleFS.h>
#include <ArduinoJson.h>
// Only loop() owns filesystem/JSON operations. Never called by acquisition.
class Storage {
 public:
  bool mounted=false, healthy=false, writable=true;
  uint32_t failures=0;
  void begin() { mounted=LittleFS.begin(false); healthy=mounted; if (!mounted) failures++; }
  bool fail() { healthy=false; failures++; return false; }
  bool quarantine(const char* path) {
    if(!LittleFS.exists(path)) return true;
    String corrupt=String(path)+".corrupt";
    if(LittleFS.exists(corrupt) || !LittleFS.rename(path,corrupt)) { writable=false; return fail(); }
    return true;
  }
  bool parse(const String& path, JsonDocument& doc, size_t cap) {
    if (!mounted) return false;
    File f=LittleFS.open(path,"r");
    if (!f || f.size()>cap) { if(f) f.close(); return false; }
    auto error=deserializeJson(doc,f,DeserializationOption::NestingLimit(8)); f.close();
    return !error && !doc.overflowed();
  }
  bool load(const char* path, JsonDocument& doc, size_t cap, bool& damaged) {
    damaged=false;
    if (!mounted) { damaged=true; return false; }
    bool exists=LittleFS.exists(path);
    if (exists && parse(path,doc,cap)) return true;
    String backup=String(path)+".bak";
    damaged=exists || LittleFS.exists(backup);
    if(exists) quarantine(path);
    doc.clear();
    if (parse(backup,doc,cap)) { fail(); return true; }
    if (damaged) fail();
    return false;
  }
  bool commit(const char* path, JsonDocument& doc, size_t cap) {
    size_t bytes=measureJson(doc);
    if (!mounted || !writable || doc.overflowed() || bytes>cap) return fail();
    String temp=String(path)+".tmp", backup=String(path)+".bak";
    File f=LittleFS.open(temp,"w"); if(!f) return fail();
    size_t written=serializeJson(doc,f); f.flush(); f.close();
    File verify=LittleFS.open(temp,"r"); bool valid=verify && verify.size()==bytes && written==bytes;
    if (verify) verify.close();
    if (!valid) return fail();
    // Keep one committed version until the replacement is complete.
    if (LittleFS.exists(path)) {
      if (LittleFS.exists(backup) && !LittleFS.remove(backup)) return fail();
      if (!LittleFS.rename(path,backup)) return fail();
    }
    if (!LittleFS.rename(temp,path)) return fail();
    return true;
  }
  File start(const char* path) {
    if(!mounted || !writable) { fail(); return File(); }
    File f=LittleFS.open(String(path)+".tmp","w"); if(!f) fail(); return f;
  }
  bool finish(const char* path,File& f,size_t expected,size_t cap,bool wroteAll) {
    if(!f) return fail();
    f.flush(); f.close(); String temp=String(path)+".tmp", backup=String(path)+".bak";
    File verify=LittleFS.open(temp,"r");
    bool valid=verify && verify.size()==expected && expected<=cap && wroteAll; if(verify) verify.close();
    if(!valid) return fail();
    if(LittleFS.exists(path)) {
      if(LittleFS.exists(backup) && !LittleFS.remove(backup)) return fail();
      if(!LittleFS.rename(path,backup)) return fail();
    }
    if(!LittleFS.rename(temp,path)) return fail(); return true;
  }
};
