// The in-game camera: field of view and seat position, adjusted live with keys
// and wheel buttons (camera_view.cpp).
#pragma once

#include <cstdint>
#include <filesystem>

namespace rex::ui {
class Window;
}

namespace fm4::camera {
// fm4.camera.toml next to the exe: the adjustments the game saved.
std::filesystem::path ConfigFile();
// Starts reading the camera keys (keybind_camera_*) from the game window.
void AttachKeys(rex::ui::Window* window);
// The window lost focus: no key is held any more.
void ReleaseKeys();
}  // namespace fm4::camera
