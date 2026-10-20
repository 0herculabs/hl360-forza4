#include <rex/filesystem.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/file.h>
#include <array>
#include <iostream>
#include <memory>
#include <stdexcept>

int main(int argc, char** argv) {
  try {
    if (argc != 2) throw std::runtime_error("Pass the read-only Disc 1 directory");
    bool found_media = false;
    for (const auto& info : rex::filesystem::ListFiles(argv[1])) {
      if (info.name == "Media") {
        if (info.type != rex::filesystem::FileInfo::Type::kDirectory)
          throw std::runtime_error("Media was incorrectly classified as a file");
        found_media = true;
      }
    }
    if (!found_media) throw std::runtime_error("Media directory missing");
    rex::filesystem::HostPathDevice device("\\Device\\DiscReadCheck", argv[1], true);
    if (!device.Initialize()) throw std::runtime_error("Read-only device initialization failed");
    auto* entry = device.ResolvePath("media\\ui\\textures.zip");
    if (!entry) throw std::runtime_error("Exact failing guest path did not resolve");
    rex::filesystem::File* raw = nullptr;
    if (entry->Open(rex::filesystem::FileAccess::kFileReadData, &raw) != 0 || !raw)
      throw std::runtime_error("Resolved archive could not be opened");
    std::unique_ptr<rex::filesystem::File> file(raw);
    std::array<uint8_t, 4> header{};
    size_t read = 0;
    if (file->ReadSync(header, 0, &read) != 0 || read != header.size() ||
        header[0] != 'P' || header[1] != 'K')
      throw std::runtime_error("Archive header could not be read");
    std::cout << "Read-only disc check passed: media\\ui\\textures.zip resolved and ZIP header read.\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
