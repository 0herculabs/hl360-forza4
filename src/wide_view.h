// Wide windows: Hor+ for the game's 16:9 views (wide_view.cpp).
#pragma once

#include <cstdint>

namespace fm4::wide_view {
// The view's widening for a window: its aspect over 16:9, 1 at 16:9 or narrower.
float ScaleFor(uint32_t width, uint32_t height);
// Whether a view of this aspect is the main view widened.
bool IsWidened(float aspect);
// The window's pixel size changed (or display_wide_view did).
void SetWindowSize(uint32_t width, uint32_t height);
// The widening in use.
float Scale();
// A view was widened this frame (the hook).
void NoteWidened();
// Once per game frame (the present path): picks wide or 16:9 presentation.
void OnFrame();
}  // namespace fm4::wide_view
