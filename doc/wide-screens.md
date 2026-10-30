# Wide screens (21:9, 32:9)

FM4 renders a 16:9 image. On a wider window or monitor the launcher's
**Display > Wide screens** setting picks what happens:

- **Fill Window** (default, `display_wide_view = true`): the game shows more of
  the world at the sides (Hor+). The image fills the window at any size, and
  resizing or maximising the window applies at once. The HUD keeps its
  proportions in the middle 16:9. Movies, loading screens and other frames
  without a 3D view are shown 16:9 with black bars.
- **Black bars**: the 16:9 image, letterboxed.
- **Stretch**: the 16:9 image stretched over the window.

At 16:9 or narrower all three look the same.

With Fill Window, **Display > HUD Position** picks where the HUD goes:

- **Standard** (default): the whole HUD in the middle 16:9.
- **Edges** (`display_hud_edges = true`): elements wholly in the outer thirds
  of the 16:9 image (the minimap, the speedometer, the lap and position
  boxes) keep their size but move to the window's left and right edges;
  the rest stays in the middle. Something that crosses a third's boundary
  stays in the middle, so a line of text that crosses it can split.

The image is still rendered at 1280x720 times the internal resolution and
then stretched horizontally, so it is a little softer across than down. On
wide monitors a higher internal resolution (Quality) makes up for it.

## How it works

As Pinyon Shift does for Forza Horizon 1 (FH1 runs on FM4's engine):

- `sub_822B5E08` returns a viewport's width / height (FH1 `sub_823E22C8`,
  the same instructions). The main view's setup (`sub_822CA758`) passes it
  to the camera and a second consumer; for the 1280x720 main viewport both
  get the window's aspect over 16:9 (`src/wide_view.cpp`). Every projection and
  culling frustum of the frame starts from the camera, so rendering and
  culling widen together. Mirrors (aspect 4.5), cube maps (1.0) and other
  render-to-texture views keep theirs.
- Projections are built in `sub_822C0BA8` (FH1 `sub_823EAED0`, identical); a
  hook there only notes whether a frame had a widened view. Frames without
  one for half a second are shown 16:9.
- The presenter stretches the image over the window (`present_letterbox`
  off while frames are wide).
- The HUD is drawn last, into the front buffer through its
  `2_10_10_10_AS_10_10_10_10` view after the tonemap (as in FH1; seen with a
  one-frame draw log, `gpu_draw_log_frame`). The SDK (`hud_squeeze`, D3D12)
  narrows those draws' viewports and scissors about the image's centre by the
  same factor, so the HUD keeps its proportions. Vulkan does not narrow the
  HUD yet.
- HUD Position Edges (`hud_squeeze_edges`, D3D12): most of the HUD is drawn
  in a few batches over the whole image, so the SDK routes those triangle
  draws through a geometry shader that places each triangle by its x range:
  wholly beyond `hud_squeeze_edges_threshold` (a third of the half width)
  from the centre, it keeps the image's edge; otherwise the centre. Both
  triangles of a quad share its x range, so they move together. Draws with
  their own viewport (the minimap) are placed whole by their viewport and
  scissor.
- The bumper and hood cameras' virtual mirror is drawn into the scene, not
  with the HUD, by its own vertex shader (`FD5E9BBE6622BCC5`, found with
  `gpu_draw_log_filter = ":384x128:"`, the mirror view's texture); FM4 lists
  it in `hud_squeeze_vertex_shaders` so it is narrowed the same way.
- The SDK reported the window as the TV's resolution when a window size was
  set, so the presenter took the 16:9 image for the window's aspect and
  stretched it even with black bars on. FM4 turns that off
  (`video_mode_from_window = false`); the game always rendered 1280x720.

`display_log_views = true` logs every 5 s which views were built (caller and
aspect) and whether they were widened.
