#include <initializer_list>
#include "../protection.h"
#include "../voltage_filter.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint64_t seconds(unsigned n) { return uint64_t(n)*1000000; }
static Decision feed(Protection& p, float v, unsigned start, unsigned end, bool valid=true) {
  Decision d;
  for(uint64_t t=seconds(start);t<=seconds(end);t+=100000) d=p.step(valid,v,t);
  return d;
}
int main() {
  static_assert(SURGE_THRESHOLD==245.0f,"Surge threshold");
  static_assert(LOAD_OFF_LEVEL==1 && LOAD_ON_LEVEL==0,"Active-low COM-NO coil wiring");
  for(float v : {244.99f,245.0f,245.01f}) {
    Protection p; p.state=RelayState::Normal;
    for(uint64_t t=0;t<seconds(5);t+=100000) assert(!p.step(true,v,t).commandOff);
    auto d=p.step(true,v,seconds(5));
    assert(d.commandOff==(v>=245)); assert((d.event==EventType::Surge)==(v>=245));
  }
  {
    VoltageFilter f; Protection p; p.state=RelayState::Normal;
    for(int i=0;i<3;i++) f.update(true,230);
    f.update(true,260); assert(f.rms==230);
    f.update(true,230); f.update(true,230);
    uint64_t crossing=0;
    for(uint64_t t=100000;t<=seconds(6);t+=100000) {
      f.update(true,260);
      if(f.rms>=245 && !crossing) crossing=t;
      auto d=p.step(true,f.rms,t);
      if(t<crossing+seconds(5)) assert(!d.commandOff);
      if(t==crossing+seconds(5)) assert(d.commandOff && d.event==EventType::Surge);
    }
  }
  VoltageFilter filter;
  for(int i=0;i<3;i++) filter.update(true,225);
  filter.update(true,290); assert(filter.rms==225 && filter.display==225);
  filter.update(true,225); assert(filter.rms==225);
  filter.update(true,225); assert(filter.rms==225);
  for(int i=0;i<4;i++) filter.update(true,260);
  assert(filter.rms==260); // A sustained rise must pass through.
  filter.update(false,0); assert(filter.rms==0 && filter.display==0);
  filter.update(true,230); assert(filter.rms==230 && filter.display==230);
  Protection band; band.state=RelayState::Normal;
  const float normalValues[] = {220.0f,225.0f,230.0f,235.0f};
  for(float v : normalValues) {
    band.step(true,v,0); assert(!strcmp(band.classification(),"NORMAL"));
  }
  band.step(true,236,1); assert(!strcmp(band.classification(),"HIGH"));
  Protection p;
  assert(!feed(p,230,0,29).commandOn);
  assert(p.step(true,230,seconds(30)).commandOn && p.tripCount==0);
  for(float bad : {NAN,-1.0f}) {
    p=Protection{}; p.state=RelayState::Normal;
    assert(p.step(true,bad,0).commandOff);
  }
  // A low reading, an invalid reading, or a missing-measurement gap restarts trip timing.
  for(int interruption=0;interruption<3;interruption++) {
    p=Protection{}; p.state=RelayState::Normal; feed(p,250,0,4);
    if(interruption<2) p.step(interruption==0,230,seconds(5));
    unsigned restart=interruption==2 ? 8 : 6;
    assert(!feed(p,250,restart,restart+4).commandOff);
    assert(p.step(true,250,seconds(restart+5)).commandOff);
  }
  for(float v : {219.99f,235.01f,244.99f}) {
    p=Protection{}; p.state=RelayState::Normal;
    for(unsigned i=0;i<60;i++) assert(!p.step(true,v,seconds(i)).commandOff);
    assert(p.step(true,v,seconds(60)).commandOff);
    assert(!feed(p,230,61,90).commandOn);
    assert(p.step(true,230,seconds(91)).commandOn);
  }
  for(float v : {220.0f,235.0f}) {
    p=Protection{};
    assert(!feed(p,v,0,29).commandOn);
    assert(p.step(true,v,seconds(30)).commandOn);
  }
  for(int interruption=0;interruption<4;interruption++) {
    p=Protection{}; feed(p,230,0,29);
    if(interruption<3) p.step(interruption!=2,interruption==0 ? 219.99f : 235.01f,seconds(30));
    unsigned restart=interruption==3 ? 33 : 31;
    assert(!feed(p,230,restart,restart+29).commandOn);
    assert(p.step(true,230,seconds(restart+30)).commandOn);
  }
  p=Protection{}; feed(p,230,0,30);
  for(unsigned episode=0;episode<3;episode++) {
    unsigned start=31+episode*37;
    auto d=feed(p,250,start,start+5); assert(d.commandOff && d.event==EventType::Surge);
    assert(p.tripCount==episode+1);
    d=feed(p,230,start+6,start+36);
    assert(d.commandOn==(episode<2));
  }
  assert(p.state==RelayState::Lockout);
  feed(p,230,2000,2100); assert(p.state==RelayState::Lockout);
  p.reset(seconds(2101)); assert(p.tripCount==0 && p.state==RelayState::Waiting);
  assert(!feed(p,230,2101,2130).commandOn);
  assert(p.step(true,230,seconds(2131)).commandOn);
  p=Protection{}; p.state=RelayState::Normal; feed(p,250,0,5);
  feed(p,260,6,100); assert(p.tripCount==1);
  feed(p,230,101,131); assert(p.state==RelayState::Normal);
  feed(p,250,1801,1806); assert(p.tripCount==1);
  p=Protection{}; p.restore(true,3,0); feed(p,230,0,40); assert(p.state==RelayState::Lockout);
  p=Protection{}; p.restore(false,1,0); assert(feed(p,230,0,30).commandOn);
  Qualification q; assert(!q.update(true,0,seconds(10))); assert(!q.update(true,seconds(10),seconds(10)));
  ResetButton b;
  assert(!b.update(false,0)); assert(!b.update(false,100000)); // held at boot unarmed
  assert(!b.update(true,200000)); assert(!b.update(true,300000));
  assert(!b.update(false,400000)); assert(b.update(false,500000));
  for(int i=0;i<20;i++) assert(!b.update(false,600000+i*100000));
  assert(!b.update(true,3000000)); assert(!b.update(true,3100000));
  assert(!b.update(false,3200000)); assert(b.update(false,3300000));
  puts("PASS: startup, thresholds, elapsed timers, interrupted recovery, cooldown, lockout, expiry, reset edges");
}
