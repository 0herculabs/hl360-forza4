// Debug captures: with capture_dir set, the presented image is saved there as
// a BMP every capture_interval seconds (renderer checks without a player,
// together with input_script). Needs the swapchain readable, which is set
// before the presenter starts.
#include "debug_capture.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/presenter.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

REXCVAR_DEFINE_STRING(capture_dir, "", "FM4/Debug",
                      "Folder the presented image is saved to every capture_interval seconds");
REXCVAR_DEFINE_DOUBLE(capture_interval, 5.0, "FM4/Debug", "Seconds between debug captures")
    .range(0.5, 600.0);

namespace fm4 {
namespace {

// 24-bit bottom-up BMP from the presenter's R8 G8 B8 X8 rows.
bool WriteBmp(const std::filesystem::path& file, const rex::ui::RawImage& image) {
  const uint32_t row = (image.width * 3 + 3) & ~3u;
  const uint32_t size = 54 + row * image.height;
  uint8_t header[54] = {'B', 'M'};
  auto put32 = [&](int at, uint32_t v) {
    for (int i = 0; i < 4; ++i) header[at + i] = uint8_t(v >> (8 * i));
  };
  put32(2, size);
  put32(10, 54);
  put32(14, 40);
  put32(18, image.width);
  put32(22, image.height);
  header[26] = 1;
  header[28] = 24;
  put32(34, row * image.height);
  std::ofstream out(file, std::ios::binary);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(header), sizeof(header));
  std::string line(row, '\0');
  for (uint32_t y = image.height; y-- > 0;) {
    const uint8_t* src = image.data.data() + y * image.stride;
    for (uint32_t x = 0; x < image.width; ++x) {
      line[x * 3 + 0] = char(src[x * 4 + 2]);
      line[x * 3 + 1] = char(src[x * 4 + 1]);
      line[x * 3 + 2] = char(src[x * 4 + 0]);
    }
    out.write(line.data(), row);
  }
  return bool(out);
}

}  // namespace

void PrepareDebugCapture() {
  if (!REXCVAR_GET(capture_dir).empty()) rex::cvar::SetFlagByName("present_capture_readable", "true");
}

void StartDebugCapture(rex::ui::Window* window, rex::system::IGraphicsSystem* graphics) {
  const std::filesystem::path dir = REXCVAR_GET(capture_dir);
  if (dir.empty() || !window || !graphics) return;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  const double interval = REXCVAR_GET(capture_interval);
  REXLOG_INFO("Debug captures to {} every {:.1f} s", dir.string(), interval);
  std::thread([window, graphics, dir, interval] {
    const auto start = std::chrono::steady_clock::now();
    for (int n = 1;; ++n) {
      std::this_thread::sleep_until(start + std::chrono::duration<double>(interval * n));
      rex::ui::RawImage image;
      bool ok = false;
      window->app_context().CallInUIThreadSynchronous([&] {
        if (auto* presenter = graphics->presenter()) ok = presenter->CapturePresentedOutput(image);
      });
      if (!ok || !image.width) continue;
      char name[32];
      std::snprintf(name, sizeof(name), "t%04d.bmp", int(interval * n));
      WriteBmp(dir / name, image);
    }
  }).detach();
}

}  // namespace fm4
