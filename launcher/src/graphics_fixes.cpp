#include "graphics_fixes.h"
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace launcher {
namespace fs = std::filesystem;
namespace {
constexpr char kBackup[] = ".fm4-shadow-originals";
constexpr char kTag[] = "  <SkipShadowMapUnlessCockpit value=\"1\"/>\n";
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
std::string Patched(const std::string& original) {
  if (original.find("SkipShadowMapUnlessCockpit") != std::string::npos) return original;
  const auto end = original.find("</DynamicRenderSettings>");
  if (original.find("<DynamicRenderSettings>") == std::string::npos || end == std::string::npos)
    throw std::runtime_error("Unexpected RenderScenarios XML format");
  auto text = original;
  text.insert(end, kTag);
  return text;
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
bool RaceShadowWorkaroundEnabled(const fs::path& game) {
  return fs::exists(game / kBackup / "active");
}
std::string SetRaceShadowWorkaround(const fs::path& game, bool enabled) {
  struct Change { fs::path file; std::string before, after; };
  std::vector<Change> changes;
  try {
    const auto dir = game / "Media/RenderScenarios";
    CheckPath(game, dir);
    CheckPath(game, game / kBackup);
    for (const auto& e : fs::directory_iterator(dir)) {
      if (!e.is_regular_file() || !RaceFile(e.path())) continue;
      const auto relative = fs::path("Media/RenderScenarios") / e.path().filename();
      const auto backup = game / kBackup / relative;
      CheckPath(game, e.path()); CheckPath(game, backup);
      const auto live = Read(e.path());
      if (enabled) {
        const auto original = fs::exists(backup) ? Read(backup) : live;
        const auto patched = Patched(original);
        if (live != original && live != patched)
          throw std::runtime_error("Externally modified file; restore it first: " + e.path().u8string());
        if (patched == original) continue;
        if (!fs::exists(backup)) Write(backup, original);
        changes.push_back({e.path(), live, patched});
      } else if (fs::exists(backup)) {
        const auto original = Read(backup);
        if (live != original && live != Patched(original))
          throw std::runtime_error("Externally modified file; restore it first: " + e.path().u8string());
        changes.push_back({e.path(), live, original});
      }
    }
    if (enabled && changes.empty()) throw std::runtime_error("No supported race render scenarios found");
    for (const auto& c : changes) Write(c.file, c.after);
    if (enabled) Write(game / kBackup / "active", "FM4 race shadow workaround v1\n");
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
      if (fs::exists(backup) && Read(game / relative) == Patched(Read(backup)))
        entry.path = std::string(kBackup) + "/" + entry.path;
    } catch (...) { /* Verification must report inaccessible or modified files. */ }
  }
  return out;
}
}
