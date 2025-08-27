#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "core/Logging/Logging.h"

namespace ProjectIE4k {

// PLT v1 header and pixel layout per IESDP
// Signature: "PLT ", Version: "V1 "
// Four unknown 16-bit fields, then width/height as 32-bit each.
// Body: For each pixel, two bytes: column (x), then row (y), order left->right,
// bottom->top.

struct PLTV1Header {
  char signature[4]; // 'P','L','T',' '
  char version[4];   // 'V','1',' '
  uint16_t unknown0; // unknown
  uint16_t unknown1; // unknown
  uint16_t unknown2; // unknown
  uint16_t unknown3; // unknown
  uint32_t width;    // image width
  uint32_t height;   // image height

  bool isValid() const {
    return std::memcmp(signature, "PLT ", 4) == 0 &&
           std::memcmp(version, "V1 ", 3) == 0;
  }
};

struct PLTV1Pixel {
  uint8_t column; // MPAL256.bmp column index
  uint8_t row;    // MPAL256.bmp row index
};

class PLTV1File {
public:
  PLTV1Header header{};
  std::vector<PLTV1Pixel>
      pixels; // Stored in file order: left->right, bottom->top

  void deserialize(const std::vector<uint8_t> &fileData) {
    if (fileData.size() < sizeof(PLTV1Header)) {
      throw std::runtime_error("PLTV1: file too small for header");
    }

    std::memcpy(&header, fileData.data(), sizeof(PLTV1Header));
    if (!header.isValid()) {
      Log(ERROR, "PLT",
          "Invalid PLT signature/version: '{:c}{:c}{:c}{:c}' "
          "'{:c}{:c}{:c}{:c}'",
          header.signature[0], header.signature[1], header.signature[2],
          header.signature[3], header.version[0], header.version[1],
          header.version[2], header.version[3]);
      throw std::runtime_error("PLTV1: invalid signature/version");
    }

    const uint64_t expectedPixels = static_cast<uint64_t>(header.width) *
                                    static_cast<uint64_t>(header.height);
    const uint64_t expectedBodyBytes = expectedPixels * 2ull;
    const uint64_t actualBodyBytes = fileData.size() - sizeof(PLTV1Header);
    if (actualBodyBytes < expectedBodyBytes) {
      throw std::runtime_error("PLTV1: file too small for pixel data");
    }

    pixels.resize(static_cast<size_t>(expectedPixels));
    const uint8_t *body = fileData.data() + sizeof(PLTV1Header);
    for (uint64_t i = 0; i < expectedPixels; ++i) {
      pixels[static_cast<size_t>(i)] =
          PLTV1Pixel{body[i * 2 + 0], body[i * 2 + 1]};
    }
  }

  std::vector<uint8_t> serialize() const {
    if (!header.isValid()) {
      throw std::runtime_error("PLTV1: cannot serialize invalid header");
    }
    const uint64_t expectedPixels = static_cast<uint64_t>(header.width) *
                                    static_cast<uint64_t>(header.height);
    if (pixels.size() != expectedPixels) {
      throw std::runtime_error(
          "PLTV1: pixel count does not match width*height");
    }

    std::vector<uint8_t> out;
    out.resize(sizeof(PLTV1Header) + pixels.size() * 2);
    std::memcpy(out.data(), &header, sizeof(PLTV1Header));
    uint8_t *body = out.data() + sizeof(PLTV1Header);
    for (size_t i = 0; i < pixels.size(); ++i) {
      body[i * 2 + 0] = pixels[i].column;
      body[i * 2 + 1] = pixels[i].row;
    }
    return out;
  }
};

} // namespace ProjectIE4k
