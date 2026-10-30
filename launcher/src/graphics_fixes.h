#pragma once
#include "installer.h"
namespace launcher {
std::string SetPlus120Physics(const std::filesystem::path& game, bool enabled);
std::vector<ManifestEntry> PhysicsVerificationManifest(const std::filesystem::path& game,
    const std::vector<ManifestEntry>& manifest);
bool RaceShadowWorkaroundEnabled(const std::filesystem::path& game);
std::string SetRaceShadowWorkaround(const std::filesystem::path& game, bool enabled);
std::vector<ManifestEntry> ShadowVerificationManifest(const std::filesystem::path& game,
    const std::vector<ManifestEntry>& manifest);
}
