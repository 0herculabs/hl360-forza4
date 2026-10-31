// fm4 - Forza Motorsport 4 recompiled for PC.

#include "fm4_app.h"

#include "crash_handler.h"
#include "file_signatures.h"
#include "wheel_input.h"
#include "audio_output.h"
#include "camera_view.h"
#include "wide_view.h"

#include <charconv>

#include <rex/cvar.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem.h>
#include <rex/logging/macros.h>
#include <rex/runtime.h>
#include <rex/system/gpu_plugin.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xam/user_profile.h>
#include <rex/ui/overlay/achievement_notification.h>

REXCVAR_DEFINE_STRING(profile_name, "", "FM4/Profile",
                      "Gamertag of the signed-in profile (empty = SDK default)");
REXCVAR_DEFINE_STRING(profile_xuid, "", "FM4/Profile",
                      "Offline XUID of the signed-in profile, 16 hex digits");

// The SDK's GPU plugin (CMake picks it): ShiftGlue's fh1 renderer, whose
// Vulkan backend draws FM4 best, or ReXGlue's xenos.
#ifndef FM4_GPU_PLUGIN
#define FM4_GPU_PLUGIN "xenos"
#endif

#ifdef FM4_SDK_DEFINES_GPU_BACKEND
// ShiftGlue's ReXApp defines it (default any); fm4.toml selects vulkan.
REXCVAR_DECLARE(std::string, gpu_backend);
#else
REXCVAR_DEFINE_STRING(gpu_backend, "vulkan", "FM4/Graphics",
                      "Graphics backend for the GPU plugin: any, d3d12, vulkan")
    .allowed({"any", "d3d12", "vulkan"})
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
#endif

REXCVAR_DEFINE_BOOL(crash_test, false, "FM4/Debug",
                    "Crash on purpose after startup to test the crash report")
    .debug_only();

// Two config files next to the exe:
//   fm4.toml       port settings (what the title needs to run), shipped
//   fm4.user.toml  the player's choices, written by the launcher
// The user file loads second so it wins. It also becomes the SDK's config
// path, because the SDK reloads that file after this hook (and the F4 settings
// overlay saves there).
//
// The SDK reads game_data_root/user_data_root before it loads any config, so
// both files are loaded here first: then they can carry the folders.
void Fm4App::OnConfigurePaths(rex::PathConfig& paths) {
  const auto exe_dir = rex::filesystem::GetExecutableFolder();
  const auto user_config = exe_dir / "fm4.user.toml";
  if (std::filesystem::exists(paths.config_path)) {
    rex::cvar::LoadConfig(paths.config_path);
  }
  if (std::filesystem::exists(user_config)) {
    rex::cvar::LoadConfig(user_config);
    paths.config_path = user_config;
  }
  // The camera adjustments the game saved (camera_view.cpp).
  if (std::filesystem::exists(fm4::camera::ConfigFile())) {
    rex::cvar::LoadConfig(fm4::camera::ConfigFile());
  }
  if (paths.game_data_root.empty()) {
    const std::string from_config = REXCVAR_GET(game_data_root);
    paths.game_data_root = from_config.empty() ? exe_dir / "game" : std::filesystem::path(from_config);
  }
  const std::string user_from_config = REXCVAR_GET(user_data_root);
  if (!user_from_config.empty()) {
    paths.user_data_root = user_from_config;
  }
}

void Fm4App::OnPostInitLogging() {
  fm4::InstallCrashHandler(rex::filesystem::GetExecutableFolder() / "logs");
}

void Fm4App::OnPreSetup(rex::RuntimeConfig& config) {
  config.input_factory = fm4::wheel::CreateInputSystem;
  config.audio_factory = [](rex::runtime::FunctionDispatcher* dispatcher) {
    return std::make_unique<fm4::OutputSystem>(dispatcher);
  };
  // FM4 always renders 1280x720. Reporting the window as the TV (the SDK's
  // default when a window size is set) made the presenter treat that image as
  // the window's aspect and stretch it, so black bars never appeared on a wide
  // window; the image keeps 16:9 and wide windows go through display_wide_view.
  rex::cvar::SetFlagByName("video_mode_from_window", "false");
  if (config.graphics || !config.gpu_plugin.empty()) {
    return;  // --gpu_plugin given explicitly; let ReXApp load it.
  }
  config.graphics = rex::system::LoadGpuPlugin(FM4_GPU_PLUGIN, REXCVAR_GET(gpu_backend));
  if (!config.graphics) {
    // Fall back to ReXApp's loader, which reports the failure to the user.
    REXLOG_ERROR("Could not create the '{}' backend, retrying with any",
                 REXCVAR_GET(gpu_backend));
    config.gpu_plugin = FM4_GPU_PLUGIN;
  }
}

// The game writes temporary data (e.g. cache:\replay_stream during races) to
// the console's cache partition and never mounts it itself, so give it a
// writable host folder.
static void MountCachePartition(rex::Runtime* runtime, const std::filesystem::path& user_root) {
  constexpr char kMountPath[] = "\\Device\\Fm4Cache";
  auto host_path = user_root / "cache_partition";
  std::error_code ec;
  std::filesystem::create_directories(host_path, ec);
  auto device = std::make_unique<rex::filesystem::HostPathDevice>(kMountPath, host_path, false);
  auto* fs = runtime->file_system();
  if (!device->Initialize() || !fs->RegisterDevice(std::move(device)) ||
      !fs->RegisterSymbolicLink("cache:", kMountPath)) {
    REXLOG_ERROR("Could not mount cache: at {}", host_path.string());
    return;
  }
  REXLOG_INFO("Mounted cache: at {}", host_path.string());
}

void Fm4App::OnPostLoadXexImage() {
  fm4::ResignProtectedFiles(runtime(), game_data_root());
}

void Fm4App::OnPostSetup() {
  fm4::wheel::SetFocus(window() && window()->HasFocus());
  if (window()) window()->AddInputListener(&fullscreen_toggle_, 100);
  fm4::camera::AttachKeys(window());
  if (window()) {
    fm4::wide_view::SetWindowSize(window()->GetActualPhysicalWidth(),
                                  window()->GetActualPhysicalHeight());
  }
  MountCachePartition(runtime(), user_data_root());

  auto* profile = runtime()->kernel_state()->user_profile();

  const std::string& name = REXCVAR_GET(profile_name);
  if (!name.empty()) {
    // Xbox gamertags are at most 15 characters.
    profile->set_name(name.substr(0, 15));
  }

  const std::string& xuid_str = REXCVAR_GET(profile_xuid);
  uint64_t xuid = 0;
  auto [end, ec] = std::from_chars(xuid_str.data(), xuid_str.data() + xuid_str.size(), xuid, 16);
  if (!xuid_str.empty() && ec == std::errc() && end == xuid_str.data() + xuid_str.size()) {
    // Bits 54-55 set make the XUID look like a restricted account to the game.
    profile->set_xuid(xuid & ~0x00C0000000000000ull);
  } else if (!xuid_str.empty()) {
    REXLOG_WARN("Ignoring invalid profile_xuid '{}'", xuid_str);
  }

  REXLOG_INFO("Signed in as '{}' (XUID {:016X})", profile->name(), profile->xuid());

  if (REXCVAR_GET(crash_test)) {
    *static_cast<volatile int*>(nullptr) = 0;
  }
}

void Fm4App::OnWindowFocusChanged(bool focused) {
  fm4::wheel::SetFocus(focused);
  if (!focused) fm4::camera::ReleaseKeys();
}
bool Fm4App::OnWindowCloseRequested() { fm4::wheel::SetFocus(false);fm4::wheel::ResetEffects();return true; }
void Fm4App::OnWindowMinimized() { fm4::wheel::SetFocus(false); }

void Fm4App::FullscreenToggle::OnKeyDown(rex::ui::KeyEvent& e) {
  if (e.virtual_key() != rex::ui::VirtualKey::kReturn || !e.is_alt_pressed() || e.prev_state()) return;
  if (auto* window = app_.window()) {
    window->SetFullscreen(!window->IsFullscreen());
    e.set_handled(true);
  }
}

void Fm4App::OnWindowPixelSizeChanged(uint32_t pixel_width, uint32_t pixel_height) {
  fm4::wide_view::SetWindowSize(pixel_width, pixel_height);
}
void Fm4App::OnGuestThreadExit(rex::system::XThread*) { fm4::wheel::SetFocus(false);fm4::wheel::ResetEffects(); }

std::unique_ptr<rex::ui::AchievementNotificationDialog> Fm4App::CreateAchievementNotificationDialog() {
  return nullptr;
}
