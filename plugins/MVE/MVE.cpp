#include "MVE.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cstring>
#include <filesystem>
#include <map>

#include "core/Logging/Logging.h"
#include "core/SClassID.h"
#include "plugins/PluginManager.h"
#include "gstmvedemux.h"
#include <opencv2/opencv.hpp>

namespace fs = std::filesystem;

namespace ProjectIE4k {

MVE::MVE(const std::string& resourceName) : PluginBase(resourceName, IE_MVE_CLASS_ID) {
  if (resourceName_.empty() || originalFileData.empty()) {
    Log(ERROR, "MVE", "Invalid or empty MVE resource: {}", resourceName_);
    valid_ = false;
    return;
  }
  
  valid_ = true;
  Log(DEBUG, "MVE", "Initialized MVE plugin for resource: {} ({} bytes)", resourceName_, originalFileData.size());
}

MVE::~MVE() {}

bool MVE::extract() {
  Log(MESSAGE, "MVE", "Starting MVE extraction for resource: {}", resourceName_);
  if (!isValid()) return false;

  std::string outputDir = getExtractDir(true);
  if (outputDir.empty()) {
      Log(ERROR, "MVE", "Failed to create output directory.");
      return false;
  }

  const uint8_t* p = originalFileData.data();
  size_t rem = originalFileData.size();

  // Header check: "Interplay MVE File\x1A\0" then 6 bytes (commonly 00 1a 00 01 33 11 little-endian words)
  static const char hdrStr[] = "Interplay MVE File";
  if (rem < 20 || std::memcmp(p, hdrStr, 18) != 0 || p[18] != 0x1A || p[19] != 0x00) {
    Log(ERROR, "MVE", "Invalid MVE header for resource: {}", resourceName_);
    return false;
  }
  p += 20; rem -= 20;
  if (rem < 6) {
    Log(ERROR, "MVE", "Truncated MVE header (missing 6-byte trailer)");
    return false;
  }
  // Skip trailer, but log if not the typical pattern
  uint16_t t0 = static_cast<uint16_t>(p[0] | (p[1] << 8));
  uint16_t t1 = static_cast<uint16_t>(p[2] | (p[3] << 8));
  uint16_t t2 = static_cast<uint16_t>(p[4] | (p[5] << 8));
  if (!(t0 == 0x001a && t1 == 0x0100 && t2 == 0x1133)) {
    Log(WARNING, "MVE", "Unexpected MVE header trailer: {:04x} {:04x} {:04x}", t0, t1, t2);
  }
  p += 6; rem -= 6;

  // Initialize video decoder state
  GstMveDemuxStream s{};
  s.width = 0;
  s.height = 0;
  s.back_buf1 = nullptr;
  s.back_buf2 = nullptr;
  s.code_map = nullptr;
  s.max_block_offset = 0;

  bool video16 = false;
  bool haveVideoInit = false;
  bool havePalette = false;
  std::array<uint8_t, 256 * 3> palette{};
  palette.fill(0);
  std::vector<uint8_t> codeMap;
  std::vector<uint8_t> videoBackBuffer8;
  std::vector<uint16_t> videoBackBuffer16;
  uint32_t frameIndex = 0;

  auto swapBackBuffers = [&]() {
    std::swap(s.back_buf1, s.back_buf2);
  };

  // GemRB format: [u16 chunkSize][u16 chunkType], then segments until 'chunkSize' bytes consumed
  size_t chunkIndex = 0;
  while (rem >= 4) {
    uint16_t chunkSize = 0, chunkType = 0;
    if (!read_u16(p, rem, chunkSize) || !read_u16(p, rem, chunkType)) break;
    if (rem < chunkSize) { Log(ERROR, "MVE", "Truncated chunk (need {}, have {})", chunkSize, rem); return false; }
    const uint8_t* cptr = p;
    size_t crem = chunkSize;
    p += chunkSize; rem -= chunkSize;

    if (chunkIndex < 3) {
      Log(DEBUG, "MVE", "Chunk[{}]: type=0x{:04x} payloadLen={} remBefore={}",
          static_cast<unsigned>(chunkIndex), chunkType, crem, rem + crem);
    }

    // Process opcodes in this chunk
    bool endOfChunk = false;
    while (!endOfChunk && crem >= 4) {
      uint16_t segLen = 0; uint8_t segType = 0, segVer = 0;
      if (!read_u16(cptr, crem, segLen)) break;
      if (crem < 2) break;
      segType = *cptr++; crem -= 1;
      segVer = *cptr++; crem -= 1;
      // Segment length is payload length (excludes 4-byte header)
      size_t payloadLen = static_cast<size_t>(segLen);
      if (crem < payloadLen) { Log(ERROR, "MVE", "Opcode payload truncated: need {}, have {}", payloadLen, crem); return false; }
      const uint8_t* payload = cptr;
      cptr += payloadLen;
      crem -= payloadLen;

      switch (segType) {
        case MVE_OC_END_OF_STREAM: {
          Log(DEBUG, "MVE", "End of stream opcode encountered (treating as end-of-chunk)");
          endOfChunk = true;
          break;
        }
        case MVE_OC_END_OF_CHUNK: {
          endOfChunk = true;
          break;
        }
        case MVE_OC_VIDEO_BUFFERS: {
          // Initialize video
          const uint8_t* vp = payload; size_t vrem = payloadLen;
          uint16_t wBlocks = 0, hBlocks = 0;
          // GemRB reads 8 bytes and for version>1 checks a format flag at +6
          if (!read_u16(vp, vrem, wBlocks) || !read_u16(vp, vrem, hBlocks)) {
            Log(ERROR, "MVE", "Truncated VIDEO_BUFFERS header");
            return false;
          }
          // Skip next 2 bytes (buffer count or unused), then optional format at +6
          uint16_t dummy = 0; if (vrem >= 2) { read_u16(vp, vrem, dummy); }
          uint16_t format = 0; if (segVer > 1 && vrem >= 2) { read_u16(vp, vrem, format); }
          video16 = (format > 0);

          s.width = static_cast<uint16_t>(wBlocks) << 3; // blocks to pixels
          s.height = static_cast<uint16_t>(hBlocks) << 3;
          // compute max starting offset to fit an 8x8 block
          if (s.width < 8 || s.height < 8) {
            Log(ERROR, "MVE", "Invalid dimensions {}x{}", s.width, s.height);
            return false;
          }
          s.max_block_offset = static_cast<uint32_t>((static_cast<uint32_t>(s.width) * s.height) - 1);

          // (Re)allocate buffers
          codeMap.clear();
          s.code_map = nullptr;
          if (!video16) {
            size_t half = static_cast<size_t>(s.width) * s.height;
            videoBackBuffer8.assign(half * 2, 0);
            s.back_buf1 = reinterpret_cast<guint16*>(videoBackBuffer8.data());
            s.back_buf2 = reinterpret_cast<guint16*>(videoBackBuffer8.data() + half);
          } else {
            size_t half = static_cast<size_t>(s.width) * s.height;
            videoBackBuffer16.assign(half * 2, 0);
            s.back_buf1 = videoBackBuffer16.data();
            s.back_buf2 = videoBackBuffer16.data() + half;
          }
          haveVideoInit = true;
          Log(DEBUG, "MVE", "Video init: {}x{} {}-bit", s.width, s.height, video16 ? 16 : 8);
          break;
        }
        case MVE_OC_PALETTE: {
          if (payloadLen < 4) { Log(ERROR, "MVE", "Palette opcode too short"); return false; }
          uint16_t start = static_cast<uint16_t>(payload[0] | (payload[1] << 8));
          uint16_t count = static_cast<uint16_t>(payload[2] | (payload[3] << 8));
          if (count == 0) count = 256;
          if (payloadLen < 4 + count * 3) { Log(ERROR, "MVE", "Palette payload too short: {} < {}", payloadLen, 4 + count * 3); return false; }
          const uint8_t* pp = payload + 4;
          for (uint16_t i = 0; i < count; ++i) {
            palette[(start + i) * 3 + 0] = pp[i * 3 + 0] << 2;
            palette[(start + i) * 3 + 1] = pp[i * 3 + 1] << 2;
            palette[(start + i) * 3 + 2] = pp[i * 3 + 2] << 2;
          }
          havePalette = true;
          Log(DEBUG, "MVE", "Palette set (uncompressed): start={}, count={}", start, count);
          break;
        }
        case MVE_OC_PALETTE_COMPRESSED: {
          // 32 groups of 8 entries: <mask> then present RGB triplets
          const uint8_t* pp = payload; size_t prem = payloadLen;
          int palIndex = 0;
          for (int group = 0; group < 32 && prem > 0; ++group) {
            uint8_t mask = 0; if (!read_u8(pp, prem, mask)) break;
            for (int bit = 0; bit < 8 && palIndex < 256; ++bit, ++palIndex) {
              if (mask & (1u << bit)) {
                if (prem < 3) { Log(ERROR, "MVE", "Compressed palette truncated"); return false; }
                palette[palIndex * 3 + 0] = *pp++; palette[palIndex * 3 + 1] = *pp++; palette[palIndex * 3 + 2] = *pp++; prem -= 3;
              }
            }
          }
          maybe_scale_palette_0_63_to_255(palette, 0, 256);
          havePalette = true;
          break;
        }
        case MVE_OC_CODE_MAP: {
          // Expect one nibble per 8x8 block; bytes = (xx*yy)/2
          int xx = s.width >> 3;
          int yy = s.height >> 3;
          size_t expectedBytes = static_cast<size_t>(xx) * yy / 2;
          if (payloadLen != expectedBytes) {
            Log(WARNING, "MVE", "CODE_MAP size {} != expected {} ({}x{} blocks)", payloadLen, expectedBytes, xx, yy);
          }
          codeMap.resize(expectedBytes, 0);
          size_t toCopy = std::min(expectedBytes, payloadLen);
          std::memcpy(codeMap.data(), payload, toCopy);
          s.code_map = const_cast<guint8*>(reinterpret_cast<const guint8*>(codeMap.data()));
          // Log first few bytes of code map for debugging
          size_t sampleN = std::min<size_t>(toCopy, 8);
          std::string sample;
          for (size_t i = 0; i < sampleN; ++i) {
            char tmp[8]; std::snprintf(tmp, sizeof(tmp), "%02x ", codeMap[i]); sample += tmp;
          }
          Log(DEBUG, "MVE", "CODE_MAP set: bytes={}, sample=[{}]", toCopy, sample);
          break;
        }
        case MVE_OC_VIDEO_DATA: {
          if (!haveVideoInit) { Log(ERROR, "MVE", "VIDEO_DATA before VIDEO_BUFFERS"); return false; }
          if (s.code_map == nullptr) { Log(ERROR, "MVE", "VIDEO_DATA without CODE_MAP"); return false; }
          // GemRB: skip 12 bytes, read 2-byte flags, then payloadLen -= 14
          if (payloadLen < 14) { Log(ERROR, "MVE", "VIDEO_DATA too short: {}", payloadLen); return false; }
          const uint8_t* dp = payload;
          size_t drem = payloadLen;
          // skip 12 bytes
          dp += 12; drem -= 12;
          // read flags (we still parse them for logging/validation)
          uint16_t flags = static_cast<uint16_t>(dp[0] | (dp[1] << 8));
          dp += 2; drem -= 2;
          // Always swap buffers before decoding, per decoder expectation
          {
            guint16* temp = s.back_buf1; s.back_buf1 = s.back_buf2; s.back_buf2 = temp;
          }
          Log(DEBUG, "MVE", "VIDEO_DATA: flags=0x{:04x}, payload={} bytes", flags, drem);
          int rc = 0;
          if (!video16) rc = ipvideo_decode_frame8(&s, dp, static_cast<uint16_t>(drem));
          else rc = ipvideo_decode_frame16(&s, dp, static_cast<uint16_t>(drem));
          if (rc != 0) { Log(ERROR, "MVE", "Decoder returned error {}", rc); return false; }
          // Compute a simple checksum of back buffer to see changes over frames
          if (!video16) {
            const uint8_t* buf = reinterpret_cast<const uint8_t*>(s.back_buf1);
            size_t sz = static_cast<size_t>(s.width) * s.height;
            uint32_t sum = 0; for (size_t i = 0; i < sz; i += (sz/64 + 1)) sum = (sum * 131) + buf[i];
            Log(DEBUG, "MVE", "Frame buffer checksum (8b): 0x{:08x}", sum);
          } else {
            const uint16_t* buf = reinterpret_cast<const uint16_t*>(s.back_buf1);
            size_t sz = static_cast<size_t>(s.width) * s.height;
            uint32_t sum = 0; for (size_t i = 0; i < sz; i += (sz/64 + 1)) sum = (sum * 131) + buf[i];
            Log(DEBUG, "MVE", "Frame buffer checksum (16b): 0x{:08x}", sum);
          }
          break;
        }
        case MVE_OC_PLAY_VIDEO: {
          if (!haveVideoInit) break; // nothing to show
          // Emit current frame as PNG using back_buf1
          std::vector<uint32_t> argb;
          if (!video16) {
            const uint8_t* indices = reinterpret_cast<const uint8_t*>(s.back_buf1);
            compose_argb_from_indexed(indices, s.width, s.height, palette, argb);
          } else {
            const uint16_t* rgb = reinterpret_cast<const uint16_t*>(s.back_buf1);
            compose_argb_from_rgb555(rgb, s.width, s.height, argb);
          }
          char namebuf[32];
          std::snprintf(namebuf, sizeof(namebuf), "_%04u.png", frameIndex + 1);
          std::string outFile = outputDir + "/" + extractBaseName() + namebuf;
          if (!savePNG(outFile, argb, s.width, s.height)) {
            Log(ERROR, "MVE", "Failed to write PNG: {}", outFile);
            return false;
          }
          ++frameIndex;
          break;
        }
        default: {
          // We ignore audio and other opcodes for frame extraction
          break;
        }
      }
    }

    if (chunkType == MVE_CHUNK_SHUTDOWN || chunkType == MVE_CHUNK_END) {
      break;
    }
  }

  Log(MESSAGE, "MVE", "Extracted {} frames from {}", static_cast<unsigned>(frameIndex), resourceName_);
  return frameIndex > 0;
}

bool MVE::assembleMOVFile(const std::string& outputPath, double fps) {
  Log(MESSAGE, "MVE", "Starting MOV assembly with audio for: {} -> {}", resourceName_, outputPath);
  
  // Find all extracted PNG frames
  std::string upscaledDir = getUpscaledDir();
  std::string baseName = extractBaseName();
  std::vector<std::string> frameFiles;
  
  try {
    if (!fs::exists(upscaledDir)) {
      Log(ERROR, "MVE", "Upscaled directory does not exist: {}", upscaledDir);
      return false;
    }
    
    // Collect all frame files in order
    for (const auto& entry : fs::directory_iterator(upscaledDir)) {
      if (entry.is_regular_file() && entry.path().extension() == ".png") {
        std::string filename = entry.path().filename().string();
        if (filename.find(baseName + "_") == 0) {
          frameFiles.push_back(entry.path().string());
        }
      }
    }
    
    if (frameFiles.empty()) {
      Log(ERROR, "MVE", "No frame files found in: {}", upscaledDir);
      return false;
    }
    
    // Sort frame files by frame number
    std::sort(frameFiles.begin(), frameFiles.end(), [&baseName](const std::string& a, const std::string& b) {
      auto extractFrameNum = [&baseName](const std::string& path) -> int {
        std::string filename = fs::path(path).filename().string();
        std::string prefix = baseName + "_";
        if (filename.find(prefix) == 0) {
          std::string numStr = filename.substr(prefix.length());
          numStr = numStr.substr(0, numStr.find('.'));
          return std::stoi(numStr);
        }
        return -1;
      };
      return extractFrameNum(a) < extractFrameNum(b);
    });
    
    Log(MESSAGE, "MVE", "Found {} frame files for MOV assembly", frameFiles.size());
    
    // Read the first frame to get dimensions
    cv::Mat firstFrame = cv::imread(frameFiles[0]);
    if (firstFrame.empty()) {
      Log(ERROR, "MVE", "Failed to read first frame: {}", frameFiles[0]);
      return false;
    }
    
    int width = firstFrame.cols;
    int height = firstFrame.rows;
    
    Log(MESSAGE, "MVE", "Frame dimensions: {}x{}, FPS: {}", width, height, fps);
    
    // Extract audio to temporary WAV file
    std::string tempAudioFile = getAssembleDir() + "/" + baseName + "_audio.wav";
    bool hasAudio = extractAudioToWAV(tempAudioFile);
    
    if (hasAudio) {
      Log(MESSAGE, "MVE", "Audio extracted to: {}", tempAudioFile);
      
      // Create a GStreamer pipeline for MOV with audio
      std::string gstPipeline = 
        "appsrc ! videoconvert ! video/x-raw,format=I420 ! x264enc ! queue ! mux. "
        "filesrc location=" + tempAudioFile + " ! wavparse ! audioconvert ! audioresample ! queue ! mux. "
        "qtmux name=mux ! filesink location=" + outputPath;
      
      Log(DEBUG, "MVE", "GStreamer pipeline: {}", gstPipeline);
      
      cv::VideoWriter writer;
      if (!writer.open(gstPipeline, cv::CAP_GSTREAMER, 0, fps, cv::Size(width, height), true)) {
        Log(ERROR, "MVE", "Failed to create GStreamer pipeline for MOV with audio");
        
        // Try a fallback approach without audio first
        Log(DEBUG, "MVE", "Trying fallback: video-only MOV creation");
        std::string fallbackPipeline = 
          "appsrc ! videoconvert ! video/x-raw,format=I420 ! x264enc ! qtmux ! filesink location=" + outputPath;
        
        if (writer.open(fallbackPipeline, cv::CAP_GSTREAMER, 0, fps, cv::Size(width, height), false)) {
          Log(WARNING, "MVE", "Created video-only MOV (audio extraction failed)");
          
          // Write frames to video-only MOV
          size_t frameCount = 0;
          for (const std::string& frameFile : frameFiles) {
            cv::Mat frame = cv::imread(frameFile);
            if (frame.empty()) {
              Log(WARNING, "MVE", "Failed to read frame: {}, skipping", frameFile);
              continue;
            }
            
            if (frame.cols != width || frame.rows != height) {
              cv::resize(frame, frame, cv::Size(width, height));
            }
            
            // Apply color space adjustment similar to Near Infinity
            frame.convertTo(frame, -1, 220.0/255.0, 16);
            
            writer.write(frame);
            frameCount++;
          }
          
          writer.release();
          
          if (fs::exists(outputPath)) {
            auto fileSize = fs::file_size(outputPath);
            Log(MESSAGE, "MVE", "Successfully created video-only MOV: {} ({} frames, {} bytes)", 
                outputPath, frameCount, fileSize);
            return true;
          }
        }
        
        return false;
      }
      
      // Write all frames to the GStreamer pipeline with proper timing
      // Apply color space adjustment similar to Near Infinity (convert 0-255 to 16-235 range)
      size_t frameCount = 0;
      double frameDuration = 1.0 / fps; // Duration of each frame in seconds
      
      for (const std::string& frameFile : frameFiles) {
        cv::Mat frame = cv::imread(frameFile);
        if (frame.empty()) {
          Log(WARNING, "MVE", "Failed to read frame: {}, skipping", frameFile);
          continue;
        }
        
        // Ensure frame has correct dimensions
        if (frame.cols != width || frame.rows != height) {
          cv::resize(frame, frame, cv::Size(width, height));
        }
        
        // Apply color space adjustment similar to Near Infinity
        // Convert from 0-255 range to 16-235 range (ITU-R BT.601)
        frame.convertTo(frame, -1, 220.0/255.0, 16);
        
        writer.write(frame);
        frameCount++;
        
        if (frameCount % 100 == 0) {
          Log(DEBUG, "MVE", "Processed {} frames", frameCount);
        }
      }
      
      writer.release();
      
      // Verify the output file was created
      if (fs::exists(outputPath)) {
        auto fileSize = fs::file_size(outputPath);
        Log(MESSAGE, "MVE", "Successfully created MOV file with audio: {} ({} frames, {} bytes)", 
            outputPath, frameCount, fileSize);
        
        // Keep temporary audio file for debugging (comment out cleanup)
        if (fs::exists(tempAudioFile)) {
          // fs::remove(tempAudioFile);
          Log(DEBUG, "MVE", "Keeping temporary audio file for debugging: {}", tempAudioFile);
        }
        
        return true;
      } else {
        Log(ERROR, "MVE", "MOV file was not created successfully");
        return false;
      }
    } else {
      Log(ERROR, "MVE", "No audio found in MVE file - MOV assembly requires audio");
      return false;
    }
    
  } catch (const std::exception& e) {
    Log(ERROR, "MVE", "Exception during MOV assembly: {}", e.what());
    return false;
  }
}

bool MVE::extractAudioToWAV(const std::string &outputPath) {
  Log(MESSAGE, "MVE", "Starting audio extraction to: {}", outputPath);
  
  if (!isValid()) {
    Log(ERROR, "MVE", "Invalid MVE resource for audio extraction");
    return false;
  }

  const uint8_t* p = originalFileData.data();
  size_t rem = originalFileData.size();

  // Skip MVE header
  if (rem < 26 || std::memcmp(p, "Interplay MVE File", 18) != 0) {
    Log(ERROR, "MVE", "Invalid MVE header for audio extraction");
    return false;
  }
  p += 26; rem -= 26;

  // Audio format information
  bool audioInitialized = false;
  bool audioCompressed = false;
  int channels = 1;
  int sampleRate = 22050; // Default MVE sample rate
  int bitsPerSample = 16;
  int frameSize = 0;

  // Audio data collection - use map to avoid duplicates
  std::map<int, std::vector<uint8_t>> audioBlocksMap;

  // Delta table for compressed audio (from Near Infinity)
  static const int16_t DELTA[256] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 47, 51, 56, 61,
    66, 72, 79, 86, 94, 102, 112, 122, 133, 145, 158, 173, 189, 206, 225, 245,
    267, 292, 318, 348, 379, 414, 452, 493, 538, 587, 640, 699, 763, 832, 908, 991,
    1081, 1180, 1288, 1405, 1534, 1673, 1826, 1993, 2175, 2373, 2590, 2826, 3084, 3365, 3672, 4008,
    4373, 4772, 5208, 5683, 6202, 6767, 7385, 8059, 8794, 9597, 10472, 11428, 12471, 13609, 14851, 16206,
    17685, 19298, 21060, 22981, 25078, 27367, 29864, 32589, -29973, -26728, -23186, -19322, -15105, -10503, -5481, -1,
    1, 1, 5481, 10503, 15105, 19322, 23186, 26728, 29973, -32589, -29864, -27367, -25078, -22981, -21060, -19298,
    -17685, -16206, -14851, -13609, -12471, -11428, -10472, -9597, -8794, -8059, -7385, -6767, -6202, -5683, -5208, -4772,
    -4373, -4008, -3672, -3365, -3084, -2826, -2590, -2373, -2175, -1993, -1826, -1673, -1534, -1405, -1288, -1180,
    -1081, -991, -908, -832, -763, -699, -640, -587, -538, -493, -452, -414, -379, -348, -318, -292,
    -267, -245, -225, -206, -189, -173, -158, -145, -133, -122, -112, -102, -94, -86, -79, -72,
    -66, -61, -56, -51, -47, -43, -42, -41, -40, -39, -38, -37, -36, -35, -34, -33,
    -32, -31, -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17,
    -16, -15, -14, -13, -12, -11, -10, -9, -8, -7, -6, -5, -4, -3, -2, -1
  };

  // Parse MVE chunks
  size_t chunkIndex = 0;
  while (rem >= 4) {
    uint16_t chunkSize = 0, chunkType = 0;
    if (!read_u16(p, rem, chunkSize) || !read_u16(p, rem, chunkType)) break;
    if (rem < chunkSize) break;
    
    const uint8_t* cptr = p;
    size_t crem = chunkSize;
    p += chunkSize; rem -= chunkSize;

    // Process opcodes in this chunk
    bool endOfChunk = false;
    while (!endOfChunk && crem >= 4) {
      uint16_t segLen = 0; uint8_t segType = 0, segVer = 0;
      if (!read_u16(cptr, crem, segLen)) break;
      if (crem < 2) break;
      segType = *cptr++; crem -= 1;
      segVer = *cptr++; crem -= 1;
      
      size_t payloadLen = static_cast<size_t>(segLen);
      if (crem < payloadLen) break;
      const uint8_t* payload = cptr;
      cptr += payloadLen;
      crem -= payloadLen;

              Log(DEBUG, "MVE", "Processing opcode: {} (version {})", segType, segVer);
              
              switch (segType) {
                case MVE_OC_END_OF_STREAM:
                case MVE_OC_END_OF_CHUNK:
                  endOfChunk = true;
                  break;
                  
                case MVE_OC_AUDIO_BUFFERS: {
                  // Initialize audio format - handle both version 0 and 1 like Near Infinity
                  if (payloadLen >= 8) {
                    uint16_t dummy = 0, flags = 0, rate = 0;
                    const uint8_t* ap = payload;
                    size_t arem = payloadLen;

                    read_u16(ap, arem, dummy); // Skip bogus data
                    read_u16(ap, arem, flags);
                    read_u16(ap, arem, rate);

                    channels = ((flags & MVE_AUDIO_STEREO) != 0) ? 2 : 1;
                    bitsPerSample = ((flags & MVE_AUDIO_16BIT) != 0) ? 16 : 8;
                    sampleRate = rate;
                    
                    // Handle compression flag based on version
                    if (segVer == 0) {
                      audioCompressed = false; // Version 0 doesn't support compression
                    } else if (segVer == 1) {
                      audioCompressed = ((flags & MVE_AUDIO_COMPRESSED) != 0);
                    } else {
                      Log(WARNING, "MVE", "Unsupported audio buffer version: {}", segVer);
                      audioCompressed = false;
                    }
                    
                    frameSize = channels * (bitsPerSample / 8);

                    audioInitialized = true;
                    Log(DEBUG, "MVE", "Audio init: {}Hz, {}bit, {}ch, compressed={}, version={}",
                        sampleRate, bitsPerSample, channels, audioCompressed, segVer);
                  }
                  break;
                }
        
                case MVE_OC_AUDIO_DATA: {
                  Log(DEBUG, "MVE", "Processing AUDIO_DATA: audioInitialized={}, payloadLen={}", audioInitialized, payloadLen);
                  if (!audioInitialized) break;

                  if (payloadLen >= 6) {
                    uint16_t index = 0, mask = 0, len = 0;
                    const uint8_t* ap = payload;
                    size_t arem = payloadLen;

                    read_u16(ap, arem, index);
                    read_u16(ap, arem, mask);
                    read_u16(ap, arem, len);

                    Log(DEBUG, "MVE", "AUDIO_DATA: index={}, mask={}, len={}, arem={}", index, mask, len, arem);

                    Log(DEBUG, "MVE", "AUDIO_DATA: checking len={} vs arem={}", len, arem);
                    if (len > 0 && arem > 0) {
                      std::vector<uint8_t> audioBlock;

                      if (audioCompressed && bitsPerSample == 16) {
                        // Decode compressed audio - follow Near Infinity approach exactly
                        audioBlock.resize(len);
                        int16_t predictor[2] = {0, 0};
                        int channelMask = channels - 1;
                        size_t outOfs = 0;

                        // Initialize start values (little-endian)
                        for (int j = 0; j < channels; j++) {
                          if (arem >= 2) {
                            predictor[j] = static_cast<int16_t>(ap[0] | (ap[1] << 8));
                            Log(DEBUG, "MVE", "Audio block {}: initial predictor[{}] = {}", index, j, predictor[j]);
                            // Store as little-endian in output
                            audioBlock[outOfs++] = static_cast<uint8_t>(predictor[j] & 0xFF);
                            audioBlock[outOfs++] = static_cast<uint8_t>(predictor[j] >> 8);
                            ap += 2; arem -= 2;
                          }
                        }

                        // Decode deltas
                        int channel = 0;
                        int deltaCount = 0;
                        while (outOfs < len && arem > 0) {
                          uint8_t deltaIndex = *ap++;
                          arem--;

                          predictor[channel] += DELTA[deltaIndex];
                          // Store as little-endian in output
                          audioBlock[outOfs++] = static_cast<uint8_t>(predictor[channel] & 0xFF);
                          audioBlock[outOfs++] = static_cast<uint8_t>(predictor[channel] >> 8);
                          channel = (channel + 1) & channelMask;
                          deltaCount++;
                        }
                        Log(DEBUG, "MVE", "Audio block {}: decoded {} deltas, final predictor[0]={}, predictor[1]={}", 
                            index, deltaCount, predictor[0], predictor[1]);
                      } else {
                        // Uncompressed audio - copy data starting from offset 6 (after index, mask, len)
                        audioBlock.resize(len);
                        if (arem >= len) {
                          std::memcpy(audioBlock.data(), ap, len);
                        }
                      }

                      // Only add if we haven't seen this index before
                      if (audioBlocksMap.find(index) == audioBlocksMap.end()) {
                        audioBlocksMap[index] = audioBlock;
                        Log(DEBUG, "MVE", "Added audio block {} with {} bytes", index, audioBlock.size());
                      } else {
                        Log(DEBUG, "MVE", "Skipping duplicate audio block {}", index);
                      }
                    }
                  }
                  break;
                }
        
        case MVE_OC_AUDIO_SILENCE: {
          if (!audioInitialized) break;
          
          if (payloadLen >= 6) {
            uint16_t index = 0, mask = 0, len = 0;
            const uint8_t* ap = payload;
            size_t arem = payloadLen;
            
            read_u16(ap, arem, index);
            read_u16(ap, arem, mask);
            read_u16(ap, arem, len);
            
                    if (len > 0) {
                      std::vector<uint8_t> silenceBlock(len, 0);
                      // Only add if we haven't seen this index before
                      if (audioBlocksMap.find(index) == audioBlocksMap.end()) {
                        audioBlocksMap[index] = silenceBlock;
                        Log(DEBUG, "MVE", "Added silence block {} with {} bytes", index, silenceBlock.size());
                      } else {
                        Log(DEBUG, "MVE", "Skipping duplicate silence block {}", index);
                      }
                    }
          }
          break;
        }
        
        default:
          // Ignore other opcodes
          break;
      }
    }
    
    if (chunkType == MVE_CHUNK_SHUTDOWN || chunkType == MVE_CHUNK_END) {
      break;
    }
  }

  if (audioBlocksMap.empty()) {
    Log(WARNING, "MVE", "No audio data found in MVE file");
    return false;
  }

  Log(DEBUG, "MVE", "Found {} unique audio blocks", audioBlocksMap.size());

  // Write WAV file
  std::ofstream wavFile(outputPath, std::ios::binary);
  if (!wavFile.is_open()) {
    Log(ERROR, "MVE", "Failed to open WAV file for writing: {}", outputPath);
    return false;
  }

  // Calculate total audio data size
  size_t totalAudioSize = 0;
  for (const auto& [index, block] : audioBlocksMap) {
    totalAudioSize += block.size();
  }

  // WAV header
  uint32_t fileSize = 36 + static_cast<uint32_t>(totalAudioSize);
  uint32_t dataSize = static_cast<uint32_t>(totalAudioSize);
  
  // RIFF header
  wavFile.write("RIFF", 4);
  wavFile.write(reinterpret_cast<const char*>(&fileSize), 4);
  wavFile.write("WAVE", 4);
  
  // fmt chunk
  wavFile.write("fmt ", 4);
  uint32_t fmtSize = 16;
  wavFile.write(reinterpret_cast<const char*>(&fmtSize), 4);
  uint16_t audioFormat = 1; // PCM
  wavFile.write(reinterpret_cast<const char*>(&audioFormat), 2);
  wavFile.write(reinterpret_cast<const char*>(&channels), 2);
  wavFile.write(reinterpret_cast<const char*>(&sampleRate), 4);
  uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
  wavFile.write(reinterpret_cast<const char*>(&byteRate), 4);
  uint16_t blockAlign = channels * (bitsPerSample / 8);
  wavFile.write(reinterpret_cast<const char*>(&blockAlign), 2);
  wavFile.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
  
  // data chunk
  wavFile.write("data", 4);
  wavFile.write(reinterpret_cast<const char*>(&dataSize), 4);
  
  // Write audio data in index order
  for (const auto& [index, block] : audioBlocksMap) {
    wavFile.write(reinterpret_cast<const char*>(block.data()), block.size());
  }
  
  wavFile.close();
  
  Log(MESSAGE, "MVE", "Successfully extracted audio: {} ({} blocks, {} bytes, {}Hz, {}bit, {}ch)", 
      outputPath, audioBlocksMap.size(), totalAudioSize, sampleRate, bitsPerSample, channels);
  return true;
}

bool MVE::assemble() {
  Log(MESSAGE, "MVE", "Starting MVE assembly for resource: {}", resourceName_);
  
  if (!isValid()) {
    Log(ERROR, "MVE", "Invalid MVE resource for assembly");
    return false;
  }
  
  // Check if upscaled frames exist
  std::string upscaledDir = getUpscaledDir();
  if (!fs::exists(upscaledDir)) {
    Log(ERROR, "MVE", "Upscaled directory does not exist: {}", upscaledDir);
    return false;
  }
  
  // Create assemble directory
  std::string assembleDir = getAssembleDir(true);
  if (assembleDir.empty()) {
    Log(ERROR, "MVE", "Failed to create assemble directory");
    return false;
  }
  
  // Determine output path
  std::string baseName = extractBaseName();
  std::string outputPath = assembleDir + "/" + baseName + ".mov";
  
  // Use default MVE frame rate (typically 15 fps for MVE files)
  double fps = 15.0;
  
  // Try to determine frame rate from MVE file if possible
  // For now, use a reasonable default
  Log(DEBUG, "MVE", "Using default frame rate: {} fps", fps);
  
  // Assemble MOV file with audio
  bool success = assembleMOVFile(outputPath, fps);
  
  if (success) {
    Log(MESSAGE, "MVE", "Successfully assembled MVE to MOV: {}", outputPath);
    return true;
  } else {
    Log(ERROR, "MVE", "Failed to assemble MVE to MOV");
    return false;
  }
}

bool MVE::extractAll() { return PluginManager::getInstance().extractAllResourcesOfType(IE_MVE_CLASS_ID); }
bool MVE::upscaleAll() { return PluginManager::getInstance().upscaleAllResourcesOfType(IE_MVE_CLASS_ID); }
bool MVE::assembleAll() { return PluginManager::getInstance().assembleAllResourcesOfType(IE_MVE_CLASS_ID); }

bool MVE::cleanDirectory(const std::string& dir) {
  try {
    if (fs::exists(dir)) fs::remove_all(dir);
    return true;
  } catch (const std::exception& e) {
    Log(ERROR, "MVE", "Failed to clean directory {}: {}", dir, e.what());
    return false;
  }
}

bool MVE::cleanExtractDirectory() { return cleanDirectory(getExtractDir(false)); }
bool MVE::cleanUpscaleDirectory() { return cleanDirectory(getUpscaledDir(false)); }
bool MVE::cleanAssembleDirectory() { return cleanDirectory(getAssembleDir(false)); }

std::string MVE::getOutputDir(bool ensureDir) const { 
    return constructPath("-mve", ensureDir);
}

std::string MVE::getExtractDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-mve-extracted";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string MVE::getUpscaledDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-mve-upscaled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

std::string MVE::getAssembleDir(bool ensureDir) const {
    std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-mve-assembled";
    if (ensureDir) ensureDirectoryExists(path);
    return path;
}

void MVE::registerCommands(CommandTable& commandTable) {
    commandTable["mve"] = {
        "MVE file operations",
        {
            {"extract", {
                "Extract MVE resource to file (e.g., mve extract ar0100)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: mve extract <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().extractResource(args[0], IE_MVE_CLASS_ID) ? 0 : 1;
                }
            }},
            {"upscale", {
                "Upscale MVE coordinates (e.g., mve upscale intro)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: mve upscale <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().upscaleResource(args[0], IE_MVE_CLASS_ID) ? 0 : 1;
                }
            }},
            {"assemble", {
                "Assemble MVE file (e.g., mve assemble intro)",
                [](const std::vector<std::string>& args) -> int {
                    if (args.empty()) {
                        std::cerr << "Usage: mve assemble <resource_name>" << std::endl;
                        return 1;
                    }
                    return PluginManager::getInstance().assembleResource(args[0], IE_MVE_CLASS_ID) ? 0 : 1;
                }
            }}
        }
    };
}

REGISTER_PLUGIN(MVE, IE_MVE_CLASS_ID);

} // namespace ProjectIE4k