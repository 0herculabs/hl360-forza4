// FM4 USA Rev 1 native XInput-FF boundary. The game owns its physics and FFB
// calculation; translate its five effect slots instead of controller rumble.
#include "wheel_input.h"
#include "telemetry/fm4_car.h"
#include "generated/default/fm4_pch.h"

namespace {
using namespace fm4::wheel;
bool Buffer(uint32_t p,uint32_t length) {return p>=0x10000 && !(p&3) && p<=0xFFFFFFFFu-length;}
ForceEffect ReadEffect(uint8_t* base,uint32_t address) {
  ForceEffect e;
  e.id=int(REX_LOAD_U32(address)); e.type=int(REX_LOAD_U32(address+4));
  e.gain=int(REX_LOAD_U32(address+12));
  e.magnitude=int32_t(REX_LOAD_U32(address+44));e.offset=e.magnitude;
  e.positive_coefficient=int32_t(REX_LOAD_U32(address+48));
  e.negative_coefficient=int32_t(REX_LOAD_U32(address+52));
  e.positive_saturation=int(REX_LOAD_U32(address+56));
  e.negative_saturation=int(REX_LOAD_U32(address+60));e.deadband=int(REX_LOAD_U32(address+64));
  return e;
}
void Complete(PPCContext& ctx,uint8_t* base,uint32_t overlap) {
  if(Buffer(overlap,8)) {REX_STORE_U32(overlap,0);REX_STORE_U32(overlap+4,0);}
  ctx.r3.u64=0;
}
}
#define ORIGINAL(name) extern "C" void __imp__##name(PPCContext& __restrict ctx,uint8_t* base)
ORIGINAL(sub_82CA9C08);
extern "C" void sub_82CA9C08(PPCContext& __restrict ctx,uint8_t* base) {
  if(!ForceAvailable(ctx.r3.u32)) {__imp__sub_82CA9C08(ctx,base);return;}
  const uint32_t p=ctx.r4.u32;
  if(!Buffer(p,8)) {ctx.r3.u64=87;return;}
  REX_STORE_U32(p,0);REX_STORE_U32(p+4,5);ctx.r3.u64=0;
}
ORIGINAL(sub_82CA9C68);
extern "C" void sub_82CA9C68(PPCContext& __restrict ctx,uint8_t* base) {
  if(!ForceAvailable(ctx.r3.u32)) {__imp__sub_82CA9C68(ctx,base);return;}
  const uint32_t p=ctx.r4.u32,count=ctx.r5.u32;
  if(!count || count>5 || !Buffer(p,count*116)) {ctx.r3.u64=87;return;}
  for(uint32_t i=0;i<count;++i) SubmitEffect(ReadEffect(base,p+i*116));
  Complete(ctx,base,ctx.r6.u32);
}
ORIGINAL(sub_82CA9CC8);
extern "C" void sub_82CA9CC8(PPCContext& __restrict ctx,uint8_t* base) {
  if(!ForceAvailable(ctx.r3.u32)) {__imp__sub_82CA9CC8(ctx,base);return;}
  if(!Buffer(ctx.r4.u32,116)) {ctx.r3.u64=87;return;}
  SubmitEffect(ReadEffect(base,ctx.r4.u32));Complete(ctx,base,ctx.r6.u32);
}
ORIGINAL(sub_82CA9D28);
extern "C" void sub_82CA9D28(PPCContext& __restrict ctx,uint8_t* base) {
  if(!ForceAvailable(ctx.r3.u32)) {__imp__sub_82CA9D28(ctx,base);return;}
  const uint32_t p=ctx.r4.u32,count=ctx.r5.u32;
  if(!count || count>5 || !Buffer(p,count*8)) {ctx.r3.u64=87;return;}
  for(uint32_t i=0;i<count;++i) {
    const uint32_t id=REX_LOAD_U32(p+i*8),op=REX_LOAD_U32(p+i*8+4);
    OperateEffect(int(id),op==1);
  }
  Complete(ctx,base,ctx.r7.u32);
}
ORIGINAL(sub_82CA9D90);
extern "C" void sub_82CA9D90(PPCContext& __restrict ctx,uint8_t* base) {
  if(!ForceAvailable(ctx.r3.u32)) {__imp__sub_82CA9D90(ctx,base);return;}
  ResetEffects();Complete(ctx,base,ctx.r4.u32);
}
ORIGINAL(sub_82CA9DF8);
extern "C" void sub_82CA9DF8(PPCContext& __restrict ctx,uint8_t* base) {
  if(!ForceAvailable(ctx.r3.u32)) {__imp__sub_82CA9DF8(ctx,base);return;}
  ResetEffects();Complete(ctx,base,ctx.r5.u32);
}
ORIGINAL(sub_823A3F68);
extern "C" void sub_823A3F68(PPCContext& __restrict ctx,uint8_t* base) {
  const uint32_t object=ctx.r3.u32;
  if(Buffer(object,1376) && REX_LOAD_U32(object)==0) fm4::telemetry::NoteDriving(REX_LOAD_U8(object+5)!=0);
  if(Buffer(object,1376) && ForceAvailable(REX_LOAD_U32(object))) SetDriving(REX_LOAD_U8(object+5)!=0);
  __imp__sub_823A3F68(ctx,base);
}
#undef ORIGINAL
