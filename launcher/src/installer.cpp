#include "installer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>
#include <tuple>

#include "i18n.h"
#include "sha256.h"
#include "safe_path.h"

namespace launcher {
namespace fs = std::filesystem;

namespace {

constexpr uint64_t kSector = 2048;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
// Game-partition offsets: XGD2, XGD1, XGD3, plain XISO.
constexpr uint64_t kPartitions[] = {0xFD90000, 0x18300000, 0x2080000, 0};
constexpr char kContentDir[] = "Content/0000000000000000/4D530910/00000002/";

// STFS package header: magic at 0, content type at 0x344, title ID at 0x360,
// first display name (128 big-endian UTF-16 characters) at 0x411.
constexpr size_t kStfsHeaderSize = 0x411 + 256;
constexpr uint32_t kMarketplaceContent = 0x00000002;
constexpr uint32_t kTitleId = 0x4D530910;

uint32_t Be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

std::string Utf16BeToUtf8(const uint8_t* p, size_t max_chars) {
  std::string out;
  for (size_t i = 0; i < max_chars; ++i) {
    uint32_t c = uint32_t(p[i * 2]) << 8 | p[i * 2 + 1];
    if (c == 0) break;
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < max_chars) {
      const uint32_t low = uint32_t(p[i * 2 + 2]) << 8 | p[i * 2 + 3];
      if (low >= 0xDC00 && low < 0xE000) {
        c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
        ++i;
      }
    }
    if (c < 0x80) {
      out += char(c);
    } else if (c < 0x800) {
      out += char(0xC0 | c >> 6);
      out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
      out += char(0xE0 | c >> 12);
      out += char(0x80 | (c >> 6 & 0x3F));
      out += char(0x80 | (c & 0x3F));
    } else {
      out += char(0xF0 | c >> 18);
      out += char(0x80 | (c >> 12 & 0x3F));
      out += char(0x80 | (c >> 6 & 0x3F));
      out += char(0x80 | (c & 0x3F));
    }
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

// The Content Install Disc's four packs: 4d53091000000001 to ...0004.
bool IsDisc2Package(const std::string& file_name) {
  static const char* kNames[] = {"4d53091000000001", "4d53091000000002", "4d53091000000003", "4d53091000000004"};
  return std::any_of(std::begin(kNames), std::end(kNames),
                     [&](const char* n) { return SDL_strcasecmp(n, file_name.c_str()) == 0; });
}

// XDVDFS preserves case; compare guest paths as Xbox/Windows paths.
bool PathStartsWith(const std::string& path, const std::string& prefix) {
  return path.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), path.begin(),
      [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); });
}

struct IsoFile {
  std::string path;
  uint64_t offset = 0;
  uint64_t size = 0;
};

uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24); }

bool ReadAt(std::ifstream& in, uint64_t offset, void* out, size_t size) {
  in.clear();
  in.seekg(static_cast<std::streamoff>(offset));
  in.read(static_cast<char*>(out), static_cast<std::streamsize>(size));
  return in.gcount() == static_cast<std::streamsize>(size);
}

// XDVDFS directories are binary trees of entries; walk one directory table.
bool WalkDirectory(std::ifstream& in, uint64_t partition, uint32_t sector, uint32_t size,
                   const std::string& prefix, std::vector<IsoFile>& out, int depth) {
  if (depth > 32 || size == 0 || size > (64u << 20)) {
    return depth <= 32 && size == 0;
  }
  std::vector<uint8_t> table(size);
  if (!ReadAt(in, partition + uint64_t(sector) * kSector, table.data(), size)) {
    return false;
  }
  std::vector<uint32_t> stack{0};
  std::vector<bool> seen(size / 4 + 1, false);
  while (!stack.empty()) {
    const uint32_t off = stack.back();
    stack.pop_back();
    if (off + 14 > size || seen[off / 4]) {
      continue;
    }
    seen[off / 4] = true;
    const uint8_t* e = table.data() + off;
    const uint16_t left = Le16(e), right = Le16(e + 2);
    if (left == 0xFFFF) {
      continue;  // padding
    }
    const uint32_t start = Le32(e + 4), length = Le32(e + 8);
    const uint8_t attributes = e[12], name_length = e[13];
    if (off + 14 + name_length > size) {
      continue;
    }
    const std::string name(reinterpret_cast<const char*>(e + 14), name_length);
    if (!IsSafeRelativePath(name) || name.find('/') != std::string::npos) return false;
    const std::string path = prefix + name;
    if (attributes & 0x10) {
      if (!WalkDirectory(in, partition, start, length, path + "/", out, depth + 1)) {
        return false;
      }
    } else {
      out.push_back({path, partition + uint64_t(start) * kSector, length});
    }
    if (left) stack.push_back(uint32_t(left) * 4);
    if (right) stack.push_back(uint32_t(right) * 4);
  }
  return true;
}

std::string FormatGb(uint64_t bytes) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.2f GB", double(bytes) / 1e9);
  return text;
}

// Reads the file index of a disc image. Empty on failure (status set).
bool ReadIndex(std::ifstream& in, std::vector<IsoFile>& files, TaskProgress& progress) {
  uint64_t partition = UINT64_MAX;
  char magic[20];
  for (uint64_t candidate : kPartitions) {
    if (ReadAt(in, candidate + 32 * kSector, magic, sizeof(magic)) &&
        std::memcmp(magic, kMagic, sizeof(magic)) == 0) {
      partition = candidate;
      break;
    }
  }
  if (partition == UINT64_MAX) {
    progress.SetStatus(L("Not a recognisable Xbox 360 disc image (XDVDFS).",
                         "No es una imagen de disco de Xbox 360 (XDVDFS) reconocible."));
    return false;
  }
  uint8_t root[8]{};
  if (!ReadAt(in, partition + 32 * kSector + 20, root, sizeof(root))) return false;
  if (!WalkDirectory(in, partition, Le32(root), Le32(root + 4), "", files, 0)) {
    progress.SetStatus(L("The disc index is damaged or the image is incomplete.",
                         "El índice del disco está dañado o la imagen está incompleta."));
    return false;
  }
  return true;
}

DiscKind KindOf(const std::vector<IsoFile>& files) {
  const bool content = std::any_of(files.begin(), files.end(), [](const IsoFile& f) {
    return PathStartsWith(f.path, kContentDir);
  });
  if (content) return DiscKind::kContent;
  const bool xex = std::any_of(files.begin(), files.end(),
                               [](const IsoFile& f) { return f.path == "default.xex"; });
  const bool media = std::any_of(files.begin(), files.end(), [](const IsoFile& f) {
    return PathStartsWith(f.path, "Media/Physics/");
  });
  return xex && media ? DiscKind::kPlay : DiscKind::kUnknown;
}

// Copies `files` out of the image into `dest`, keeping their paths.
bool CopyFiles(std::ifstream& in, const std::vector<IsoFile>& files, const fs::path& dest,
               TaskProgress& progress) {
  uint64_t total = 0;
  for (const auto& f : files) total += f.size;
  progress.total = total;
  progress.done = 0;

  std::error_code ec;
  fs::create_directories(dest, ec);
  const auto space = fs::space(dest, ec);
  if (!ec && space.available < total) {
    progress.SetStatus(std::string(L("Not enough space: ", "No hay espacio suficiente: ")) +
                       FormatGb(total) + L(" needed, ", " necesarios, ") + FormatGb(space.available) +
                       L(" free.", " libres."));
    return false;
  }

  std::vector<char> buffer(8u << 20);
  for (const auto& f : files) {
    if (progress.cancel) {
      progress.SetStatus(L("Cancelled.", "Cancelado."));
      return false;
    }
    progress.SetStatus(std::string(L("Extracting ", "Extrayendo ")) + f.path);
    const fs::path out_path = dest / fs::u8path(f.path);
    fs::create_directories(out_path.parent_path(), ec);
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      progress.SetStatus(std::string(L("Cannot write ", "No se puede escribir ")) + out_path.u8string());
      return false;
    }
    in.clear();
    in.seekg(static_cast<std::streamoff>(f.offset));
    uint64_t left = f.size;
    while (left) {
      const size_t chunk = size_t(std::min<uint64_t>(left, buffer.size()));
      in.read(buffer.data(), static_cast<std::streamsize>(chunk));
      if (in.gcount() != static_cast<std::streamsize>(chunk)) {
        progress.SetStatus(std::string(L("The image is truncated (failed reading ",
                                         "La imagen está truncada (fallo leyendo ")) +
                           f.path + ").");
        return false;
      }
      out.write(buffer.data(), static_cast<std::streamsize>(chunk));
      left -= chunk;
      progress.done += chunk;
      if (progress.cancel) break;
    }
    if (!out) {
      progress.SetStatus(std::string(L("Error writing ", "Error escribiendo ")) + out_path.u8string() +
                         L(" (disk full?).", " (¿disco lleno?)."));
      return false;
    }
  }
  return !progress.cancel;
}

// Runs fm4tool with `args`, calling `on_line` for every line it prints.
// Returns its exit code, or -1 if it could not start / was cancelled.
int RunTool(const fs::path& tool, const std::vector<std::string>& args,
            const std::function<void(const std::string&)>& on_line, TaskProgress& progress) {
  std::vector<std::string> storage{tool.u8string()};
  storage.insert(storage.end(), args.begin(), args.end());
  std::vector<const char*> argv;
  for (const auto& a : storage) argv.push_back(a.c_str());
  argv.push_back(nullptr);
  const std::string cwd = tool.parent_path().u8string();
  SDL_PropertiesID props = SDL_CreateProperties();
  SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(argv.data()));
  SDL_SetStringProperty(props, SDL_PROP_PROCESS_CREATE_WORKING_DIRECTORY_STRING, cwd.c_str());
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
  SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
  SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
  SDL_Process* proc = SDL_CreateProcessWithProperties(props);
  SDL_DestroyProperties(props);
  if (!proc) {
    progress.SetStatus(std::string(L("Could not start fm4tool: ", "No se pudo iniciar fm4tool: ")) +
                       SDL_GetError());
    return -1;
  }
  SDL_IOStream* out = SDL_GetProcessOutput(proc);
  std::string pending;
  char buf[4096];
  for (;;) {
    const size_t got = SDL_ReadIO(out, buf, sizeof(buf));
    if (got == 0) {
      // The pipe is non-blocking: no data yet is not the end of the output.
      if (SDL_GetIOStatus(out) == SDL_IO_STATUS_NOT_READY && !progress.cancel) {
        SDL_Delay(20);
        continue;
      }
      if (progress.cancel) SDL_KillProcess(proc, true);
      break;
    }
    pending.append(buf, got);
    for (size_t nl; (nl = pending.find('\n')) != std::string::npos;) {
      std::string line = pending.substr(0, nl);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      on_line(line);
      pending.erase(0, nl + 1);
    }
    if (progress.cancel) {
      SDL_KillProcess(proc, true);
      break;
    }
  }
  if (!pending.empty()) on_line(pending);
  int exit_code = -1;
  SDL_WaitProcess(proc, true, &exit_code);
  SDL_DestroyProcess(proc);
  return progress.cancel ? -1 : exit_code;
}

}  // namespace

bool InstallForzaPlus(const fs::path& script, const fs::path& zip, const fs::path& update,
                      const fs::path& game, const fs::path& user, const std::string& mode,
                      TaskProgress& progress) {
  const char* system = SDL_getenv("SystemRoot");
  if (!system) { progress.SetStatus("SystemRoot is unavailable"); return false; }
  const auto powershell = fs::u8path(system) / "System32/WindowsPowerShell/v1.0/powershell.exe";
  std::vector<std::string> args{"-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
      "-File", script.u8string(), "-BaseZip", zip.u8string(), "-GameDir", game.u8string(),
      "-UserDir", user.u8string(), "-Mode", mode};
  if (!update.empty()) { args.push_back("-UpdateZip"); args.push_back(update.u8string()); }
  return RunTool(powershell, args, [&](const std::string& line) { progress.SetStatus(line); }, progress) == 0;
}

bool UninstallForzaPlus(const fs::path& script,const fs::path& game,const fs::path& user,TaskProgress& progress) {
  const char* system=SDL_getenv("SystemRoot");
  if(!system) {progress.SetStatus("SystemRoot is unavailable");return false;}
  const auto powershell=fs::u8path(system)/"System32/WindowsPowerShell/v1.0/powershell.exe";
  const std::vector<std::string> args{"-NoLogo","-NoProfile","-NonInteractive","-ExecutionPolicy","Bypass",
      "-File",script.u8string(),"-GameDir",game.u8string(),"-UserDir",user.u8string()};
  return RunTool(powershell,args,[&](const std::string& line){progress.SetStatus(line);},progress)==0;
}

std::vector<ManifestEntry> LoadManifest(const fs::path& file) {
  std::vector<ManifestEntry> out;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream row(line);
    ManifestEntry entry;
    row >> entry.sha256 >> entry.size;
    std::getline(row >> std::ws, entry.path);
    if (!entry.path.empty() && entry.path.back() == '\r') {
      entry.path.pop_back();
    }
    if (entry.sha256.size() != 64 || entry.path.empty()) {
      continue;
    }
    const auto same = std::find_if(out.begin(), out.end(), [&](const ManifestEntry& e) {
      return e.path == entry.path && e.size == entry.size;
    });
    if (same != out.end()) {
      same->alternatives.push_back(std::move(entry.sha256));
    } else {
      out.push_back(std::move(entry));
    }
  }
  return out;
}

DiscKind IdentifyIso(const fs::path& iso) {
  std::ifstream in(iso, std::ios::binary);
  std::vector<IsoFile> files;
  TaskProgress ignored;
  if (!in || !ReadIndex(in, files, ignored)) return DiscKind::kUnknown;
  return KindOf(files);
}

bool ExtractPlayDisc(const fs::path& iso, const fs::path& game_dir, TaskProgress& progress) {
  std::ifstream in(iso, std::ios::binary);
  if (!in) {
    progress.SetStatus(std::string(L("Cannot open the image: ", "No se puede abrir la imagen: ")) +
                       iso.u8string());
    return false;
  }
  std::vector<IsoFile> files;
  progress.SetStatus(L("Reading the disc index...", "Leyendo el índice del disco..."));
  if (!ReadIndex(in, files, progress)) return false;
  if (KindOf(files) != DiscKind::kPlay) {
    progress.SetStatus(L("This is not Disc 1 (Play Disc) of Forza Motorsport 4.",
                         "Esta no es el Disco 1 (Play Disc) de Forza Motorsport 4."));
    return false;
  }
  files.erase(std::remove_if(files.begin(), files.end(),
                             [](const IsoFile& f) { return f.path.rfind("$SystemUpdate/", 0) == 0; }),
              files.end());
  if (!CopyFiles(in, files, game_dir, progress)) return false;
  progress.SetStatus(std::string(L("Extracted ", "Extraídos ")) + std::to_string(files.size()) +
                     L(" files.", " archivos."));
  return true;
}

bool InstallContentDisc(const fs::path& iso, const fs::path& game_dir, const fs::path& user_dir,
                        const fs::path& tool, TaskProgress& progress) {
  std::ifstream in(iso, std::ios::binary);
  if (!in) {
    progress.SetStatus(std::string(L("Cannot open the image: ", "No se puede abrir la imagen: ")) +
                       iso.u8string());
    return false;
  }
  if (!fs::exists(game_dir / "default.xex")) {
    progress.SetStatus(L("Install Disc 1 first: Disc 2 is installed for it.",
                         "Instala antes el Disco 1: el Disco 2 se instala para él."));
    return false;
  }
  if (!fs::exists(tool)) {
    progress.SetStatus(std::string(L("Not found: ", "No se encuentra ")) + tool.u8string());
    return false;
  }
  if (!UserDirFitsContent(user_dir)) {
    progress.SetStatus(std::string(L("The user data folder path is too long for the Disc 2 files (",
                                     "La ruta de la carpeta de datos de usuario es demasiado larga para los "
                                     "archivos del Disco 2 (")) +
                       std::to_string(user_dir.wstring().size()) + L(" characters; at most ", " caracteres; como máximo ") +
                       std::to_string(kMaxUserDirLength) +
                       L("). Choose a shorter one, e.g. C:\\Games\\FM4\\user.",
                         "). Elige una más corta, p. ej. C:\\Juegos\\FM4\\user."));
    return false;
  }
  std::vector<IsoFile> files;
  progress.SetStatus(L("Reading the disc index...", "Leyendo el índice del disco..."));
  if (!ReadIndex(in, files, progress)) return false;
  if (KindOf(files) != DiscKind::kContent) {
    progress.SetStatus(L("This is not Disc 2 (Content Install Disc) of Forza Motorsport 4.",
                         "Esta no es el Disco 2 (Content Install Disc) de Forza Motorsport 4."));
    return false;
  }
  files.erase(std::remove_if(files.begin(), files.end(),
                             [](const IsoFile& f) { return !PathStartsWith(f.path, kContentDir); }),
              files.end());

  // Step 1: the packages, out of the image (counted as the first half).
  const fs::path staging = user_dir / ".disc2-staging";
  std::error_code ec;
  fs::remove_all(staging, ec);
  const bool copied = CopyFiles(in, files, staging, progress);
  in.close();
  const uint64_t copy_total = progress.total;
  if (!copied) {
    fs::remove_all(staging, ec);
    return false;
  }

  // Step 2: fm4tool installs each package (files + license header).
  progress.total = copy_total * 2;
  progress.done = copy_total;
  size_t installed = 0;
  std::string last_error;
  progress.SetStatus(L("Installing content packages...", "Instalando paquetes de contenido..."));
  const int code = RunTool(
      tool, {"install-dlc", game_dir.u8string(), staging.u8string(), user_dir.u8string()},
      [&](const std::string& line) {
        if (line.find(": installed") != std::string::npos) {
          ++installed;
          progress.done = copy_total + copy_total * installed / files.size();
          progress.SetStatus(std::string(L("Installed ", "Instalado ")) + line.substr(0, line.find(':')));
        } else if (line.find("FAILED") != std::string::npos || line.find("cannot") != std::string::npos) {
          last_error = line;
        }
      },
      progress);
  fs::remove_all(staging, ec);
  if (code != 0 || installed != files.size()) {
    if (!progress.cancel) {
      progress.SetStatus(std::string(L("Installing the content failed", "Falló la instalación del contenido")) +
                         (last_error.empty() ? "." : ": " + last_error));
    }
    return false;
  }
  progress.done = progress.total.load();
  progress.SetStatus(std::string(L("Installed ", "Instalados ")) + std::to_string(installed) +
                     L(" content packages (cars and tracks).", " paquetes de contenido (coches y circuitos)."));
  return true;
}

size_t CountContentPackages(const fs::path& user_dir) {
  const auto installed = ListInstalledContent(user_dir);
  return size_t(std::count_if(installed.begin(), installed.end(), [](const ContentPackage& p) { return p.disc2; }));
}

std::vector<ContentPackage> ScanContentPackages(const fs::path& path, size_t& skipped) {
  std::vector<ContentPackage> found;
  skipped = 0;
  auto consider = [&](const fs::path& file) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec || size < kStfsHeaderSize) return;
    std::ifstream in(file, std::ios::binary);
    uint8_t header[kStfsHeaderSize];
    if (!in.read(reinterpret_cast<char*>(header), sizeof(header))) return;
    if (std::memcmp(header, "LIVE", 4) != 0 && std::memcmp(header, "PIRS", 4) != 0 &&
        std::memcmp(header, "CON ", 4) != 0) {
      return;  // not a package
    }
    if (Be32(header + 0x344) != kMarketplaceContent || Be32(header + 0x360) != kTitleId) {
      ++skipped;
      return;
    }
    ContentPackage package;
    package.path = file;
    package.file_name = file.filename().u8string();
    package.display_name = Utf16BeToUtf8(header + 0x411, 128);
    if (package.display_name.empty()) package.display_name = package.file_name;
    package.size = size;
    package.disc2 = IsDisc2Package(package.file_name);
    found.push_back(std::move(package));
  };
  std::error_code ec;
  if (fs::is_regular_file(path, ec)) {
    consider(path);
  } else {
    for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (it->is_regular_file(ec)) consider(it->path());
    }
  }
  std::sort(found.begin(), found.end(), [](const ContentPackage& a, const ContentPackage& b) {
    return std::tie(a.display_name, a.file_name) < std::tie(b.display_name, b.file_name);
  });
  return found;
}

std::vector<ContentPackage> ListInstalledContent(const fs::path& user_dir) {
  std::vector<ContentPackage> out;
  std::error_code ec;
  const fs::path title = user_dir / "0000000000000000" / "4D530910";
  for (const auto& entry : fs::directory_iterator(title / "00000002", ec)) {
    const std::string name = entry.path().filename().u8string();
    const fs::path header_path = title / "Headers" / "00000002" / (name + ".header");
    if (!entry.is_directory(ec) || !fs::exists(header_path, ec)) continue;
    ContentPackage package;
    package.path = entry.path();
    package.file_name = name;
    // The header is the runtime's XCONTENT_AGGREGATE_DATA: device ID, content
    // type, then the display name (128 big-endian UTF-16 characters).
    uint8_t header[8 + 256] = {};
    std::ifstream in(header_path, std::ios::binary);
    if (in.read(reinterpret_cast<char*>(header), sizeof(header))) {
      package.display_name = Utf16BeToUtf8(header + 8, 128);
    }
    if (package.display_name.empty()) package.display_name = name;
    for (auto it = fs::recursive_directory_iterator(entry.path(), ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (it->is_regular_file(ec)) package.size += it->file_size(ec);
    }
    package.disc2 = IsDisc2Package(name);
    out.push_back(std::move(package));
  }
  std::sort(out.begin(), out.end(), [](const ContentPackage& a, const ContentPackage& b) {
    if (a.disc2 != b.disc2) return a.disc2;
    return std::tie(a.display_name, a.file_name) < std::tie(b.display_name, b.file_name);
  });
  return out;
}

bool InstallContentPackages(const std::vector<fs::path>& packages, const fs::path& game_dir,
                            const fs::path& user_dir, const fs::path& tool, TaskProgress& progress) {
  if (!fs::exists(game_dir / "default.xex")) {
    progress.SetStatus(L("Install Disc 1 first: content is installed for it.",
                         "Instala antes el Disco 1: el contenido se instala para él."));
    return false;
  }
  if (!fs::exists(tool)) {
    progress.SetStatus(std::string(L("Not found: ", "No se encuentra ")) + tool.u8string());
    return false;
  }
  std::vector<std::string> args{"install-package", game_dir.u8string(), user_dir.u8string()};
  std::vector<uint64_t> sizes;
  uint64_t total = 0;
  for (const auto& package : packages) {
    std::error_code ec;
    sizes.push_back(fs::file_size(package, ec));
    total += sizes.back();
    args.push_back(package.u8string());
  }
  progress.total = total;
  progress.done = 0;
  progress.SetStatus(L("Installing content packages...", "Instalando paquetes de contenido..."));
  size_t installed = 0;
  std::string last_error;
  const int code = RunTool(
      tool, args,
      [&](const std::string& line) {
        if (line.find(": installed") != std::string::npos) {
          if (installed < sizes.size()) progress.done += sizes[installed];
          ++installed;
          progress.SetStatus(std::string(L("Installed ", "Instalado ")) + line.substr(0, line.find(':')));
        } else if (line.find("FAILED") != std::string::npos || line.find("cannot") != std::string::npos) {
          last_error = line;
        }
      },
      progress);
  if (code != 0 || installed != packages.size()) {
    if (!progress.cancel) {
      progress.SetStatus(std::string(L("Installing the content failed", "Falló la instalación del contenido")) +
                         (last_error.empty() ? "." : ": " + last_error));
    }
    return false;
  }
  progress.done = progress.total.load();
  progress.SetStatus(std::string(L("Installed ", "Instalados ")) + std::to_string(installed) +
                     L(" content packages.", " paquetes de contenido."));
  return true;
}

std::string RemoveContentPackage(const fs::path& user_dir, const std::string& file_name) {
  if (!IsSafeRelativePath(file_name) || file_name.find_first_of("/\\") != std::string::npos) {
    return L("Invalid package name.", "Nombre de paquete no válido.");
  }
  const fs::path title = user_dir / "0000000000000000" / "4D530910";
  std::error_code ec;
  fs::remove(title / "Headers" / "00000002" / fs::u8path(file_name + ".header"), ec);
  if (ec) return std::string(L("Could not remove ", "No se pudo quitar ")) + file_name + ": " + ec.message();
  fs::remove_all(title / "00000002" / fs::u8path(file_name), ec);
  if (ec) return std::string(L("Could not remove ", "No se pudo quitar ")) + file_name + ": " + ec.message();
  return {};
}

bool ExtractCover(const fs::path& game_dir, const fs::path& tool, const fs::path& cover) {
  std::error_code ec;
  if (!fs::exists(game_dir / "nxeart", ec) || !fs::exists(tool, ec)) return false;
  const fs::path temp = cover.parent_path() / ".nxeart";
  fs::remove_all(temp, ec);
  TaskProgress ignored;
  const int code = RunTool(tool, {"stfs", (game_dir / "nxeart").u8string(), temp.u8string()},
                           [](const std::string&) {}, ignored);
  bool ok = false;
  if (code == 0 && fs::exists(temp / "nxeslot.jpg", ec)) {
    fs::copy_file(temp / "nxeslot.jpg", cover, fs::copy_options::overwrite_existing, ec);
    ok = !ec;
  }
  fs::remove_all(temp, ec);
  return ok;
}

VerifyReport VerifyGame(const fs::path& dir, const std::vector<ManifestEntry>& manifest, bool full_hash,
                        TaskProgress& progress) {
  VerifyReport report;
  std::vector<const ManifestEntry*> to_hash;
  uint64_t total = 0;
  for (const auto& entry : manifest) {
    std::error_code ec;
    const fs::path path = dir / fs::u8path(entry.path);
    const auto size = fs::file_size(path, ec);
    if (ec) {
      report.missing.push_back(entry.path);
    } else if (size != entry.size) {
      report.wrong_size.push_back(entry.path);
    } else if (full_hash) {
      to_hash.push_back(&entry);
      total += entry.size;
    }
  }
  if (!full_hash || !report.missing.empty() || !report.wrong_size.empty()) {
    return report;
  }

  progress.total = total;
  progress.done = 0;
  progress.SetStatus(L("Checking file integrity...", "Comprobando la integridad de los archivos..."));
  std::atomic<size_t> next{0};
  std::mutex bad_mutex;
  auto worker = [&] {
    std::vector<char> buffer(4u << 20);
    for (size_t i; (i = next++) < to_hash.size() && !progress.cancel;) {
      const ManifestEntry& entry = *to_hash[i];
      std::ifstream in(dir / fs::u8path(entry.path), std::ios::binary);
      Sha256 sha;
      while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = in.gcount();
        if (got <= 0) break;
        sha.Update(buffer.data(), size_t(got));
        progress.done += uint64_t(got);
      }
      const std::string digest = sha.HexDigest();
      if (digest != entry.sha256 &&
          std::find(entry.alternatives.begin(), entry.alternatives.end(), digest) == entry.alternatives.end()) {
        std::lock_guard lock(bad_mutex);
        report.wrong_hash.push_back(entry.path);
      }
    }
  };
  std::vector<std::thread> threads;
  const unsigned count = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
  for (unsigned t = 0; t < count; ++t) threads.emplace_back(worker);
  for (auto& t : threads) t.join();
  std::sort(report.wrong_hash.begin(), report.wrong_hash.end());
  return report;
}

fs::path VerifiedMarker(const fs::path& game_dir) { return game_dir / ".fm4-verified"; }


}  // namespace launcher
