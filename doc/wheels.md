# Racing wheels, separate USB devices and native FFB

The wheel support is the Pinyon Shift (Forza Horizon 1) racing wheel work
brought to FM4: the same Wheel screen, binding syntax and settings,
on top of FM4's own native force feedback hooks.

- Steering, pedals, buttons and an H-shifter can each come from a different USB
  device: a direct-drive base, a separate pedal set, a shifter, a handbrake or
  a button box. They reach the game as one Xbox 360 racing wheel (XInput
  subtype 2) on player one.
- Force feedback is the game's own: FM4 sends its wheel effects (constant
  force, spring, damper, friction, inertia) through XInput-FF, and the port
  plays them as SDL haptic effects on the force device. Controller rumble is
  played as a vibration on top, if the device supports it.
- SimHub telemetry: see [telemetry.md](telemetry.md).

Devices are raw SDL joysticks, so no brand list is needed. Close tools that
take the wheel exclusively before starting the game.

## Setup (launcher, **Wheel** tab)

1. Set the rotation in your wheel software (Fanatec, MOZA Pit House, ...).
2. Turn on **Enable Wheel Support**. On the same line, set **Wheel rotation**
   (as set in the wheel software) and **In-game rotation** (how far the
   game's steering wheel turns; match it by eye in cockpit view, lower it if
   the game's wheel turns further than yours; Off maps your wheel's full
   rotation to full lock).
3. Check the live view (it stays at the top while the cards below open and close): the wheel, pedal bars and button chips show exactly
   what the game will read. **Bind** on a row binds just that control, **+**
   adds another input for the same button, **x** clears it, **Invert**
   reverses an axis.
4. **Force Feedback**: pick the device that receives the forces (by default
   the wheel base), press **Test** (left, then right, as hard as **Strength**:
   100 % is half the base's maximum, 200 % all of it), and tick
   **Reverse force direction** if it pulled right first. Start with a low
   **Strength** on a direct-drive base.
5. **Deadzones**: steering sensitivity and dead zone, dead zones per pedal with
   a live meter beside them, and the button
   the clutch pedal presses. FM4's clutch is a button: select **Manual with
   clutch** in the game and match the button to its controls.

Everything is saved to `fm4.user.toml` as it changes and applies from the
next game start. Older FM4 wheel settings (calibrated axes and button
numbers) are carried over the first time the launcher reads them.

Some bases appear twice (a Fanatec CSL DD has two HID interfaces with the
same `0eb7:0004`). Bindings name such a device by its path; if Test does not
move the wheel, pick the other entry.

## Settings

Devices are named by `vvvv:pppp` (USB vendor and product, hex), part of the
name, or `path:<SDL path>`. Inputs are `[device|]input`; without a device they
are on the wheel base (`fm4_wheel_device`).

These were `wheel_*` up to 0.3.11, the same names as the ShiftGlue SDK's own
wheel driver, which took them over (FFB at about 1 %). FM4 keeps its own
driver: the SDK's is switched off in `patches/shiftglue`, and the launcher
carries the old keys over.

| Setting | Default | What it does |
| --- | --- | --- |
| `fm4_wheel_enabled` | `false` | Use the wheel bindings for player one. |
| `fm4_wheel_device` | `""` | The wheel base; empty for the first device SDL types as a wheel. |
| `fm4_wheel_steering_axis`, `fm4_wheel_steering_invert` | `0`, `false` | Steering axis on the base. |
| `fm4_wheel_rotation`, `fm4_wheel_game_rotation` | `900`, `0` | The rotation set in the wheel's software and the in-game steering wheel's, degrees lock to lock. With `fm4_wheel_game_rotation` set, full lock comes when the wheel has turned that far (900 on 360: 250 % sensitivity), so the cockpit wheel follows the real one; `0` maps the wheel's full rotation to full lock. |
| `fm4_wheel_steering_scale`, `fm4_wheel_steering_deadzone` | `100`, `0` | Sensitivity and dead zone, percent. |
| `fm4_wheel_throttle`, `fm4_wheel_brake`, `fm4_wheel_clutch` | `""` | Pedal axes, `[device|][+|-]axisN` (`-`: the pedal rests at the top). |
| `fm4_wheel_buttons` | the hat as the D-pad | `NAME=input,...`: `N` a button, `hN.up` a hat direction, `[+|-]axisN` an axis past half travel. NAME is A, B, X, Y, LB, RB, LT, RT, LS, RS, BACK, START, UP, DOWN, LEFT, RIGHT, GEAR1-GEAR7 / REVERSE, or a camera action (CAM_FOV_IN, CAM_FOV_OUT, CAM_FOV_RESET, CAM_SEAT_UP, CAM_SEAT_DOWN, CAM_SEAT_FORWARD, CAM_SEAT_BACK, CAM_SEAT_RESET; see [camera](camera.md)). A NAME may appear more than once. |
| `fm4_wheel_clutch_button` | `B` | Buttons the clutch pedal presses past half travel. |
| `fm4_wheel_clutch_analog` | `false` | Also send the clutch as the right stick's Y axis (FH1's analog clutch; not confirmed in FM4). |
| `fm4_wheel_pedal_deadzone`, `fm4_wheel_{throttle,brake,clutch}_deadzone_{bottom,top}` | `3`, `-1`/`0` | Pedal dead zones, percent; bottom `-1` uses `fm4_wheel_pedal_deadzone`. |
| `fm4_wheel_ffb_device` | `""` | Device that receives forces; empty for the wheel base. |
| `fm4_wheel_ffb_mode` | `on` | `on` (the game's effects), `rumble` (vibration only), `off`. |
| `fm4_wheel_ffb_strength` | `25` | Overall strength, percent (0-200; above 100 the strongest forces clip at the base's maximum). |
| `fm4_wheel_ffb_min_force` | `0` | Lift small forces for geared or belted wheels. |
| `fm4_wheel_ffb_spring`, `_damper`, `_friction`, `_inertia` | `100` | Each of the game's condition effects, percent (0-200). |
| `fm4_wheel_ffb_rumble`, `_rumble_engine`, `_rumble_road` | `50`, `100`, `100` | The game's rumble as a vibration, and each motor's share. |
| `fm4_wheel_ffb_invert` | `false` | Reverse the force direction. |
| `fm4_wheel_log_inputs` | `false` | Log raw axis, button and hat changes. |

H-shifter gears are sent as FH1 reads them (the held gear on the right stick's
X axis, with a shifter reported in the capabilities); FM4 shares FH1's wheel
code but this is not confirmed in FM4 yet. FH1's 900-degree flag does not exist
in FM4 (no comparison against 900 in its code); the rotation is matched by
scaling the steering instead (`fm4_wheel_game_rotation`). FM4 has no single
steering rotation to read: each car has its own lock in its data
(`SteerLock`, `SteeringWheelAngleScale`).

## Force feedback implementation

`src/wheel_ffb.cpp` replaces FM4's XInput-FF functions (`sub_82CA9C08` to
`sub_82CA9DF8`): the game's five effect slots are decoded from guest memory
(forces and coefficients quantised to +/-127, gains to 255) and passed to the
device monitor (`src/wheel.cpp`), which scales them (`src/wheel_mapping.cpp`)
and plays them. Effects are updated only when they change (constant force at
most every 10 ms, conditions every 40 ms), run only while the window has focus
and the game reports the player driving (`sub_823A3F68`), and stop when the
game has not sent anything for 200 ms. The launcher opens the force device only
for Test, so the game can take it.

`fm4-wheel-probe.exe` lists every device with its match text and raw values
without sending any force.

## Tests

`ctest` covers the binding parser, device matching, steering and pedal maths,
FFB scaling, settings round trip and migration (`wheel-input`), the guest FFB
hooks (`wheel-native-ffb`), a virtual wheel plus a separate virtual pedal set
through the game's input system (`wheel-runtime`), the launcher's device
monitor (`wheel-ui`) and the telemetry packet (`telemetry-dash`). They do not
certify physical force feedback.
