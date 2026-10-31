// Wide windows (21:9, 32:9): Hor+ in the guest, as Pinyon Shift does for FH1.
//
// FM4 renders a 16:9 image. With display_wide_view the game's main view is
// widened by the window's aspect over 16:9, so the title renders and culls a
// wider horizontal field of view into its 16:9 image, and the presenter
// stretches that image over the window: the world keeps its proportions and
// the sides show more of it. The HUD is narrowed back to its proportions by
// the SDK (hud_squeeze).
//
// sub_822B5E08 returns a viewport's width / height (FH1 sub_823E22C8,
// identical). The main view's setup (sub_822CA758) passes it to the camera
// (vtable slot 0x3C, the aspect every projection and frustum of the frame
// starts from) and to a second consumer; both are widened, the 1280x720 main
// viewport only. Mirrors, cube maps and render-to-texture views keep theirs.
// Projections are built in sub_822C0BA8 (FH1 sub_823EAED0) with the aspect in
// f2: that hook only notes frames with a widened view and logs.
#include "wide_view.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "generated/default/fm4_pch.h"

REXCVAR_DEFINE_BOOL(display_wide_view, true, "FM4/Display",
                    "Fill wide windows: show more of the world at the sides (Hor+) instead of "
                    "black bars; the HUD stays 16:9 in the middle");
REXCVAR_DEFINE_BOOL(display_hud_edges, false, "FM4/Display",
                    "With display_wide_view: the HUD's sides go to the window's edges (the minimap, "
                    "the speedometer) instead of the middle 16:9");
REXCVAR_DEFINE_BOOL(display_log_views, false, "FM4/Display",
                    "Log the perspective views the game builds every 5 s (caller, aspect)");

namespace fm4::wide_view {
namespace {
constexpr float k16x9 = 16.0f / 9.0f;
std::atomic<float> g_scale{1.0f};
// Widened views since the last frame, and whether frames are shown wide.
std::atomic<uint32_t> g_frame_views{0};
std::atomic<bool> g_wide{false};

// Shows frames wide (stretched over the window, HUD narrowed) or 16:9 (black
// bars, as without the setting).
void Present(bool wide) {
  if (wide == g_wide.exchange(wide)) return;
  const float scale = wide ? g_scale.load() : 1.0f;
  rex::cvar::SetFlagByName("present_letterbox", wide ? "false" : "true");
  rex::cvar::SetFlagByName("hud_squeeze", fmt::format("{:.6f}", scale));
  // ShiftGlue's renderers (Vulkan included) have only the centred squeeze,
  // under its FH1 name; whichever the SDK lacks is ignored.
  rex::cvar::SetFlagByName("fh1_hud_squeeze", fmt::format("{:.6f}", scale));
  // Edges: HUD elements in the outer thirds of the 16:9 image keep their size
  // but move with the window's left and right edges.
  rex::cvar::SetFlagByName("hud_squeeze_edges", REXCVAR_GET(display_hud_edges) ? "true" : "false");
  // The virtual mirror of the bumper and hood cameras is drawn into the scene
  // by its own vertex shader (seen with gpu_draw_log_filter ":384x128:", the
  // mirror view's texture); it is narrowed with the HUD.
  rex::cvar::SetFlagByName("hud_squeeze_vertex_shaders", "FD5E9BBE6622BCC5");
  REXLOG_INFO("Wide view: {}", wide ? "wide frames" : "16:9 frames (no 3D view)");
}

// display_log_views: calls per caller and aspect.
std::mutex g_log_mutex;
std::map<std::pair<uint32_t, int>, uint32_t> g_views;
std::chrono::steady_clock::time_point g_last_log;

void NoteView(uint32_t caller, float aspect, bool widened) {
  std::lock_guard lock(g_log_mutex);
  ++g_views[{caller, int(std::lround(aspect * 1000.0f)) * (widened ? -1 : 1)}];
  const auto now = std::chrono::steady_clock::now();
  if (now - g_last_log < std::chrono::seconds(5)) return;
  g_last_log = now;
  for (const auto& [key, count] : g_views) {
    REXLOG_INFO("Wide view: caller {:08X} aspect {:.3f}{} x{}", key.first,
                std::abs(key.second) / 1000.0f, key.second < 0 ? " (widened)" : "", count);
  }
  g_views.clear();
}
}  // namespace

float ScaleFor(uint32_t width, uint32_t height) {
  if (!width || !height) return 1.0f;
  const float scale = (float(width) / float(height)) / k16x9;
  // Narrower than 16:9 (or within 1 %) stays letterboxed.
  return scale > 1.01f ? scale : 1.0f;
}

bool IsWidened(float aspect) {
  const float scale = g_scale.load(std::memory_order_relaxed);
  return scale != 1.0f && std::abs(aspect - k16x9 * scale) < 0.01f;
}

void SetWindowSize(uint32_t width, uint32_t height) {
  const float scale = REXCVAR_GET(display_wide_view) ? ScaleFor(width, height) : 1.0f;
  if (scale == g_scale.exchange(scale)) return;
  REXLOG_INFO("Wide view: window {}x{}, view widened x{:.4f}", width, height, scale);
  // Wide frames take the new factor (or end) at once.
  if (g_wide.load()) {
    Present(false);
    if (scale > 1.0f) Present(true);
  }
}

void OnFrame() {
  // Frames without a widened 3D view (movies, loading, the title screen) are
  // the title's own 16:9 layers: stretched they would look wrong, so they are
  // shown 16:9. The switch to wide is quick, the switch back waits half a
  // second so a camera cut or a dialog does not flicker.
  static uint32_t with = 0, without = 0;
  const bool views = g_frame_views.exchange(0, std::memory_order_relaxed) != 0;
  with = views ? with + 1 : 0;
  without = views ? 0 : without + 1;
  if (g_scale.load(std::memory_order_relaxed) == 1.0f) {
    Present(false);
  } else if (with >= 2) {
    Present(true);
  } else if (without >= 30) {
    Present(false);
  }
}

float Scale() { return g_scale.load(std::memory_order_relaxed); }

void NoteWidened() { g_frame_views.fetch_add(1, std::memory_order_relaxed); }
}  // namespace fm4::wide_view

// The viewport aspect getter: r3 the viewport (width at +8, height at +12).
extern "C" void __imp__sub_822B5E08(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void sub_822B5E08(PPCContext& __restrict ctx, uint8_t* base) {
  const uint32_t viewport = ctx.r3.u32;
  __imp__sub_822B5E08(ctx, base);
  // The main view's setup: the camera's aspect, and the second consumer.
  const uint32_t caller = uint32_t(ctx.lr);
  if (caller != 0x822CA7FCu && caller != 0x822CAA5Cu) return;
  const float scale = fm4::wide_view::Scale();
  if (scale == 1.0f || REX_LOAD_U32(viewport + 8) != 1280u || REX_LOAD_U32(viewport + 12) != 720u) {
    return;
  }
  ctx.f1.f64 = double(float(ctx.f1.f64) * scale);
}

// The projection and frustum builder (FH1 sub_823EAED0): f1 field of view,
// f2 aspect, f3 near, f4 far.
extern "C" void __imp__sub_822C0BA8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void sub_822C0BA8(PPCContext& __restrict ctx, uint8_t* base) {
  const float aspect = float(ctx.f2.f64);
  const bool widened = fm4::wide_view::IsWidened(aspect);
  if (REXCVAR_GET(display_log_views)) fm4::wide_view::NoteView(uint32_t(ctx.lr), aspect, widened);
  if (widened) fm4::wide_view::NoteWidened();
  __imp__sub_822C0BA8(ctx, base);
}
