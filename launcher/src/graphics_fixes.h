#pragma once
#include "installer.h"
namespace launcher {
std::string SetPlus120Physics(const std::filesystem::path& game, bool enabled);
std::vector<ManifestEntry> PhysicsVerificationManifest(const std::filesystem::path& game,
    const std::vector<ManifestEntry>& manifest);

// Settings written into the race render scenarios (Media/RenderScenarios),
// the values fm4-static-recomp forces through the game's override layer.
struct RenderScenarioOptions {
  bool skip_exterior_shadows = true;  // race shadow workaround: cockpit shadows only
  int car_detail = 0;                 // 0 console, 1 high, 2 ultra
  bool full_rate_mirror = false;      // every frame, up to 16 cars, cones and barriers
  bool full_rate_bloom = false;       // 16-car races skip alternate frames
  bool soft_particles = false;
  bool scenery_shadows = false;       // cones and barriers
  int reflections = 0;                // 0 console, 1 every race, 2 double rate
  bool crowds_every_race = false;     // 1- and 16-car races have none
  bool no_msaa = false;               // a temporal upscaler smooths edges itself
  bool Any() const;  // anything not at the console's values
};

bool RaceShadowWorkaroundEnabled(const std::filesystem::path& game);
// Rewrites the race scenarios from their originals with `options`; all of
// them at the console's values restores the originals. Empty on success.
std::string SetRenderScenarios(const std::filesystem::path& game, const RenderScenarioOptions& options);
// The workaround alone (other options at the console's values).
std::string SetRaceShadowWorkaround(const std::filesystem::path& game, bool enabled);
std::vector<ManifestEntry> ShadowVerificationManifest(const std::filesystem::path& game,
    const std::vector<ManifestEntry>& manifest);
}
