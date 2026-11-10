// fm4 - cheats from the launcher (src/cheats.cpp).

#pragma once

namespace rex {
class Runtime;
}

namespace fm4::cheats {

// Lets the unicorn cars into the car lists when cheat_unicorn_dealer is on.
// Call after the XEX is loaded and before the module launches.
void PatchImage(rex::Runtime* runtime);

}  // namespace fm4::cheats
