// fm4tool - helper utilities for the FM4 recompilation project.
//
//   fm4tool iso <image.iso> <out_dir>
//       Extracts the game partition of an Xbox 360 disc image.
//
//   fm4tool stfs <package> <out_dir>
//       Extracts an STFS/PIRS/LIVE/CON package into a host directory.
//
//   fm4tool install-dlc <game_dir> <disc2_dir> <user_data_dir>
//       Installs every content package of the FM4 content install disc into
//       <user_data_dir> with the runtime's ContentManager (files + license
//       header), as the game expects. <game_dir> holds default.xex (Disc 1).
//
//   fm4tool install-package <game_dir> <user_data_dir> <package>...
//       Installs the given downloadable content packages the same way.
//       Anything that is not FM4 marketplace content is refused.
//
//   fm4tool image <game_root> <xex_name> <out_dir>
//       Loads the XEX the same way codegen does and writes the decrypted,
//       decompressed image (image.bin) plus its section table and the module
//       hash Xenia uses for .patch.toml files (sections.txt).

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <queue>
#include <string>
#include <vector>

#include <rex/filesystem/device.h>
#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>
#include <xxh3.h>

namespace fs = std::filesystem;

static constexpr char kDlcContentDir[] = "Content/0000000000000000/4D530910/00000002";

static int ExtractDevice(rex::filesystem::Device& device, const fs::path& out_dir) {
  if (!device.Initialize()) {
    std::fprintf(stderr, "cannot read %s\n", out_dir.string().c_str());
    return 1;
  }
  std::queue<rex::filesystem::Entry*> queue;
  queue.push(device.ResolvePath("/"));
  std::vector<uint8_t> buffer;
  size_t files = 0;
  uint64_t bytes = 0;
  while (!queue.empty()) {
    auto* entry = queue.front();
    queue.pop();
    for (auto& child : entry->children()) {
      queue.push(child.get());
    }
    auto dest = out_dir / rex::to_path(entry->path());
    if (entry->attributes() & rex::filesystem::kFileAttributeDirectory) {
      fs::create_directories(dest);
      continue;
    }
    fs::create_directories(dest.parent_path());
    std::error_code ec;
    if (fs::file_size(dest, ec) == entry->size() && !ec) {
      continue;  // already extracted
    }
    rex::filesystem::File* in = nullptr;
    if (entry->Open(rex::filesystem::FileAccess::kFileReadData, &in) != rex::X_STATUS(0)) {
      std::fprintf(stderr, "cannot open %s\n", entry->path().c_str());
      return 1;
    }
    std::ofstream out(dest, std::ios::binary);
    buffer.resize(std::min<size_t>(entry->size(), 16u << 20));
    size_t offset = 0;
    while (offset < entry->size()) {
      size_t chunk = std::min(buffer.size(), entry->size() - offset);
      size_t read = 0;
      in->ReadSync(std::span<uint8_t>(buffer.data(), chunk), offset, &read);
      if (read == 0) {
        break;
      }
      out.write(reinterpret_cast<const char*>(buffer.data()), read);
      offset += read;
    }
    in->Destroy();
    if (offset != entry->size()) {
      std::fprintf(stderr, "short read on %s\n", entry->path().c_str());
      return 1;
    }
    ++files;
    bytes += offset;
  }
  std::printf("%zu files (%.1f MiB) extracted to %s\n", files, bytes / 1048576.0,
              out_dir.string().c_str());
  return 0;
}

// The runtime's ContentManager installs packages the way the game reads
// them: files under 0000000000000000/<title>/00000002/<package>/ plus a
// Headers/00000002/<package>.header holding the content data and the
// license mask from the STFS header. It needs the XEX loaded for the title ID.
static int InstallPackages(const fs::path& game_dir, const fs::path& user_dir,
                           const std::vector<fs::path>& packages) {
  rex::Runtime runtime(game_dir, user_dir);
  if (runtime.Setup(rex::RuntimeConfig{.kernel_init = rex::kernel::InitializeKernel,
                                       .tool_mode = true}) != rex::X_STATUS(0) ||
      runtime.LoadXexImage("game:\\default.xex") != rex::X_STATUS(0)) {
    std::fprintf(stderr, "cannot load %s\\default.xex\n", game_dir.string().c_str());
    return 1;
  }
  auto* content = runtime.kernel_state()->content_manager();
  for (const auto& package : packages) {
    // InstallContent files everything as marketplace content of the running
    // title, so anything else (avatar items, other games) is refused here.
    rex::filesystem::StfsContainerDevice device("", package);
    const auto name = package.filename().string();
    if (!device.Initialize()) {
      std::printf("%s: FAILED (not an STFS package)\n", name.c_str());
      return 1;
    }
    const auto& metadata = device.header().metadata;
    const auto type = static_cast<rex::system::XContentType>(metadata.content_type);
    if (type != rex::system::XContentType::kMarketplaceContent ||
        uint32_t(metadata.execution_info.title_id) != runtime.kernel_state()->title_id()) {
      std::printf("%s: FAILED (not Forza Motorsport 4 downloadable content)\n", name.c_str());
      return 1;
    }
    // Neither the installer nor the game opens files with long-path support.
    char title[9];
    std::snprintf(title, sizeof(title), "%08X", runtime.kernel_state()->title_id());
    const fs::path install_root =
        fs::absolute(user_dir) / "0000000000000000" / title / "00000002" / package.filename();
    size_t longest = 0;
    std::queue<rex::filesystem::Entry*> queue;
    queue.push(device.ResolvePath("/"));
    while (!queue.empty()) {
      auto* entry = queue.front();
      queue.pop();
      for (auto& child : entry->children()) queue.push(child.get());
      longest = std::max(longest, (install_root / rex::to_path(entry->path())).wstring().size());
    }
    if (longest > 259) {
      std::printf("%s: FAILED (path too long: %zu characters, at most 259; choose a shorter user data folder)\n",
                  name.c_str(), longest);
      return 1;
    }
    auto result = content->InstallContent(package);
    std::printf("%s: %s\n", name.c_str(), result == 0 ? "installed" : "FAILED");
    std::fflush(stdout);  // the launcher reads progress from a pipe
    if (result != 0) {
      return 1;
    }
  }
  return 0;
}

static int InstallDlc(const fs::path& game_dir, const fs::path& disc2_dir,
                      const fs::path& user_dir) {
  fs::path src = disc2_dir / kDlcContentDir;
  if (!fs::is_directory(src)) {
    std::fprintf(stderr, "%s is not the FM4 content install disc\n", disc2_dir.string().c_str());
    return 1;
  }
  std::vector<fs::path> packages;
  for (const auto& package : fs::directory_iterator(src)) {
    if (package.is_regular_file()) packages.push_back(package.path());
  }
  return InstallPackages(game_dir, user_dir, packages);
}

static int DumpImage(const fs::path& game_root, const std::string& xex_name,
                     const fs::path& out_dir) {
  rex::Runtime runtime(game_root);
  if (runtime.Setup(rex::RuntimeConfig{.kernel_init = rex::kernel::InitializeKernel,
                                       .tool_mode = true}) != rex::X_STATUS(0)) {
    std::fprintf(stderr, "runtime setup failed\n");
    return 1;
  }
  if (runtime.LoadXexImage("game:\\" + xex_name) != rex::X_STATUS(0)) {
    std::fprintf(stderr, "failed to load %s\n", xex_name.c_str());
    return 1;
  }
  auto* xex = runtime.kernel_state()->GetExecutableModule()->xex_module();
  uint32_t base = xex->base_address();
  uint32_t size = xex->image_size();

  fs::create_directories(out_dir);
  std::ofstream bin(out_dir / "image.bin", std::ios::binary);
  bin.write(reinterpret_cast<const char*>(runtime.memory()->TranslateVirtual(base)), size);

  std::ofstream txt(out_dir / "sections.txt");
  char line[256];
  std::snprintf(line, sizeof(line), "base %08X size %08X entry %08X\n", base, size,
                xex->entry_point());
  txt << line;
  for (const auto& s : xex->pe_sections()) {
    std::snprintf(line, sizeof(line), "%-8s %08X %08X %08X\n", s.name, s.address, s.size,
                  s.flags);
    txt << line;
  }

  // Same hash Xenia uses to match .patch.toml files (UserModule::CalculateHash):
  // XXH3 over [first code descriptor, last code descriptor], indexing
  // descriptors as if each one were a single page.
  auto* sec = xex->xex_security_info();
  uint32_t page_size = runtime.memory()->LookupHeap(base)->page_size();
  uint32_t first = UINT32_MAX, last = 0;
  for (uint32_t i = 0; i < sec->page_descriptor_count; i++) {
    rex::xex2_page_descriptor desc;
    desc.value = rex::byte_swap(sec->page_descriptors[i].value);
    if (desc.info == rex::XEX_SECTION_CODE) {
      first = std::min(first, i);
      last = i;
    }
  }
  uint32_t start = base + first * page_size;
  uint32_t end = base + (last + 1) * page_size;
  std::snprintf(line, sizeof(line), "hash %016llX\n",
                static_cast<unsigned long long>(
                    XXH3_64bits(runtime.memory()->TranslateVirtual(start), end - start)));
  txt << line;
  std::printf("%s", line);
  std::printf("wrote %u bytes from %08X\n", size, base);
  return 0;
}

// Exercises the real VFS/XEX/recompiled-DLL lifecycle without running guest code.
static int TestModuleReload(const fs::path& game_root, const fs::path& user_root,
                            const std::string& library) {
  rex::Runtime runtime(game_root, user_root);
  if (runtime.Setup(rex::RuntimeConfig{.kernel_init = rex::kernel::InitializeKernel,
                                      .tool_mode = true}) != rex::X_STATUS(0) ||
      runtime.LoadXexImage("game:\\default.xex") != rex::X_STATUS(0)) return 1;
  auto* kernel = runtime.kernel_state();
  kernel->RegisterRecompiledModule("xmediafacade_default.xex", "xmediafacade_default.xex",
                                  library.c_str());
  bool matched_absolute_path = true;
  for (int cycle = 0; cycle < 3; ++cycle) {
    auto module = kernel->LoadUserModule("game:\\XMediaFacade_default.xex", false);
    if (!module) {
      std::fprintf(stderr, "module reload FAILED on cycle %d\n", cycle + 1);
      return 1;
    }
    matched_absolute_path &= kernel->FindRecompiledModule(module->path()).has_value();
    kernel->UnloadUserModule(module, false);
    module.reset();
  }
  if (!matched_absolute_path) {
    std::fprintf(stderr, "absolute module path lookup FAILED\n");
    return 1;
  }
  std::puts("PASS: three XMediaFacade load/unload cycles and absolute path lookups");
  return 0;
}

int main(int argc, char** argv) {
  std::string mode = argc > 1 ? argv[1] : "";
  if (mode == "iso" && argc == 4) {
    rex::filesystem::DiscImageDevice device("", argv[2]);
    return ExtractDevice(device, argv[3]);
  }
  if (mode == "stfs" && argc == 4) {
    rex::filesystem::StfsContainerDevice device("", argv[2]);
    return ExtractDevice(device, argv[3]);
  }
  if (mode == "install-dlc" && argc == 5) {
    return InstallDlc(argv[2], argv[3], argv[4]);
  }
  if (mode == "install-package" && argc >= 5) {
    return InstallPackages(argv[2], argv[3], std::vector<fs::path>(argv + 4, argv + argc));
  }
  if (mode == "image" && argc == 5) {
    return DumpImage(argv[2], argv[3], argv[4]);
  }
  if (mode == "module-reload-test" && argc == 5) {
    return TestModuleReload(argv[2], argv[3], argv[4]);
  }
  std::fprintf(stderr,
               "usage:\n"
               "  fm4tool iso <image.iso> <out_dir>\n"
               "  fm4tool stfs <package> <out_dir>\n"
               "  fm4tool install-dlc <game_dir> <disc2_dir> <user_data_dir>\n"
               "  fm4tool install-package <game_dir> <user_data_dir> <package>...\n"
               "  fm4tool image <game_root> <xex_name> <out_dir>\n"
               "  fm4tool module-reload-test <game_root> <test_user_dir> <module_dll>\n");
  return 2;
}
