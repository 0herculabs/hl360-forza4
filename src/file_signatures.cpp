// FM4 USA Rev 1: protected-file table at 0x82EA0000, 66 entries.
// Rebuild only for a separate Project Forza Plus installation.
// Big-endian block size/count, offset/size index, NUL names and SHA-256 per 32 KiB.
#include "file_signatures.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include <cstring>
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>
#include <stdexcept>
#include "../launcher/src/safe_path.h"

#include <rex/logging/macros.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>

namespace fm4 {
namespace {

constexpr uint32_t kTableAddress = 0x82EA0000;
constexpr uint32_t kBlockSize = 0x8000;
constexpr size_t kHashSize = 32;

uint32_t LoadBe32(const uint8_t* p) {
  return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void StoreBe32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
}

bool Sha256(const uint8_t* data, size_t size, uint8_t out[kHashSize]) {
  return BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<uint8_t*>(data),
                                   ULONG(size), out, kHashSize));
}

struct Entry {
  std::string name;
  uint32_t size;
};

}  // namespace

void ResignProtectedFiles(rex::Runtime* runtime, const std::filesystem::path& game_root) {
  if (!std::filesystem::exists(game_root / ".fm4-plus-ready")) {
    return;
  }
  uint8_t* table = runtime->memory()->TranslateVirtual(kTableAddress);
  if (!table || LoadBe32(table) != kBlockSize) {
    throw std::runtime_error("FM4 signature table is unavailable");
  }
  const uint32_t count = LoadBe32(table + 4);
  if (count != 66) {
    throw std::runtime_error("FM4 signature table has an unexpected entry count");
  }
  const uint32_t data_start = 8 + count * 8;

  // Read the original index and the space it occupies.
  std::vector<Entry> entries;
  uint32_t table_end = data_start;
  constexpr uint32_t capacity = 0x5771; // Resource 0D163575; XDBF follows at 0x82EA5780.
  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t offset = LoadBe32(table + 8 + i * 8);
    const uint32_t size = LoadBe32(table + 12 + i * 8);
    if (offset >= capacity - data_start - 128 || size > (256u << 20))
      throw std::runtime_error("FM4 signature table: invalid entry");
    const char* name = reinterpret_cast<const char*>(table + data_start + offset);
    const size_t name_len = strnlen(name, 128);
    if (name_len == 0 || name_len == 128) {
      throw std::runtime_error("FM4 signature table has a malformed entry");
    }
    std::string relative(name, name_len);
    for (char& c : relative) if (c == '\\') c = '/';
    if (!launcher::IsSafeRelativePath(relative))
      throw std::runtime_error("FM4 signature table: unsafe file name");
    const uint32_t blocks = (size + kBlockSize - 1) / kBlockSize;
    table_end = std::max<uint32_t>(table_end,
                                   data_start + offset + uint32_t(name_len) + 1 + blocks * kHashSize);
    entries.push_back({std::string(name, name_len), size});
  }
  if (table_end > capacity) throw std::runtime_error("FM4 signature table exceeds reserved area");

  // Rebuild it from the files on disk.
  std::vector<uint8_t> rebuilt(data_start, 0);
  StoreBe32(rebuilt.data(), kBlockSize);
  StoreBe32(rebuilt.data() + 4, count);
  int changed = 0;
  for (uint32_t i = 0; i < count; ++i) {
    std::string rel = entries[i].name;
    for (char& c : rel) c = c == '\\' ? '/' : c;
    const std::filesystem::path path = game_root / std::filesystem::path(rel); // ASCII names in this XEX.
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      throw std::runtime_error("Missing protected FM4 file: " + path.string());
    }
    const std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const uint32_t offset = uint32_t(rebuilt.size() - data_start);
    StoreBe32(rebuilt.data() + 8 + i * 8, offset);
    StoreBe32(rebuilt.data() + 12 + i * 8, uint32_t(data.size()));
    rebuilt.insert(rebuilt.end(), entries[i].name.begin(), entries[i].name.end());
    rebuilt.push_back(0);
    for (size_t pos = 0; pos < data.size() || (data.empty() && pos == 0); pos += kBlockSize) {
      uint8_t hash[kHashSize];
      const size_t len = std::min<size_t>(kBlockSize, data.size() - pos);
      if (!Sha256(reinterpret_cast<const uint8_t*>(data.data()) + pos, len, hash)) {
        throw std::runtime_error("FM4 protected-file SHA-256 failed");
      }
      rebuilt.insert(rebuilt.end(), hash, hash + kHashSize);
      if (data.empty()) break;
    }
    if (data.size() != entries[i].size) ++changed;
  }

  if (rebuilt.size() == table_end && std::memcmp(rebuilt.data(), table, rebuilt.size()) == 0) {
    REXLOG_INFO("File signatures: all {} protected files match the shipped table", count);
    return;
  }
  // Larger databases need more hashes. Relocate the resource through its XEX
  // directory; never overwrite the adjacent dashboard resource or guest code.
  auto module = runtime->kernel_state()->GetExecutableModule();
  rex::xex2_opt_resource_info* resources = nullptr;
  if (module->GetOptHeader(rex::XEX_HEADER_RESOURCE_INFO, &resources) != rex::X_STATUS(0) || !resources)
    throw std::runtime_error("FM4 resource directory is unavailable");
  rex::xex2_resource* resource = nullptr;
  const uint32_t resource_count = (resources->size - 4) / sizeof(rex::xex2_resource);
  for (uint32_t i = 0; i < resource_count; ++i) {
    auto& r = resources->resources[i];
    if (std::memcmp(r.name, "0D163575", 8) == 0 && r.address == kTableAddress && r.size == capacity)
      resource = &r;
  }
  if (!resource) throw std::runtime_error("FM4 protected-file resource does not match USA Rev 1");
  if (rebuilt.size() > (1u << 20)) throw std::runtime_error("FM4 signature resource is too large");
  const uint32_t address = runtime->memory()->SystemHeapAlloc(uint32_t(rebuilt.size()));
  if (!address) throw std::runtime_error("Cannot allocate FM4 signature resource");
  std::memcpy(runtime->memory()->TranslateVirtual(address), rebuilt.data(), rebuilt.size());
  const auto offset = reinterpret_cast<const uint8_t*>(resource) - reinterpret_cast<const uint8_t*>(module->xex_header());
  auto* guest_resource = reinterpret_cast<rex::xex2_resource*>(
      runtime->memory()->TranslateVirtual(module->guest_xex_header()) + offset);
  resource->address = address;
  resource->size = uint32_t(rebuilt.size());
  guest_resource->address = address;
  guest_resource->size = uint32_t(rebuilt.size());
  REXLOG_INFO("File signatures: table rebuilt from the files on disk ({} of {} changed size)",
              changed, count);
}

}  // namespace fm4

