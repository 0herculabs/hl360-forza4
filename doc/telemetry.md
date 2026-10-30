# SimHub telemetry

The game can send the player's car over UDP in Forza's "Data Out" layout, so
SimHub, dashboards and bass shakers work as they do with Forza Motorsport 7 or
Forza Horizon 4/5. Set it up in the launcher's **Wheel** tab, **Telemetry**:

| Setting | Default | What it does |
| --- | --- | --- |
| `telemetry_udp` | `false` | Send telemetry. |
| `telemetry_udp_format` | `fm7` | `fm7` (FM7 Dash, 311 bytes) or `horizon` (FH4/5, 324 bytes). Pick the same game in SimHub. |
| `telemetry_udp_target` | `127.0.0.1:5300` | Where packets go, `host:port`. |
| `telemetry_udp_moving_only` | `false` | Send "race off" while the car stands still. |
| `telemetry_log` | `true` | While telemetry is on, write the values sent to the game log every 5 s. |

Menus, pause and replays send "race off": the game's own driving flag
(`sub_823A3F68`, object `+5`) is clear and the car is not moving.

## Fields sent

RPM, idle RPM, redline, gear, speed, forward velocity, throttle, brake,
clutch, handbrake, steering, and per wheel: rotation speed, slip ratio, slip
angle, combined slip, suspension travel (metres and normalised over the range
seen so far). Surface rumble, position, orientation, acceleration and lap data
are not sent yet (zero).

## Where the values come from

The encoder and sender are the Pinyon Shift (Forza Horizon 1) telemetry code.
FH1 is built on FM4's engine, so FM4's offsets were found by matching FM4's
code to the FH1 functions in Pinyon Shift's findings (`site/findings/objects.toml`):
each function's instructions are compared with offsets, immediates and branch
targets masked, and matched pairs are aligned instruction by instruction, so an
FH1 offset maps to the FM4 offset in the same instruction.

| What | FH1 | FM4 | How it matched |
| --- | --- | --- | --- |
| Rumble builder (per frame, the pad's cars at `r3 + 0x68`) | `sub_8248AB08` | `sub_826DB940` | 0.60 similarity, no other candidate above 0.25 |
| Player car slot getter | `sub_8248BAE8` | `sub_826DC8A0` | identical after masking |
| Car dynamics object | `[car + 0x2D90]` | `[car + 0x2D50]` | getter `sub_8249E4B8` / `sub_82357940` at the same call site |
| Driver type (0 the player) | `+0x2DD0` | `+0x2D90` | neighbour `+0x2DCC` / `+0x2D8C` read by the same builder call; compared with 0 in both |
| Dynamics vtable | `0x8223759C` (339 slots) | `0x820D0B5C` (315 slots) | slot-by-slot method alignment, 0.40 (next unrelated table 0.06) |
| Engine speed, limiter, idle (rad/s) | `+0x144`, `+0x1C8`, `+0x4CC` | same | engine getters identical |
| Redline (rad/s) | `+0x1C4` | `+0x1C4` (assumed; neighbours unchanged) | |
| Gear byte (0 reverse, 9 neutral in FH1) | `+0x8C0` | `+0x8BC` | gear getter |
| Speed (m/s) | `+0xC38` | `+0xC18` | vtable slot `0x158` / `0x124` |
| Controls (gas, brake, steer, clutch, handbrake) | `+0xF40` | `+0xE90` | getter returning the block; layout assumed, values range-checked |
| Wheel blocks | `+0xF60`, stride `0x400` | `+0xEB0`, stride `0x420` | per-wheel getters `mulli r4, 0x420` |
| Wheel: suspension, speed | `+0x0`, `+0xC` | `+0x0`, `+0xC` | getters and suspension model |
| Wheel: slip ratio, slip angle | `+0x1F0`, `+0x208` | `+0x210`, `+0x228` | per-wheel getters (FH1 slot − `0x38` = FM4 slot) |
| Wheel: contact, spring rest | `+0x31C`, `+0x35C` | `+0x33C`, `+0x37C` | suspension model `sub_82D2EC70` / `sub_8230BE18` |

Nothing here has been checked against a drive yet. With `telemetry_log` on,
the log line `Telemetry: rpm … gear … (raw …) speed …` shows what is read:
compare it with the in-game dashboard (RPM, gear, speed) and the pedals. If
the controls are out of range they are not sent and the line says so.

The matching scripts are in `tools/fh1-match` (Python 3 with `capstone`).
They work on `fm4tool image <game_root> default.xex <dir>` dumps of both games
(FH1 base build, FM4 Rev 1):

```
python tools/fh1-match/match_funcs.py <fh1 dir> <fm4 dir> 8248AB08
python tools/fh1-match/align_funcs.py <fh1 dir> <fm4 dir> 8248AB08 826DB940
python tools/fh1-match/match_vtable.py <fh1 dir> <fm4 dir> 8223759C 339 --map --slots 0xd4,0x158
```

The first drive confirmed RPM, gear, speed and the pedals in SimHub.
