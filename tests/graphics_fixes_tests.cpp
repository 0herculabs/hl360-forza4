#include "graphics_fixes.h"
#include "settings.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace launcher;
namespace fs = std::filesystem;
static void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static void Put(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << s;
}
static std::string Get(const fs::path& p) {
  std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}};
}
int main() {
  const auto root = fs::current_path() / ("fm4-graphics-test-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  try {
    const auto race = root / "Media/RenderScenarios/16-Car_Race.xml";
    const auto ui = root / "Media/RenderScenarios/UI_Car.xml";
    const std::string xml = "<DynamicRenderSettings>\r\n  <FrameRate value=\"60\"/>\r\n</DynamicRenderSettings>\r\n";
    Put(race, xml); Put(ui, xml);
    std::vector<ManifestEntry> manifest{{"Media/RenderScenarios/16-Car_Race.xml", xml.size(), "sha"},
                                      {"Media/RenderScenarios/UI_Car.xml", xml.size(), "sha"}};
    const auto enable_error = SetRaceShadowWorkaround(root, true);
    if (!enable_error.empty()) throw std::runtime_error("enable failed: " + enable_error);
    Require(RaceShadowWorkaroundEnabled(root), "active marker missing");
    const auto patched = Get(race);
    Require(patched.find("SkipShadowMapUnlessCockpit") != std::string::npos, "race patch missing");
    Require(Get(ui) == xml, "UI modified");
    Require(ShadowVerificationManifest(root, manifest)[0].path.find(".fm4-shadow-originals/") == 0,
        "original not verified");
    Require(SetRaceShadowWorkaround(root, true).empty() && Get(race) == patched, "enable not idempotent");
    Put(race, patched + "external change");
    Require(ShadowVerificationManifest(root, manifest)[0].path == manifest[0].path,
        "external change concealed from verification");
    Require(!SetRaceShadowWorkaround(root, false).empty(), "external edit overwritten");
    Put(race, patched);
    Require(SetRaceShadowWorkaround(root, false).empty(), "restore failed");
    Require(Get(race) == xml && !RaceShadowWorkaroundEnabled(root), "restore not byte exact");
    Put(root / "Media/RenderScenarios/8-Car_Race.xml", "malformed");
    Require(!SetRaceShadowWorkaround(root, true).empty(), "malformed XML accepted");
    Require(Get(race) == xml && !RaceShadowWorkaroundEnabled(root), "partial patch survived failure");
    Put(root / "settings.toml", "async_shader_compilation = true\nframe_stats = true\nprofile_name = 'Tester'\n");
    Settings loaded; Require(loaded.Load(root / "settings.toml"), "settings load failed");
    Require(loaded.Save(root / "settings.toml"), "settings migration failed");
    const auto migrated = Get(root / "settings.toml");
    Require(migrated.find("async_shader_compilation = false") != std::string::npos &&
        migrated.find("frame_stats = false") != std::string::npos, "experimental settings not reset");
    Require(loaded.profile_name == "Tester", "profile lost during migration");
    Require(!loaded.experimental_120fps,"60 Hz is not the default");
    loaded.experimental_120fps=true;
    Require(loaded.Save(root / "settings.toml"),"120 Hz settings save failed");
    Settings high_refresh;Require(high_refresh.Load(root / "settings.toml") && high_refresh.experimental_120fps,
        "120 Hz mode did not survive reload");
    Require(Get(root / "settings.toml").find("vsync = true")!=std::string::npos,"120 Hz disabled vblank pacing");
    high_refresh.experimental_120fps=false;Require(high_refresh.Save(root / "settings.toml"),"60 Hz restore failed");
    Settings standard;Require(standard.Load(root / "settings.toml") && !standard.experimental_120fps,"120 Hz remained enabled");
    high_refresh.ResetDisplay();Require(!high_refresh.experimental_120fps,"Display reset kept experimental mode");
    fs::remove_all(root);
    std::cout << "Race workaround, UI preservation, restoration, verification, errors and settings: OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << " (fixture: " << root << ")\n"; return 1;
  }
}
