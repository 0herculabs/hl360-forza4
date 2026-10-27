#include "../src/wheel.h"
#include <rex/ppc/context.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
bool available=true, driving=false;
int forwarded=0,resets=0;
std::vector<fm4::wheel::ForceEffect> effects;
std::vector<std::pair<int,bool>> operations;
void Check(bool value,const char* why) {if(!value) throw std::runtime_error(why);}
void Put(uint8_t* base,uint32_t p,uint32_t v) {v=__builtin_bswap32(v);std::memcpy(base+p,&v,4);}
uint32_t Get(uint8_t* base,uint32_t p) {uint32_t v;std::memcpy(&v,base+p,4);return __builtin_bswap32(v);}
}
namespace fm4::wheel {
bool ForceAvailable(uint32_t user) {return available && user==0;}
void SubmitEffect(ForceEffect e) {effects.push_back(e);}
void OperateEffect(int id,bool start) {operations.emplace_back(id,start);}
void ResetEffects() {++resets;}
void SetDriving(bool value) {driving=value;}
}
namespace fm4::telemetry {
void NoteDriving(bool) {}
}
#define STUB(name) \
extern "C" void name(PPCContext&,uint8_t*); \
extern "C" void __imp__##name(PPCContext&,uint8_t*) {++forwarded;}
STUB(sub_82CA9C08) STUB(sub_82CA9C68) STUB(sub_82CA9CC8) STUB(sub_82CA9D28)
STUB(sub_82CA9D90) STUB(sub_82CA9DF8) STUB(sub_823A3F68)
#undef STUB
int main() {
  try {
    std::vector<uint8_t> memory(0x20000);
    auto* base=memory.data();constexpr uint32_t data=0x10000,overlap=0x11000;
    PPCContext c{};c.r4.u32=data;
    sub_82CA9C08(c,base);
    Check(c.r3.u32==0 && Get(base,data+4)==5,"Native effect-slot capabilities incorrect");
    Put(base,data,5);Put(base,data+4,1);Put(base,data+12,255);Put(base,data+44,uint32_t(-127));
    Put(base,overlap,997);c.r3.u32=0;c.r4.u32=data;c.r5.u32=1;c.r6.u32=overlap;
    sub_82CA9C68(c,base);
    Check(effects.size()==1 && effects[0].id==5 && effects[0].type==1 && effects[0].magnitude==-127 &&
        effects[0].gain==255 && Get(base,overlap)==0,"Native big-endian SetEffect decoding/completion failed");
    Put(base,data+44,127);c.r3.u32=0;c.r5.u32=1024;sub_82CA9CC8(c,base);
    Check(effects.size()==2 && effects.back().magnitude==127,"Native UpdateEffect mask treated as a count");
    c.r3.u32=0;c.r5.u32=6;sub_82CA9C68(c,base);
    Check(c.r3.u32==87 && effects.size()==2,"Out-of-range effect count accepted");
    Put(base,data,5);Put(base,data+4,2);c.r3.u32=0;c.r4.u32=data;c.r5.u32=1;c.r7.u32=overlap;
    sub_82CA9D28(c,base);Check(operations.size()==1 && operations[0]==std::make_pair(5,false),"Stop effect operation failed");
    c.r3.u32=0;Put(base,data+4,1);sub_82CA9D28(c,base);
    Check(operations.back()==std::make_pair(5,true),"Start effect operation failed");
    c.r3.u32=0;c.r4.u32=overlap;sub_82CA9D90(c,base);Check(resets==1,"Initialize/reset did not clear effects");
    c.r3.u32=0;c.r5.u32=overlap;sub_82CA9DF8(c,base);Check(resets==2,"Device control did not clear effects");
    Put(base,data,0);base[data+5]=1;c.r3.u32=data;sub_823A3F68(c,base);
    Check(driving && forwarded==1,"Native driving gate not observed/forwarded");
    base[data+5]=0;c.r3.u32=data;sub_823A3F68(c,base);Check(!driving,"Native pause/disable gate not observed");
    available=false;const int before=forwarded;
    c.r3.u32=0;sub_82CA9C08(c,base);c.r3.u32=0;sub_82CA9C68(c,base);
    c.r3.u32=0;sub_82CA9CC8(c,base);c.r3.u32=0;sub_82CA9D28(c,base);
    c.r3.u32=0;sub_82CA9D90(c,base);c.r3.u32=0;sub_82CA9DF8(c,base);
    Check(forwarded==before+6,"Default/no-wheel behavior was intercepted");
    std::puts("Native FFB effect decoding, completion, bounds, operations and fallback passed.");
    return 0;
  }catch(const std::exception& e){std::fprintf(stderr,"FFB test failed: %s\n",e.what());return 1;}
}
