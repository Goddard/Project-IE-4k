#ifndef __MVE_H__
#define __MVE_H__

#include <string>
#include <vector>

#include "plugins/PluginBase.h"

#define MVE_PALETTE_COUNT 256

#define MVE_DEFAULT_AUDIO_STREAM 0x01

// interplay mve definitions
/* MVE chunk types */
#define MVE_CHUNK_INIT_AUDIO 0x0000
#define MVE_CHUNK_AUDIO_ONLY 0x0001
#define MVE_CHUNK_INIT_VIDEO 0x0002
#define MVE_CHUNK_VIDEO 0x0003
#define MVE_CHUNK_SHUTDOWN 0x0004
#define MVE_CHUNK_END 0x0005

/* MVE segment opcodes */
#define MVE_OC_END_OF_STREAM 0x00
#define MVE_OC_END_OF_CHUNK 0x01
#define MVE_OC_CREATE_TIMER 0x02
#define MVE_OC_AUDIO_BUFFERS 0x03
#define MVE_OC_PLAY_AUDIO 0x04
#define MVE_OC_VIDEO_BUFFERS 0x05
#define MVE_OC_PLAY_VIDEO 0x07
#define MVE_OC_AUDIO_DATA 0x08
#define MVE_OC_AUDIO_SILENCE 0x09
#define MVE_OC_VIDEO_MODE 0x0A
#define MVE_OC_PALETTE 0x0C
#define MVE_OC_PALETTE_COMPRESSED 0x0D
#define MVE_OC_CODE_MAP 0x0F
#define MVE_OC_VIDEO_DATA 0x11

/* audio flags */
#define MVE_AUDIO_STEREO 0x0001
#define MVE_AUDIO_16BIT 0x0002
#define MVE_AUDIO_COMPRESSED 0x0004

/* video flags */
#define MVE_VIDEO_DELTA_FRAME 0x0001

namespace ProjectIE4k {

// Helpers to read little-endian values safely
static inline bool read_u8(const uint8_t *&p, size_t &rem, uint8_t &out) {
  if (rem < 1)
    return false;
  out = *p++;
  rem -= 1;
  return true;
}
static inline bool read_u16(const uint8_t *&p, size_t &rem, uint16_t &out) {
  if (rem < 2)
    return false;
  out = static_cast<uint16_t>(p[0] | (p[1] << 8));
  p += 2;
  rem -= 2;
  return true;
}
static inline bool read_u32(const uint8_t *&p, size_t &rem, uint32_t &out) {
  if (rem < 4)
    return false;
  out = static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
  p += 4;
  rem -= 4;
  return true;
}
static inline bool read_bytes(const uint8_t *&p, size_t &rem,
                              const uint8_t *&start, size_t len) {
  if (rem < len)
    return false;
  start = p;
  p += len;
  rem -= len;
  return true;
}

// PNG ARGB composer from 8-bit indexed
static inline void
compose_argb_from_indexed(const uint8_t *indices, int width, int height,
                          const std::array<uint8_t, 256 * 3> &palette,
                          std::vector<uint32_t> &argbOut) {
  argbOut.resize(static_cast<size_t>(width) * height);
  for (int y = 0; y < height; ++y) {
    const uint8_t *row = indices + static_cast<size_t>(y) * width;
    for (int x = 0; x < width; ++x) {
      uint8_t idx = row[x];
      uint8_t r = palette[idx * 3 + 0];
      uint8_t g = palette[idx * 3 + 1];
      uint8_t b = palette[idx * 3 + 2];
      argbOut[static_cast<size_t>(y) * width + x] =
          (0xFFu << 24) | (static_cast<uint32_t>(r) << 16) |
          (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    }
  }
}

// PNG ARGB composer from 16-bit RGB555
static inline void compose_argb_from_rgb555(const uint16_t *pixels, int width,
                                            int height,
                                            std::vector<uint32_t> &argbOut) {
  argbOut.resize(static_cast<size_t>(width) * height);
  for (int y = 0; y < height; ++y) {
    const uint16_t *row = pixels + static_cast<size_t>(y) * width;
    for (int x = 0; x < width; ++x) {
      uint16_t v = row[x];
      // MVE uses RGB555 format (5-5-5), not RGB565 (5-6-5)
      uint8_t r = static_cast<uint8_t>(((v >> 10) & 0x1F) << 3);
      uint8_t g = static_cast<uint8_t>(((v >> 5) & 0x1F) << 3);
      uint8_t b = static_cast<uint8_t>((v & 0x1F) << 3);
      // Expand 5-bit values to 8-bit by replicating high bits
      r |= (r >> 5);
      g |= (g >> 5);
      b |= (b >> 5);
      argbOut[static_cast<size_t>(y) * width + x] =
          (0xFFu << 24) | (static_cast<uint32_t>(r) << 16) |
          (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
    }
  }
}

// Scale palette entries if values appear to be in 0..63 range
static inline void
maybe_scale_palette_0_63_to_255(std::array<uint8_t, 256 * 3> &pal, int start,
                                int count) {
  int end = std::min(256, start + count);
  bool needsScale = false;
  for (int i = start; i < end; ++i) {
    if (pal[i * 3 + 0] <= 63 && pal[i * 3 + 1] <= 63 && pal[i * 3 + 2] <= 63) {
      needsScale = true; // likely VGA scale
    } else {
      needsScale =
          false; // at least one >63 in this range; do not scale this range
      break;
    }
  }
  if (!needsScale)
    return;
  for (int i = start; i < end; ++i) {
    pal[i * 3 + 0] = static_cast<uint8_t>((pal[i * 3 + 0] * 255 + 31) / 63);
    pal[i * 3 + 1] = static_cast<uint8_t>((pal[i * 3 + 1] * 255 + 31) / 63);
    pal[i * 3 + 2] = static_cast<uint8_t>((pal[i * 3 + 2] * 255 + 31) / 63);
  }
}

class MVE : public PluginBase {
public:
  explicit MVE(const std::string &resourceName);
  ~MVE() override;

  // Core operations
  bool extract() override;  // Extract frames as PNGs
  bool assemble() override; // No-op or copy behavior

  // Batch operations (delegated to PluginManager)
  bool extractAll() override;
  bool upscaleAll() override;
  bool assembleAll() override;

  // Clean directories before operations - operation-specific
  bool cleanExtractDirectory() override;
  bool cleanUpscaleDirectory() override;
  bool cleanAssembleDirectory() override;
  bool cleanDirectory(const std::string &dir);

  // Metadata
  std::string getPluginName() const override { return "MVE"; }
  SClass_ID getResourceType() const override { return IE_MVE_CLASS_ID; }
  std::string getResourceName() const override { return resourceName_; }
  bool isValid() const override { return valid_; }

  // Paths
  std::string getOutputDir(bool ensureDir = true) const override;
  std::string getExtractDir(bool ensureDir = true) const override;
  std::string getUpscaledDir(bool ensureDir = true) const override;
  std::string getAssembleDir(bool ensureDir = true) const override;

  // Command registration
  static void registerCommands(CommandTable &commandTable);

private:
  // Audio segment structure for preserving original audio
  struct AudioSegment {
    uint8_t segType;
    uint8_t segVer;
    std::vector<uint8_t> payload;
  };

  // Represents a single MVE frame, including its associated audio and sync
  // opcodes
  struct MVEFrame {
    std::vector<AudioSegment> opcodes;
    // We will populate the pixel data for this frame during assembly
  };

  // Assembly helper functions
  bool assembleMVEFile(const std::vector<std::string> &frameFiles,
                       const std::string &outputFile, int width, int height,
                       const std::vector<MVE::MVEFrame> &frames);
  bool assembleMOVFile(const std::string &outputPath, double fps);
  bool extractAudioToWAV(const std::string &outputPath);
  bool buildFrameAudioMap(std::vector<MVEFrame> &frames);
  bool testDecodingAssembledFile(const std::string &assembledFilePath);
};

} // namespace ProjectIE4k

#endif /* __MVE_H__ */