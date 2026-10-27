# Camera: field of view and seat position

In game, the field of view and the seat (the eye) can be changed live, so a
cockpit view can match a real screen and seating position. Each camera view
keeps its own settings (cockpit, hood, bumper, chase). The game saves them to
`fm4.camera.toml` next to `fm4.exe` about a second after the last change.

| Action | Default key | Wheel button (Wheel tab, Bindings > Camera) |
| --- | --- | --- |
| Field of view: narrower | `=` or numpad `+` | `CAM_FOV_IN` |
| Field of view: wider | `-` or numpad `-` | `CAM_FOV_OUT` |
| Field of view: default | `0` or numpad `0` | `CAM_FOV_RESET` |
| Seat up / down | Page Up / Page Down | `CAM_SEAT_UP`, `CAM_SEAT_DOWN` |
| Seat forward / back | Home / End | `CAM_SEAT_FORWARD`, `CAM_SEAT_BACK` |
| Seat: default | Insert | `CAM_SEAT_RESET` |

- A press moves one step: 1 degree of field of view, 1 cm of seat. Hold the
  key or button to keep going.
- The game's own camera is the middle of each range: up to 30 degrees
  narrower or wider (kept between 10 and 120 degrees), the seat 30 cm up or
  down and 50 cm forward or back.
- Change the keys in the launcher's **Controls > Camera** card. They work with
  or without the keyboard pad. **Reset all camera views** there deletes
  `fm4.camera.toml`.
- Moving the seat far can show the inside of the car's model, which was only
  built to be seen from the game's own seat.

## How it works

`src/camera_view.cpp` hooks two getters that the main view's setup
(`sub_822CA758`) calls for its camera and its culling view:

- `sub_82390070`: the camera-to-world matrix. The seat offset moves its
  position along the camera's up and view axes, in a copy.
- `sub_822EC390`: the vertical field of view. The adjustment is added.

The 1280x720 main view is the only one changed, so mirrors and other views
keep theirs. Views are told apart by the camera description's `+8` value,
which the log shows as `Camera: view N`. Seat forward follows the way the car
drives. Which of the matrix's axes that is (its rows or columns) is learned from
the car's motion in the first seconds of driving, and saved as
`camera_view_axis`. Until then, forward and back may not be straight.
