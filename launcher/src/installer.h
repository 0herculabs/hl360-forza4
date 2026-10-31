#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace launcher {

// Shared between the worker thread and the UI.
struct TaskProgress {
  enum class State { kIdle, kRunning, kDone, kFailed, kCancelled };
  std::atomic<State> state{State::kIdle};
  std::atomic<uint64_t> done{0};
  std::atomic<uint64_t> total{0};
  std::atomic<bool> cancel{false};

  void SetStatus(std::string text) {
    std::lock_guard lock(mutex);
    status = std::move(text);
  }
  std::string Status() {
    std::lock_guard lock(mutex);
    return status;
  }

  std::mutex mutex;
  std::string status;
};

struct ManifestEntry {
  std::string path;  // '/'-separated, relative to the game folder
  uint64_t size = 0;
  std::string sha256;
  std::vector<std::string> alternatives;  // other accepted contents of the same size
};

// Parses game_files.sha256 (`<sha256> <size> <path>` per line, '#' comments).
std::vector<ManifestEntry> LoadManifest(const std::filesystem::path& file);

// The two discs of Forza Motorsport 4 USA Rev 1.
enum class DiscKind { kUnknown, kPlay, kContent };

// Which disc an image is, from its file index: the content install disc holds
// Content/0000000000000000/4D530910/00000002, the play disc Media/ and
// default.xex. kUnknown for anything else (or an unreadable image).
DiscKind IdentifyIso(const std::filesystem::path& iso);

// Extracts the play disc into `game_dir` (all files minus $SystemUpdate).
bool ExtractPlayDisc(const std::filesystem::path& iso, const std::filesystem::path& game_dir,
                     TaskProgress& progress);

// Installs the content install disc: its 4 packages are copied out of the
// image into a staging folder, then fm4tool (next to the launcher) installs
// them into `user_dir` the way the game reads them - files plus a license
// header per package. `game_dir` holds default.xex (the title ID comes from it).
bool InstallContentDisc(const std::filesystem::path& iso, const std::filesystem::path& game_dir,
                        const std::filesystem::path& user_dir, const std::filesystem::path& tool,
                        TaskProgress& progress);

// Packages of the content disc installed in `user_dir` (files and header).
constexpr size_t kContentPackages = 4;

// Additional content of FM4: the four Disc 2 packs and downloadable content
// (marketplace packages, type 00000002, of title 4D530910).
struct ContentPackage {
  std::filesystem::path path;  // the package file, or its installed folder
  std::string file_name;       // the package's file name, which names the install
  std::string display_name;
  uint64_t size = 0;
  bool disc2 = false;  // one of the Content Install Disc packs
};

// FM4 packages in `path`: a package file, or a folder searched recursively
// (e.g. a 4D530910 folder copied from a console). Other titles and content
// types, such as avatar items, are left out and counted in `skipped`.
std::vector<ContentPackage> ScanContentPackages(const std::filesystem::path& path, size_t& skipped);

// Packages installed in `user_dir` (folder plus header), Disc 2 packs first.
std::vector<ContentPackage> ListInstalledContent(const std::filesystem::path& user_dir);

// Installs package files with fm4tool, like the Disc 2 packs.
bool InstallContentPackages(const std::vector<std::filesystem::path>& packages,
                            const std::filesystem::path& game_dir, const std::filesystem::path& user_dir,
                            const std::filesystem::path& tool, TaskProgress& progress);

// Removes an installed package (its folder and header). Empty on success.
std::string RemoveContentPackage(const std::filesystem::path& user_dir, const std::string& file_name);

// The deepest Disc 2 file is 142 characters below the user data folder, and
// both the installer and the game open files without long-path support
// (MAX_PATH = 260 including the terminator).
constexpr size_t kLongestContentPath = 142;
constexpr size_t kMaxUserDirLength = 259 - 1 - kLongestContentPath;
inline bool UserDirFitsContent(const std::filesystem::path& user_dir) {
  return user_dir.wstring().size() <= kMaxUserDirLength;
}
// Disc 2 packs installed in `user_dir`; downloadable content is not counted.
size_t CountContentPackages(const std::filesystem::path& user_dir);

bool InstallForzaPlus(const std::filesystem::path& script, const std::filesystem::path& zip,
                      const std::filesystem::path& update, const std::filesystem::path& game,
                      const std::filesystem::path& user, const std::string& mode, TaskProgress& progress);
bool UninstallForzaPlus(const std::filesystem::path& script,const std::filesystem::path& game,
                        const std::filesystem::path& user,TaskProgress& progress);

// Copies the dashboard tile art out of <game_dir>/nxeart (an STFS package)
// to `cover`, using fm4tool. Shown on the launcher's left column.
bool ExtractCover(const std::filesystem::path& game_dir, const std::filesystem::path& tool,
                  const std::filesystem::path& cover);

struct VerifyReport {
  std::vector<std::string> missing;
  std::vector<std::string> wrong_size;
  std::vector<std::string> wrong_hash;
  bool ok() const { return missing.empty() && wrong_size.empty() && wrong_hash.empty(); }
};

// Checks `dir` against the manifest. full_hash=false only checks presence and
// size (instant); true also hashes every file on all cores.
VerifyReport VerifyGame(const std::filesystem::path& dir, const std::vector<ManifestEntry>& manifest,
                        bool full_hash, TaskProgress& progress);

// Marker written after a full verification passed, so the launcher can trust
// the folder later without re-hashing 8 GB.
std::filesystem::path VerifiedMarker(const std::filesystem::path& game_dir);

}  // namespace launcher
