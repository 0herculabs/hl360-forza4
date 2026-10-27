// fm4 - Forza Motorsport 4 recompiled for PC.

#pragma once

#include <rex/rex_app.h>
#include <rex/ui/window_listener.h>

class Fm4App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  // Defined in main.cpp, the only TU that sees the generated PPCImageConfig.
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx);

 protected:
  // Loads fm4.toml and fm4.user.toml (the launcher's) and takes the game and
  // user-data folders from them.
  void OnConfigurePaths(rex::PathConfig& paths) override;
  // Installs the crash handler (logs/crash_*.txt + .dmp next to the exe).
  void OnPostInitLogging() override;
  // Loads the xenos GPU plugin with the backend chosen in the launcher.
  void OnPreSetup(rex::RuntimeConfig& config) override;
  // FM4 image hook; no FM3 address-dependent patches are applied.
  void OnPostLoadXexImage() override;
  // Signs in the profile selected in the launcher (--profile_name / --profile_xuid).
  void OnPostSetup() override;
  void OnWindowFocusChanged(bool focused) override;
  bool OnWindowCloseRequested() override;
  void OnWindowMinimized() override;
  // Wide windows widen the view to the window's aspect (wide_view.cpp).
  void OnWindowPixelSizeChanged(uint32_t pixel_width, uint32_t pixel_height) override;
  void OnGuestThreadExit(rex::system::XThread* thread) override;
  // No achievement toasts. While the ImGui drawer has any dialog registered
  // (this one stays registered even with nothing on screen) the presenter
  // repaints the window on its own at the monitor's refresh rate or faster,
  // out of step with the game's 60 frames: high "FPS" and visible judder.
  // Without it the window is presented once per game frame (same as the FM2
  // recompilation).
  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog() override;

 private:
  // Alt+Enter switches between fullscreen and the window (the SDK's binds
  // have no modifiers).
  class FullscreenToggle : public rex::ui::WindowInputListener {
   public:
    explicit FullscreenToggle(Fm4App& app) : app_(app) {}
    void OnKeyDown(rex::ui::KeyEvent& e) override;

   private:
    Fm4App& app_;
  };
  FullscreenToggle fullscreen_toggle_{*this};
};
