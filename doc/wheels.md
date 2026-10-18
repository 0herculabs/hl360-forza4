# Experimental steering wheels and native FFB

The wheel backend uses raw SDL joysticks and SDL haptic capabilities rather
than a brand or model whitelist. The local MOZA R3 enumerates as **Gudsen R3
Racing Wheel and Pedals**, with eight axes and 128 buttons. Devices must expose
a PC joystick interface; force feedback additionally requires constant-force
effects and device gain control through the installed Windows driver. Other
MOZA bases and other brands use the same path, but have not been physically
validated. This is not a claim of universal compatibility.

## Setup

The default calibration interface is now a guided tour. Select your wheel and
press **Start guided calibration**. Follow eight prompts: center/release all,
accelerator fully pressed, accelerator released, brake fully pressed, brake
released, full left, full right, and centered again. Hold each position while
pressing **Next**, then **Finish and save**. The launcher detects the moving
axes and pedal direction automatically and displays **OK** when saved.
During the tour it identifies each detected axis. After calibration, **Live
controls** shows the steering, accelerator and brake Axis numbers, normalized
percentages. Steering has a center marker; pedals read 0–100%.
**View all axes in real time** also shows every axis as a percentage, including unused ones,
without opening advanced settings.

Cancellation preserves the previous calibration. Ambiguous movement, unchanged
controls and invalid positions leave the wizard on the current step for retry.
Reconnect the same device after a disconnect. Set rotation in your base software
before starting. Manual calibration remains under **Advanced calibration and
FFB test**; button assignment and FFB controls have separate collapsed sections.

1. Connect the base in PC mode with its normal vendor driver. Pedals connected
   to the base use that same selected device. Separate USB pedal aggregation
   is not implemented in this version.
2. In **Controls / Controles**, select the wheel explicitly and enable
   **Use steering wheel / Usar volante**.
3. Expand **Show all raw axes / Ver todos los ejes**. Move the wheel and press
   each pedal to identify the three independent axis numbers.
4. Select steering, throttle and brake axes. Capture full left, center, full
   right, throttle released/fully pressed, and brake released/fully pressed.
   All seven captures are required; invalid or duplicate-axis calibrations
   keep steering, pedals and FFB inactive; assigned buttons and the D-pad still
   work in menus. Changing an axis clears its
   affected calibration captures. The base's rotation setting should match
   the range used for the full-left/full-right captures.
5. Assign menu buttons and paddles to the desired Xbox button actions. The
   first hat maps to the D-pad automatically. For wheels reporting D-pad
   directions as buttons, assign D-pad up/down/left/right in the button list.
   In wheel mode player one uses the wheel alone,
   preventing a connected gamepad or keyboard from changing its subtype.
6. Optionally press **Test FFB: 20% pulse, 150 ms** to check the hardware output
   path. This explicit diagnostic is independent of game FFB.
7. Enable **Experimental game FFB** to try native effects. Default device gain
   is 10%; the adjustable device gain ranges from 0 to 100%. An inversion setting affects constant
   torque. Unsupported condition effects are skipped; constant force and gain
   control are required before the guest is told FFB is available.

FFB is off by default. Settings are saved to `fm4.user.toml`; close the game
before changing them. `fm4-wheel-probe.exe` prints a read-only inventory and
current raw axis values. It never requests motor effects.

## Paddles and optional clutch pedal

Open **Paddles and clutch** to assign a physical button to **Shift up** and
**Shift down**. Each action has a selectable destination Xbox button; the defaults
are RB/LB and must match the wheel layout selected inside FM4. These bindings are
independent of menu A/B/X/Y bindings. Select Manual or Manual with clutch in FM4.

Enable **Use clutch pedal** and run **Calibrate clutch**: release all pedals,
press only the clutch fully, then release it. Its axis is detected separately,
excluding steering, throttle and brake. It supports reversed pedals, preserves
the existing driving calibration and shows the clutch's Axis/percentage live.
The clutch is optional and does not prevent driving without a third pedal.

This version bridges the calibrated clutch axis to a configurable Xbox button
at a configurable activation threshold (default 50%, destination B). It does
**not** implement a native proportional clutch or bite-point simulation. Set the
destination to the clutch button in your FM4 wheel layout. The legacy wheel
XInput mapping has no standardized analog clutch field; see Microsoft's
[XUSB wheel mapping](https://learn.microsoft.com/en-us/windows/win32/xinput/directinput-and-xusb-devices).

## Native FM4 boundary

This implementation is specific to the USA Rev 1 executable already used by
the recompilation, including an optional Forza Plus data installation. It does
not execute a different mod executable.

The generated native XInput-FF library wrappers are intercepted at:

| Address | Role |
| --- | --- |
| `0x82CA9C08` | Device information; FM4 requires five slots |
| `0x82CA9C68` | Create effects, arrays of 116-byte descriptors |
| `0x82CA9CC8` | Update an effect |
| `0x82CA9D28` | Start/stop operation lists |
| `0x82CA9D90`, `0x82CA9DF8` | Reset/device control |
| `0x823A3F68` | Per-frame FFB submission and its enable gate |

The descriptor producer is `0x828CE8D0`. It creates slots 1â€“5 for inertia,
friction, damper, spring and constant force, using native type codes 10, 11, 9,
8 and 1 respectively. Descriptor fields are big-endian: ID at +0, type at +4,
gain at +12, type-specific data at +44. Signed force/coefficient values range
from -127 to 127; gain and saturation range from 0 to 255. The constant-force
updater maps the game's normalized force at the FFB object +1336 to +/-127.
The adapter preserves the game's computation and translates the resulting
effects, instead of treating the two controller rumble motors as wheel torque.

No-wheel and disabled-FFB calls forward to the original guest library. The
adapter completes its intercepted asynchronous requests synchronously. Only
player zero is handled. Effects and capability checks are logged by the wheel
driver when available.

Game haptic calls belong to one worker thread. The launcher uses its UI thread
for joystick and haptic operations alongside SDL event pumping. Output requires game focus, the
native FFB enable gate and a heartbeat less than 200 ms old. Generated effects
have finite 100 ms duration and are renewed every 32 ms. Focus loss, minimize,
window close, guest exit and disconnection stop output. The brief launcher
diagnostic has its own 150 ms duration and stops when the controls tab loses
focus. Hardware gain control limits the combined output of all effects.

## Validation and remaining work

### v0.2.6 D-pad buttons and R3 pedal selection

Four extra button bindings map D-pad directions for wheels that report them as
buttons instead of a POV hat. Existing settings retain their button bindings.
The R3 pedal shortcut selects throttle axis 2 and brake axis 5 (zero-based,
matching the raw axis display), clearing pedal captures so their released/full
positions must be captured again. It preserves steering captures. All seven
captures must be valid to activate driving axes. Runtime tests cover independent
throttle/brake output on axes 2/5 and D-pad button input alongside a POV hat.

### v0.2.5 diagnostic strength

The explicit launcher diagnostic now requests 20% constant force for 150 ms.
The 750 ms cooldown and focus/disconnection stop behavior remain in place.
Game FFB gain is configured separately.

### v0.2.4 menu buttons before calibration

A selected wheel now exposes assigned buttons and its D-pad even when axis
calibration is incomplete. Previously the runtime skipped the entire wheel
driver, so buttons detected by the launcher never reached the game. Uncalibrated
axes stay neutral and FFB stays unavailable. The runtime regression test now
checks that an uncalibrated virtual wheel's A button reaches player zero while
its fully pressed but uncalibrated throttle remains neutral, then checks the
fully calibrated path separately.

### v0.2.3 launcher diagnostic stability

The launcher's monitor now runs on the UI thread, keeping joystick/haptic calls
on the same thread as SDL event pumping. The runtime keeps its worker mode.
The haptic device remains open between diagnostic pulses instead of repeatedly
closing/reacquiring DirectInput. No idle stop commands are sent when there are
no effects, and repeated test requests are rejected during a 750 ms cooldown.
The launcher explicitly destroys its monitor before SDL/video shutdown.

The `wheel-ui` test pumps video events, updates eight virtual axes and 128
buttons, repeatedly requests diagnostics through the unsupported-device path,
and checks same-thread callbacks and repeated monitor teardown. It sends no
physical force. Stability on the R3 during actual haptic output still requires
hardware verification; these checks do not reproduce a vendor-driver hang.

### v0.2.2 haptic library fix

The earlier SDK SDL library was compiled with haptic support disabled. A
successful joystick preview did not detect that omission, so the diagnostic
reported `SDL not built with haptic (force feedback) support`. FM4 now prefers
a local SDL install with `SDL_HAPTIC=ON` and Windows DirectInput enabled.

Build it from the same SDL source version used by your SDK:

```bat
scripts\build-wheel-sdl.cmd path\to\rexglue-sdk\thirdparty\sdl3
scripts\build.cmd
ctest --test-dir out\build\win-amd64-release --output-on-failure
```

The local build/install stays under FM4's `out` directory and does not change
the shared SDK. `FM4_SDL3_ROOT` can override the install location. The
`wheel-haptic-backend` regression test initializes and enumerates haptics without
opening a device or sending motor commands. It fails with the old library even
on machines without a connected wheel. A passing backend test does not confirm
physical force output or vendor driver compatibility.

### v0.2.1 input handoff fix

The launcher now closes and joins its raw-device monitor before starting the
game and keeps it closed while the child runs. Windows SDL DirectInput acquires
wheels exclusively; leaving the preview open could prevent the game acquiring
the same base. Raw polling explicitly allows background events because the game
window can belong to a different SDL instance in the runtime DLL. Motor output
still requires application focus.

The diagnostic explicitly sets global gain to 100% and uses a 5%-magnitude,
150 ms effect. Previously a residual 10% device gain could reduce the pulse to
0.5%. Successful submission means the driver accepted the command, not that
physical output has been verified. Device-open and haptic errors are now shown
or logged, including failures before input becomes ready.

Automated checks cover normal/inverted/asymmetric calibration, pedal inversion
and clamping, malformed calibration rejection, settings persistence, a virtual
wheel's axes/buttons/hot unplug, delivery through the real guest InputSystem,
wheel subtype and disabled FFB flags, big-endian native effect decoding,
synchronous request completion, operation lists, bounded effect counts,
fallback forwarding, effect scaling/inversion and finite durations.

R3 device enumeration and raw values have been observed locally. Physical
forces, force direction, every native control-operation semantic, menu/pause
behavior and races still require validation with hardware. The other brands
are supported by capability detection, not certified profiles. The preview
must remain labeled experimental until these checks are complete.

SDL reference: [joystick API](https://wiki.libsdl.org/SDL3/CategoryJoystick)
and [haptic API](https://wiki.libsdl.org/SDL3/CategoryHaptic).



