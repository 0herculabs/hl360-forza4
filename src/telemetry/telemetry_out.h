#pragma once

#include "forza_dash.h"

namespace fm4::telemetry {

// Whether telemetry_udp is on, so the per-frame car read runs for it.
bool Wanted();
// telemetry_udp_moving_only: send "race off" while the car stands still.
bool MovingOnly();
// telemetry_log: write the values sent to the game log every few seconds.
bool LogValues();

// Sends one sample as a Forza Data Out packet to telemetry_udp_target.
void Send(const CarSample& sample);

}  // namespace fm4::telemetry
