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
    // Detail options: written over the original, switchable, restorable.
    RenderScenarioOptions ultra{false, 2, true, false, false, false, 2, false};
    Require(SetRenderScenarios(root, ultra).empty(), "detail options failed");
    const auto detailed = Get(race);
    Require(detailed.find("<CarKeepLOD0 value=\"1\"/>\r\n") != std::string::npos &&
            detailed.find("<MaxCarsInMirror value=\"16\"/>") != std::string::npos &&
            detailed.find("<EnvMapFrequencyScale value=\"2.0\"/>") != std::string::npos &&
            detailed.find("SkipShadowMapUnlessCockpit") == std::string::npos, "detail options not written");
    Require(ShadowVerificationManifest(root, manifest)[0].path.find(".fm4-shadow-originals/") == 0,
        "original not verified with detail options");
    Require(SetRenderScenarios(root, RenderScenarioOptions{true}).empty() &&
            Get(race).find("CarKeepLOD0") == std::string::npos &&
            Get(race).find("SkipShadowMapUnlessCockpit") != std::string::npos, "switching options failed");
    Require(SetRenderScenarios(root, RenderScenarioOptions{false}).empty() && Get(race) == xml,
        "console values not restored");
    Put(root / "Media/RenderScenarios/8-Car_Race.xml", "malformed");
    Require(!SetRaceShadowWorkaround(root, true).empty(), "malformed XML accepted");
    Require(Get(race) == xml && !RaceShadowWorkaroundEnabled(root), "partial patch survived failure");
    Put(root / "settings.toml", "async_shader_compilation = true\nframe_stats = true\nprofile_name = 'Tester'\n");
    Settings loaded; Require(loaded.Load(root / "settings.toml"), "settings load failed");
    Require(loaded.Save(root / "settings.toml"), "settings migration failed");
    const auto migrated = Get(root / "settings.toml");
    // Pipelines compile before the draw: a dropped draw darkened the asphalt.
    Require(migrated.find("async_shader_compilation = false") != std::string::npos &&
        migrated.find("frame_stats = false") != std::string::npos, "shader compilation or diagnostics not reset");
    loaded.readback_resolve = "fast";
    loaded.async_shader_compilation = true;
    Require(loaded.Save(root / "settings.toml"), "compatibility settings save failed");
    Settings compatibility;
    Require(compatibility.Load(root / "settings.toml") && compatibility.readback_resolve == "none" &&
        !compatibility.async_shader_compilation, "renderer compatibility baseline not enforced");
    Require(loaded.profile_name == "Tester", "profile lost during migration");
    Require(!loaded.experimental_120fps,"60 Hz is not the default");
    loaded.experimental_120fps=true;
    loaded.vsync=true;
    Require(loaded.Save(root / "settings.toml"),"120 Hz settings save failed");
    Settings high_refresh;Require(high_refresh.Load(root / "settings.toml") && high_refresh.experimental_120fps,
        "120 Hz mode did not survive reload");
    Require(high_refresh.vsync && Get(root / "settings.toml").find("vsync = true")!=std::string::npos,"VSync on did not survive reload");
    if (high_refresh.gpu_backend == "vulkan") {
      for (const std::string mode : {"immediate", "mailbox", "fifo_relaxed"})
        Require(Get(root / "settings.toml").find("vulkan_allow_present_mode_" + mode + " = false") != std::string::npos,
            "VSync on must disable every non-FIFO presentation mode");
    }
    high_refresh.vsync = false;
    Require(high_refresh.Save(root / "settings.toml"), "VSync off save failed");
    if (high_refresh.gpu_backend == "vulkan") {
      for (const std::string mode : {"immediate", "mailbox", "fifo_relaxed"})
        Require(Get(root / "settings.toml").find("vulkan_allow_present_mode_" + mode + " = true") != std::string::npos,
            "VSync off must restore automatic presentation selection");
    }
    Settings unsynced;
    Require(unsynced.Load(root / "settings.toml") && !unsynced.vsync && unsynced.experimental_120fps, "VSync off did not survive reload independently of FPS");
    high_refresh.experimental_120fps=false;Require(high_refresh.Save(root / "settings.toml"),"60 Hz restore failed");
    Settings standard;Require(standard.Load(root / "settings.toml") && !standard.experimental_120fps,"120 Hz remained enabled");
    high_refresh.ResetDisplay();Require(!high_refresh.experimental_120fps,"Display reset kept experimental mode");
    for (const std::string mode : {"auto", "stereo", "surround"}) {
      loaded.audio_output_mode = mode;
      Require(loaded.Save(root / "settings.toml"), "audio settings save failed");
      Settings audio;
      Require(audio.Load(root / "settings.toml") && audio.audio_output_mode == mode,
          "audio output mode did not survive reload");
    }
    loaded.video_mode_width = 1920;
    loaded.video_mode_height = 1080;
    loaded.present_effect = "fsr";
    loaded.present_fsr_sharpness_reduction = 0.15;
    Require(loaded.Save(root / "settings.toml"), "visual preset save failed");
    Settings visuals;
    Require(visuals.Load(root / "settings.toml") && visuals.video_mode_width == 1280 &&
        visuals.video_mode_height == 720 && visuals.present_effect == "fsr" &&
        visuals.present_fsr_sharpness_reduction == 0.2, "fixed Xenia guest resolution not restored");
    for (int preset = 0; preset < 3; ++preset) {
      loaded.ApplyGraphicsPreset(preset);
      Require(loaded.Save(root / "settings.toml"), "preset save failed");
      Settings reloaded;
      Require(reloaded.Load(root / "settings.toml") && reloaded.graphics_preset == preset &&
          reloaded.resolution_scale == (preset == 0 ? 1 : 2) &&
          reloaded.anisotropic_override == 3 + preset &&
          reloaded.present_effect == (preset == 2 ? "bilinear" : "fsr"),
          "preset values changed after reload");
      Require(reloaded.experimental_120fps == loaded.experimental_120fps, "preset changed frame rate");
    }
    loaded.ApplyGraphicsPreset(3);
    loaded.resolution_scale = 3;
    loaded.anisotropic_override = 2;
    loaded.present_effect = "cas";
    loaded.experimental_120fps = true;
    Require(loaded.Save(root / "settings.toml"), "custom save failed");
    Settings custom;
    Require(custom.Load(root / "settings.toml") && custom.graphics_preset == 3 &&
        custom.resolution_scale == 3 && custom.anisotropic_override == 2 &&
        custom.present_effect == "cas" && custom.experimental_120fps,
        "custom choices or 120 FPS lost on reload");
    const auto physics = root / "Media/physics/PhysicsSettings.ini";
    Put(physics, "TimeScale 1\r\nCarPhysicsUpdateHz 360\r\n");
    const auto original_physics = Get(physics);
    Require(SetPlus120Physics(root, true).empty() && Get(physics) == original_physics,
        "original game physics modified");
    Put(root / ".fm4-plus-ready", "Normal");
    Require(SetPlus120Physics(root, true).empty(), "120 physics failed");
    const auto high_physics = Get(physics);
    Require(high_physics.find("CarPhysicsUpdateHz 695") != std::string::npos,
        "supplied 120 physics missing");
    Require(SetPlus120Physics(root, true).empty() && Get(physics) == high_physics,
        "120 physics not idempotent");
    std::vector<ManifestEntry> physics_manifest{{"Media/physics/PhysicsSettings.ini", original_physics.size(), "sha"}};
    Require(PhysicsVerificationManifest(root, physics_manifest)[0].path == ".fm4-120-physics-original.ini",
        "physics original not verified");
    Put(physics, high_physics + "external");
    Require(!SetPlus120Physics(root, false).empty(), "external physics overwritten");
    Require(PhysicsVerificationManifest(root, physics_manifest)[0].path == physics_manifest[0].path,
        "external physics concealed");
    Put(physics, high_physics);
    Require(SetPlus120Physics(root, false).empty() && Get(physics) == original_physics,
        "60 FPS physics restoration failed");
    fs::remove_all(root);
    std::cout << "Race workaround, UI preservation, restoration, verification, errors and settings: OK\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << " (fixture: " << root << ")\n"; return 1;
  }
}
