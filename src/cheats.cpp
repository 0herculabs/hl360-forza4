// Cheats from the launcher's Cheats tab: the profile's credits and the unicorn
// cars in the dealer.
//
// Credits are the profile statistic "Main/Credits" (key string 0x8200F314).
// FM4 sets them through 0x82405FC0 (profile, credits): it clamps to
// 999,999,999, stores the value, marks the profile changed and tells the
// credit displays. A profile loading with more than 20,000,000 is lowered to
// that amount (0x82431430). The first time the game reads the credits of the
// loaded profile, the launcher's amount is set through that same function, so
// the menus show it and the game saves it with the profile. Reads go through
// 0x82385178 (out, profile, key), used by the credit displays, or through
// 0x823E5700 (profile).
//
// Unicorn cars are the cars with Data_Car.IsUnicorn = 1: only gifts on the
// console. The dealer's car lists (over Drivable_Data_Car, a view of Data_Car)
// filter them out with SQL held in .rdata. Replacing "IsUnicorn=0" with
// "Id<>1215   " (same length) lets 17 of them through; Id 1215 is NUL_Car_00,
// a placeholder that is also flagged unicorn. The AI's car picks keep their
// filter.

#include "cheats.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(cheat_set_credits, false, "FM4/Cheats",
                    "Set the profile's credits to cheat_credits at every game start");
REXCVAR_DEFINE_INT32(cheat_credits, 1000000, "FM4/Cheats", "Credits for cheat_set_credits (at most 20,000,000)");
REXCVAR_DEFINE_BOOL(cheat_unicorn_dealer, false, "FM4/Cheats", "Show the unicorn cars in the car lists");

namespace fm4::cheats {

namespace {

// The game lowers anything above this when a profile loads.
constexpr int32_t kMaxCredits = 20000000;

constexpr uint32_t kCreditsKey = 0x8200F314;  // "Main/Credits"

struct Filter {
  uint32_t address;
  const char* text;
};

// The dealer's car list filters. Each is checked before it is changed, so
// another executable is left alone.
constexpr Filter kUnicornFilters[] = {
    {0x820337E4, " AND IsUnicorn=0 AND IsCarVisible(ModelId) "},
    {0x82033810, " and IsUnicorn=0 AND IsCarInstalledAndPurchased(ModelId) "},
    {0x8203CF50, " AND IsUnicorn=0 AND IsCarVisible(Id) "},
};

// Cleared once the credits are set (or when the cheat is off).
std::atomic<bool> g_credits_pending{true};

}  // namespace

void PatchImage(rex::Runtime* runtime) {
  if (!REXCVAR_GET(cheat_unicorn_dealer)) return;
  auto* memory = runtime->memory();
  int patched = 0;
  for (const Filter& f : kUnicornFilters) {
    char* text = reinterpret_cast<char*>(memory->TranslateVirtual(f.address));
    if (std::memcmp(text, f.text, std::strlen(f.text) + 1) != 0) {
      REXLOG_WARN("cheats: unexpected car list filter at {:08X}, unicorn cars not shown", f.address);
      continue;
    }
    // .rdata is read-only for the guest and the host alike.
    const uint32_t condition = f.address + uint32_t(std::strstr(text, "IsUnicorn=0") - text);
    auto* heap = memory->LookupHeap(condition);
    uint32_t old_protect = 0;
    if (!heap || !heap->Protect(condition, 11, rex::memory::kMemoryProtectRead | rex::memory::kMemoryProtectWrite,
                                &old_protect)) {
      REXLOG_WARN("cheats: car list filter at {:08X} is not writable", f.address);
      continue;
    }
    std::memcpy(memory->TranslateVirtual(condition), "Id<>1215   ", 11);
    heap->Protect(condition, 11, old_protect);
    ++patched;
  }
  REXLOG_INFO("cheats: unicorn cars shown in {} car lists", patched);
}

}  // namespace fm4::cheats

extern "C" void sub_82405FC0(PPCContext& __restrict ctx, uint8_t* base);

namespace {

// Sets the credits of `profile` once, before the read that called it.
void SetCreditsOnce(PPCContext& ctx, uint8_t* base, uint32_t profile) {
  using namespace fm4::cheats;
  if (!g_credits_pending.load(std::memory_order_relaxed) || !g_credits_pending.exchange(false) ||
      !REXCVAR_GET(cheat_set_credits)) {
    return;
  }
  // The call may clobber any volatile register; the read still needs its arguments.
  const int32_t credits = std::clamp(REXCVAR_GET(cheat_credits), 0, kMaxCredits);
  const PPCContext saved = ctx;
  ctx.r3.u64 = profile;
  ctx.r4.u64 = uint32_t(credits);
  sub_82405FC0(ctx, base);
  ctx = saved;
  REXLOG_INFO("cheats: profile credits set to {}", credits);
}

}  // namespace

extern "C" void __imp__sub_82385178(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void sub_82385178(PPCContext& __restrict ctx, uint8_t* base) {
  if (ctx.r5.u32 == fm4::cheats::kCreditsKey && ctx.r4.u32) SetCreditsOnce(ctx, base, ctx.r4.u32);
  __imp__sub_82385178(ctx, base);
}

extern "C" void __imp__sub_823E5700(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void sub_823E5700(PPCContext& __restrict ctx, uint8_t* base) {
  if (ctx.r3.u32) SetCreditsOnce(ctx, base, ctx.r3.u32);
  __imp__sub_823E5700(ctx, base);
}
