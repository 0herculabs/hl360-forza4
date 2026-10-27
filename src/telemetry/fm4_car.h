#pragma once

namespace fm4::telemetry {
// The game's own "player is driving" flag (sub_823A3F68); telemetry sends
// race off while it is clear (menus, pause, replays).
void NoteDriving(bool driving);
}  // namespace fm4::telemetry
