#include "graphics_fixes.h"
#include "plus_physics_profile.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace launcher {
namespace fs = std::filesystem;
namespace {
constexpr char kBackup[] = ".fm4-shadow-originals";
// The active marker: v1 (the workaround alone) or v2 with the options.
constexpr char kMarkerV2[] = "FM4 render scenarios v2\n";
std::string Read(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  if (!f) throw std::runtime_error("Cannot read " + p.u8string());
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
void Write(const fs::path& p, const std::string& text) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary | std::ios::trunc);
  if (!f || !f.write(text.data(), text.size()) || !f.flush())
    throw std::runtime_error("Cannot write " + p.u8string());
}
bool RaceFile(const fs::path& p) {
  const auto name = p.filename().u8string();
  if (p.extension() != ".xml" || name.rfind("UI_", 0) == 0) return false;
  for (const std::string suffix : {"_Race.xml", "_PreRace.xml", "_Replay.xml", "_321_and_PostRace.xml"}) {
    if (name.size() >= suffix.size() && name.compare(name.size()-suffix.size(), suffix.size(), suffix) == 0)
      return true;
  }
  return false;
}
// Sets <tag .../> to `attributes`, replacing the file's element or adding one.
void SetTag(std::string& text, const std::string& tag, const std::string& attributes) {
  const std::string element = "<" + tag + " " + attributes + "/>";
  for (size_t at = text.find("<" + tag); at != std::string::npos; at = text.find("<" + tag, at + 1)) {
    const char next = at + tag.size() + 1 < text.size() ? text[at + tag.size() + 1] : '\0';
    if (next != ' ' && next != '/' && next != '\t') continue;  // a longer tag name
    const auto close = text.find("/>", at);
    if (close == std::string::npos) throw std::runtime_error("Unexpected RenderScenarios XML format");
    text.replace(at, close + 2 - at, element);
    return;
  }
  const auto end = text.find("</DynamicRenderSettings>");
  const std::string eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
  text.insert(end, "  " + element + eol);
}
// Sets a tiling control the file has (TilingScenario, UITilingScenario).
void SetTilingIfPresent(std::string& text, const std::string& tag, int value) {
  if (text.find("<" + tag + " ") != std::string::npos)
    SetTag(text, tag, "value=\"" + std::to_string(value) + "\"");
}
std::string Patched(const std::string& original, const RenderScenarioOptions& o, const std::string& name) {
  const auto end = original.find("</DynamicRenderSettings>");
  if (original.find("<DynamicRenderSettings>") == std::string::npos || end == std::string::npos)
    throw std::runtime_error("Unexpected RenderScenarios XML format");
  auto text = original;
  // Byte for byte as earlier launchers wrote it (always "\n"), so the files of
  // existing installs are still recognised as the launcher's.
  if (o.skip_exterior_shadows && text.find("SkipShadowMapUnlessCockpit") == std::string::npos)
    text.insert(end, "  <SkipShadowMapUnlessCockpit value=\"1\"/>\n");
  // Values from fm4-static-recomp (src/game_hooks.cpp).
  if (o.car_detail == 1) {
    SetTag(text, "CarLODThresholds", "lod1=\"3\" lod2=\"5\" count=\"7\"");
    SetTag(text, "CarLODTransDist", "near=\"20.0\" far=\"35.0\" veryfar=\"70.0\"");
  } else if (o.car_detail == 2) {
    // Other cars keep the game's LOD range, so far away they still reach LOD3
    // and drop the projected shadow the game does not expect there.
    SetTag(text, "CarLODThresholds", "lod1=\"4\" lod2=\"6\" count=\"8\"");
    SetTag(text, "CarLODTransDist", "near=\"30.0\" far=\"60.0\" veryfar=\"120.0\"");
    SetTag(text, "CarMinWheelLOD", "value=\"0\"");
    SetTag(text, "CarKeepLOD0", "value=\"1\"");
    SetTag(text, "CarForceNoLOD0", "value=\"0\"");
    SetTag(text, "CarFocusLODMinMax", "min=\"0\" max=\"0\"");
  }
  if (o.full_rate_mirror) {
    SetTag(text, "HalfRateMirror", "value=\"0\"");
    SetTag(text, "MaxCarsInMirror", "value=\"16\"");
    SetTag(text, "CollidablesInMirror", "value=\"1\"");
  }
  if (o.full_rate_bloom) SetTag(text, "HalfRateBloom", "value=\"0\"");
  if (o.soft_particles) SetTag(text, "SoftParticles", "value=\"1\"");
  if (o.scenery_shadows) SetTag(text, "CollidableShadows", "value=\"1\"");
  if (o.reflections == 1) SetTag(text, "EnvMapFrequencyScale", "value=\"1.0\"");
  else if (o.reflections == 2) SetTag(text, "EnvMapFrequencyScale", "value=\"2.0\"");
  if (o.crowds_every_race) SetTag(text, "CrowdLoad", "value=\"1\"");
  // The game's tiling table: 0 is 4x MSAA in three tiles, 1 2x in two, 2 none
  // in one. Split screen keeps its own 1280x360 tiling.
  if (o.no_msaa && name.rfind("Splitscreen", 0) != 0) {
    SetTilingIfPresent(text, "TilingScenario", 2);
    SetTilingIfPresent(text, "UITilingScenario", 2);
  }
  return text;
}
std::string Serialize(const RenderScenarioOptions& o) {
  return std::string(kMarkerV2) + std::to_string(o.skip_exterior_shadows) + " " + std::to_string(o.car_detail) +
         " " + std::to_string(o.full_rate_mirror) + " " + std::to_string(o.full_rate_bloom) + " " +
         std::to_string(o.soft_particles) + " " + std::to_string(o.scenery_shadows) + " " +
         std::to_string(o.reflections) + " " + std::to_string(o.crowds_every_race) + " " +
         std::to_string(o.no_msaa) + "\n";
}
// The options the live files were written with (the active marker).
RenderScenarioOptions Applied(const fs::path& game) {
  RenderScenarioOptions o;
  const auto marker = game / kBackup / "active";
  if (!fs::exists(marker)) return {false};
  const auto text = Read(marker);
  if (text.rfind(kMarkerV2, 0) != 0) return o;  // v1: the workaround alone
  int v[9] = {};  // no_msaa (the ninth) came later
  if (std::sscanf(text.c_str() + sizeof(kMarkerV2) - 1, "%d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2],
                  &v[3], &v[4], &v[5], &v[6], &v[7], &v[8]) < 8)
    throw std::runtime_error("Unreadable render scenario marker: " + marker.u8string());
  return {v[0] != 0, v[1], v[2] != 0, v[3] != 0, v[4] != 0, v[5] != 0, v[6], v[7] != 0, v[8] != 0};
}
void CheckPath(const fs::path& root, const fs::path& path) {
  const auto base = fs::canonical(root);
  const auto relative = path.lexically_normal().lexically_relative(root.lexically_normal());
  if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
    throw std::runtime_error("Path leaves the game folder: " + path.u8string());
  // Canonicalize existing components only. Windows weakly_canonical may try
  // to probe inaccessible ancestors when the final backup path doesn't exist.
  auto probe = root;
  for (const auto& component : relative) {
    probe /= component;
    if (!fs::exists(probe)) break;
    const auto resolved = fs::canonical(probe).lexically_relative(base);
    if (resolved.empty() || resolved.is_absolute() || *resolved.begin() == "..")
      throw std::runtime_error("Path leaves the game folder: " + path.u8string());
  }

}
}
std::string SetPlus120Physics(const fs::path& game, bool enabled) {
  try {
    if (!fs::exists(game / ".fm4-plus-ready")) return {};
    const auto live = game / "Media/physics/PhysicsSettings.ini";
    const auto backup = game / ".fm4-120-physics-original.ini";
    CheckPath(game, live); CheckPath(game, backup);
    const auto before = Read(live);
    if (fs::exists(backup)) {
      const auto original = Read(backup);
      if (before != original && before != kPlus120Physics)
        throw std::runtime_error("PhysicsSettings.ini was externally modified; refusing to overwrite it");
      if (!enabled) { Write(live, original); return {}; }
    } else {
      if (!enabled) return {};
      Write(backup, before);
    }
    if (before != kPlus120Physics) Write(live, kPlus120Physics);
    return {};
  } catch (const std::exception& e) { return e.what(); }
}
std::vector<ManifestEntry> PhysicsVerificationManifest(const fs::path& game,
    const std::vector<ManifestEntry>& manifest) {
  auto out = manifest;
  if (!fs::exists(game / ".fm4-plus-ready")) return out;
  try {
    const auto live = game / "Media/physics/PhysicsSettings.ini";
    const auto backup = game / ".fm4-120-physics-original.ini";
    CheckPath(game, live); CheckPath(game, backup);
    if (fs::exists(backup) && Read(live) == kPlus120Physics)
      for (auto& entry : out)
        if (fs::path(entry.path) == fs::path("Media/physics/PhysicsSettings.ini"))
          entry.path = ".fm4-120-physics-original.ini";
  } catch (...) { /* Report modifications through normal verification. */ }
  return out;
}
bool RenderScenarioOptions::Any() const {
  return skip_exterior_shadows || car_detail || full_rate_mirror || full_rate_bloom || soft_particles ||
         scenery_shadows || reflections || crowds_every_race || no_msaa;
}
bool RaceShadowWorkaroundEnabled(const fs::path& game) {
  return fs::exists(game / kBackup / "active");
}
std::string SetRaceShadowWorkaround(const fs::path& game, bool enabled) {
  return SetRenderScenarios(game, RenderScenarioOptions{enabled});
}
std::string SetRenderScenarios(const fs::path& game, const RenderScenarioOptions& options) {
  struct Change { fs::path file; std::string before, after; };
  std::vector<Change> changes;
  const bool enabled = options.Any();
  try {
    const auto dir = game / "Media/RenderScenarios";
    CheckPath(game, dir);
    CheckPath(game, game / kBackup);
    const auto applied = Applied(game);
    bool found = false;
    for (const auto& e : fs::directory_iterator(dir)) {
      if (!e.is_regular_file() || !RaceFile(e.path())) continue;
      found = true;
      const auto relative = fs::path("Media/RenderScenarios") / e.path().filename();
      const auto backup = game / kBackup / relative;
      CheckPath(game, e.path()); CheckPath(game, backup);
      const auto live = Read(e.path());
      const auto original = fs::exists(backup) ? Read(backup) : live;
      // The live file must be the original or what the launcher last wrote.
      if (live != original && live != Patched(original, applied, e.path().filename().u8string()))
        throw std::runtime_error("Externally modified file; restore it first: " + e.path().u8string());
      const auto wanted = enabled ? Patched(original, options, e.path().filename().u8string()) : original;
      if (enabled && wanted != original && !fs::exists(backup)) Write(backup, original);
      if (wanted != live) changes.push_back({e.path(), live, wanted});
    }
    if (enabled && !found) throw std::runtime_error("No supported race render scenarios found");
    for (const auto& c : changes) Write(c.file, c.after);
    if (enabled) Write(game / kBackup / "active", Serialize(options));
    else fs::remove(game / kBackup / "active");
    return {};
  } catch (const std::exception& e) {
    std::string error = e.what();
    for (const auto& c : changes) {
      try { Write(c.file, c.before); }
      catch (const std::exception& rollback) { error += "\nRollback: " + std::string(rollback.what()); }
    }
    return error;
  }
}
std::vector<ManifestEntry> ShadowVerificationManifest(const fs::path& game,
    const std::vector<ManifestEntry>& manifest) {
  auto out = manifest;
  for (auto& entry : out) {
    const fs::path relative = fs::u8path(entry.path);
    if (relative.parent_path() != fs::path("Media/RenderScenarios") || !RaceFile(relative)) continue;
    const auto backup = game / kBackup / relative;
    try {
      CheckPath(game, backup);
      if (fs::exists(backup) && Read(game / relative) == Patched(Read(backup), Applied(game), relative.filename().u8string()))
        entry.path = std::string(kBackup) + "/" + entry.path;
    } catch (...) { /* Verification must report inaccessible or modified files. */ }
  }
  return out;
}
}
