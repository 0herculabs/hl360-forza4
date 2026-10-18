#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace launcher {

// Plain SHA-256 (FIPS 180-4), used to verify imported game files.
class Sha256 {
 public:
  Sha256();
  void Update(const void* data, size_t size);
  std::string HexDigest();  // finalizes

 private:
  void Block(const uint8_t* p);
  uint32_t state_[8];
  uint8_t buffer_[64];
  size_t buffered_ = 0;
  uint64_t length_ = 0;
};

}  // namespace launcher
