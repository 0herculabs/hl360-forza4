#pragma once

namespace rex::ui {
class Window;
}
namespace rex::system {
class IGraphicsSystem;
}

namespace fm4 {
// Before the presenter starts: makes the swapchain readable when capture_dir is set.
void PrepareDebugCapture();
// Saves the presented image to capture_dir every capture_interval seconds.
void StartDebugCapture(rex::ui::Window* window, rex::system::IGraphicsSystem* graphics);
}
