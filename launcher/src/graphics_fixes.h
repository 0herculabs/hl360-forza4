#pragma once
#include "installer.h"
namespace launcher {
bool RaceShadowWorkaroundEnabled(const std::filesystem::path& game);
std::string SetRaceShadowWorkaround(const std::filesystem::path& game, bool enabled);
std::vector<ManifestEntry> ShadowVerificationManifest(const std::filesystem::path& game,
    const std::vector<ManifestEntry>& manifest);
}
