#include "Upscaler.h"

#include <cmath>

#include "core/CFG.h"
#include "core/Logging/Logging.h"

namespace ProjectIE4k {

namespace {
class PerExtractorVkAllocators {
public:
  explicit PerExtractorVkAllocators(int gpuIndex = 0)
      : blobVkAllocator_(ncnn::get_gpu_device(gpuIndex)),
        stagingVkAllocator_(ncnn::get_gpu_device(gpuIndex)) {}

  ncnn::VkBlobAllocator *blobVk() { return &blobVkAllocator_; }
  ncnn::VkStagingAllocator *stagingVk() { return &stagingVkAllocator_; }

  void forceCleanup() {
    blobVkAllocator_.clear();
    stagingVkAllocator_.clear();
  }
  void waitForCleanup() const {}

private:
  ncnn::VkBlobAllocator blobVkAllocator_;
  ncnn::VkStagingAllocator stagingVkAllocator_;
};
} // namespace

Upscaler::Upscaler(const cv::Mat &imageData, ncnn::Net *model, int tileSize)
    : imageData_(imageData.clone()), model_(model), tileSize_(tileSize),
      overlap_(32) {
  Log(DEBUG, "Upscaler",
      "Upscaler created with image data: {}x{}, scale: {}x, tileSize: {}, "
      "overlap: {}",
      imageData.cols, imageData.rows, PIE4K_CFG.UpScaleFactor, tileSize,
      overlap_);

  needsTiling_ = (imageData_.cols > tileSize_) || (imageData_.rows > tileSize_);

  outputWidth_ = imageData_.cols * PIE4K_CFG.UpScaleFactor;
  outputHeight_ = imageData_.rows * PIE4K_CFG.UpScaleFactor;

  if (needsTiling_) {
    tiledImageData_ = cv::Mat::zeros(outputHeight_, outputWidth_, CV_32FC3);
    weightMap_ = cv::Mat::zeros(outputHeight_, outputWidth_, CV_32F);
  }

  if (!hasImageData()) {
    Log(ERROR, "Upscaler", "No image data to upscale");
    // throw error
  }

  if (!model_) {
    Log(ERROR, "Upscaler", "No model available for upscaling");
    // throw error
  }
}

Upscaler::~Upscaler() { Log(DEBUG, "Upscaler", "Upscaler destroyed"); }

bool Upscaler::needsTiling() const { return needsTiling_; }

const cv::Mat &Upscaler::getImageData() const { return imageData_; }

cv::Size Upscaler::getImageSize() const {
  if (imageData_.empty()) {
    return cv::Size(0, 0);
  }
  return cv::Size(imageData_.cols, imageData_.rows);
}

bool Upscaler::hasImageData() const { return !imageData_.empty(); }

cv::Mat Upscaler::upscale() {
  // Extract alpha channel if present (BGRA input)
  cv::Mat alphaChannel;
  bool hasAlpha = (imageData_.channels() == 4);
  if (hasAlpha) {
    std::vector<cv::Mat> channels;
    cv::split(imageData_, channels);
    alphaChannel = channels[3].clone(); // BGRA -> channels[3] is alpha
    Log(DEBUG, "Upscaler", "Preserving alpha channel for transparency");
  }

  if (needsTiling_) {
    // Generate tile regions
    std::vector<cv::Rect> tileRegions = generateTileRegions();
    Log(DEBUG, "UpscalerService", "Generated {} tile regions for processing",
        tileRegions.size());

    for (size_t i = 0; i < tileRegions.size(); ++i) {
      const auto &region = tileRegions[i];
      Log(DEBUG, "UpscalerService", "Processing tile {}/{}: ({},{}) {}x{}",
          i + 1, tileRegions.size(), region.x, region.y, region.width,
          region.height);

      // Upscale and stitch the tile region directly
      if (!stichTile(region)) {
        Log(ERROR, "UpscalerService",
            "Failed to process tile {}/{} at ({},{}) {}x{}", i + 1,
            tileRegions.size(), region.x, region.y, region.width,
            region.height);
      }

      Log(DEBUG, "UpscalerService", "Successfully processed tile {}/{}", i + 1,
          tileRegions.size());
    }

    Log(DEBUG, "UpscalerService", "All {} tiles processed, finalizing result",
        tileRegions.size());

    normalizeByWeights(tiledImageData_, weightMap_);

    // Convert final result: already float [0,255], just to 8-bit
    cv::Mat finalResult;
    tiledImageData_.convertTo(finalResult, CV_8UC3);
    tiledImageData_.release();
    weightMap_.release();

    // If we had alpha, upscale it and recombine
    if (hasAlpha && !alphaChannel.empty()) {
      // Upscale alpha channel using nearest-neighbor to preserve sharp edges
      cv::Mat upscaledAlpha;
      cv::resize(alphaChannel, upscaledAlpha, finalResult.size(), 0, 0,
                 cv::INTER_NEAREST);

      // Ensure alpha is 8UC1 and same size as result
      if (upscaledAlpha.type() != CV_8UC1) {
        upscaledAlpha.convertTo(upscaledAlpha, CV_8UC1);
      }

      // Combine BGR + alpha to create BGRA
      std::vector<cv::Mat> bgrChannels;
      cv::split(finalResult, bgrChannels);
      bgrChannels.push_back(upscaledAlpha);
      cv::merge(bgrChannels, finalResult);

      Log(DEBUG, "Upscaler",
          "Recombined upscaled alpha channel for tiled result");
    }

    Log(DEBUG, "Upscaler", "Tiled upscale completed: {}x{}", finalResult.cols,
        finalResult.rows);

    return finalResult;
  }

  // Convert to NCNN format
  ncnn::Mat ncnnInput = prepareTileForNcnn(imageData_);

  // Process with NCNN
  ncnn::PoolAllocator blob_allocator;
  ncnn::PoolAllocator workspace_allocator;
  ncnn::Extractor extractor = model_->create_extractor();
  extractor.set_blob_allocator(&blob_allocator);
  extractor.set_workspace_allocator(&workspace_allocator);
  PerExtractorVkAllocators vkAllocsDirect(0);
  extractor.set_blob_vkallocator(vkAllocsDirect.blobVk());
  extractor.set_staging_vkallocator(vkAllocsDirect.stagingVk());
  ncnn::Mat ncnnOutput;

  int ret = extractor.input("data", ncnnInput);
  if (ret != 0) {
    Log(ERROR, "Upscaler", "Failed to set NCNN input");
    return cv::Mat();
  }

  ret = extractor.extract("output", ncnnOutput);
  if (ret != 0) {
    Log(ERROR, "Upscaler", "Failed to extract NCNN output");
    return cv::Mat();
  }

  cv::Mat result = ncnnToOpenCV(ncnnOutput);

  // If we had alpha, upscale it and recombine
  if (hasAlpha && !alphaChannel.empty()) {
    // Upscale alpha channel using nearest-neighbor to preserve sharp edges
    cv::Mat upscaledAlpha;
    cv::resize(alphaChannel, upscaledAlpha, result.size(), 0, 0,
               cv::INTER_NEAREST);

    // Ensure alpha is 8UC1 and same size as result
    if (upscaledAlpha.type() != CV_8UC1) {
      upscaledAlpha.convertTo(upscaledAlpha, CV_8UC1);
    }

    // Combine BGR + alpha to create BGRA
    std::vector<cv::Mat> bgrChannels;
    cv::split(result, bgrChannels);
    bgrChannels.push_back(upscaledAlpha);
    cv::merge(bgrChannels, result);

    Log(DEBUG, "Upscaler",
        "Recombined upscaled alpha channel for direct result");
  }

  Log(DEBUG, "Upscaler", "Direct upscale completed: {}x{} -> {}x{}",
      imageData_.cols, imageData_.rows, result.cols, result.rows);

  extractor.clear();
  ncnnOutput.release();
  ncnnInput.release(); // Release the input NCNN Mat
  return result;
}

cv::Mat Upscaler::extractTile(const cv::Rect &tileRegion) const {
  if (!hasImageData()) {
    Log(ERROR, "Upscaler", "No image data to extract from");
    return cv::Mat();
  }

  cv::Mat tile = imageData_(tileRegion);
  Log(DEBUG, "Upscaler", "Extracted tile: {}x{} at ({},{})", tile.cols,
      tile.rows, tileRegion.x, tileRegion.y);

  return tile;
}

std::vector<cv::Rect> Upscaler::generateTileRegions() const {
  std::vector<cv::Rect> regions;

  if (!hasImageData()) {
    Log(ERROR, "Upscaler", "No image data to generate tiles for");
    return regions;
  }

  int imageWidth = imageData_.cols;
  int imageHeight = imageData_.rows;
  int step = tileSize_ - overlap_;

  Log(DEBUG, "Upscaler",
      "Generating tiles for {}x{} image with tile size {} and overlap {}",
      imageWidth, imageHeight, tileSize_, overlap_);

  for (int y = 0; y < imageHeight; y += step) {
    for (int x = 0; x < imageWidth; x += step) {
      int width = std::min(tileSize_, imageWidth - x);
      int height = std::min(tileSize_, imageHeight - y);

      regions.emplace_back(x, y, width, height);
    }
  }

  Log(DEBUG, "Upscaler", "Generated {} tile regions", regions.size());
  return regions;
}

bool Upscaler::upscaleTile(cv::Mat &inputData) {
  Log(DEBUG, "Upscaler", "upscaleTile input: {}x{} channels:{}", inputData.cols,
      inputData.rows, inputData.channels());

  ncnn::PoolAllocator blob_allocator;
  ncnn::PoolAllocator workspace_allocator;
  PerExtractorVkAllocators vkAllocsTile(0);

  // Convert to NCNN format
  ncnn::Mat ncnnInput = prepareTileForNcnn(inputData);
  Log(DEBUG, "Upscaler", "NCNN input: {}x{}x{}", ncnnInput.w, ncnnInput.h,
      ncnnInput.c);

  // Process with NCNN

  ncnn::Extractor extractor = model_->create_extractor();
  extractor.set_blob_allocator(&blob_allocator);
  extractor.set_workspace_allocator(&workspace_allocator);
  extractor.set_blob_vkallocator(vkAllocsTile.blobVk());
  extractor.set_staging_vkallocator(vkAllocsTile.stagingVk());

  ncnn::Mat ncnnOutput;

  int ret = extractor.input("data", ncnnInput);
  if (ret != 0) {
    Log(ERROR, "Upscaler", "Failed to set NCNN input");
    return false;
  }

  ret = extractor.extract("output", ncnnOutput);
  if (ret != 0) {
    Log(ERROR, "Upscaler", "Failed to extract NCNN output");
    return false;
  }

  Log(DEBUG, "Upscaler", "NCNN output: {}x{}x{}", ncnnOutput.w, ncnnOutput.h,
      ncnnOutput.c);

  // Convert back to OpenCV format
  inputData = ncnnToOpenCV(ncnnOutput);

  extractor.clear();
  ncnnOutput.release();
  ncnnInput.release(); // Release the input NCNN Mat
  return true;
}

bool Upscaler::stichTile(const cv::Rect &region) {
  cv::Mat tile = extractTile(region);

  if (!upscaleTile(tile)) {
    return false;
  }

  // Scale the region for output coordinates
  const int s = PIE4K_CFG.UpScaleFactor;
  cv::Rect outputRegion(region.x * s, region.y * s, region.width * s,
                        region.height * s);

  // Blend this tile into the existing cv mat holding tile data
  // Pass original region dimensions for correct mask creation
  blendTileIntoResult(tiledImageData_, weightMap_, tile, outputRegion,
                      region.width, region.height);

  Log(DEBUG, "Upscaler",
      "Successfully stitched tile at ({},{}) {}x{} into final result", region.x,
      region.y, region.width, region.height);

  return true;
}

void Upscaler::blendTileIntoResult(cv::Mat &result, cv::Mat &weightMap,
                                   const cv::Mat &tile,
                                   const cv::Rect &outputRegion, int regionW,
                                   int regionH) const {
  // Use the passed original tile dimensions (before upscaling)
  int inputTileW = regionW;
  int inputTileH = regionH;

  // Create blend mask for INPUT tile size, then resize to output size
  cv::Mat mask = createBlendMask(inputTileW, inputTileH);
  // For first tile at origin, test with full weight mask to isolate artifacts
  if (outputRegion.x == 0 && outputRegion.y == 0) {
    mask.setTo(1.0f);
  }

  cv::resize(mask, mask, cv::Size(tile.cols, tile.rows), 0, 0,
             cv::INTER_LINEAR);
  // After resize, force interior to exactly 1.0
  int ovOut = std::min(overlap_ * PIE4K_CFG.UpScaleFactor,
                       std::min(mask.cols, mask.rows) / 2);
  if (ovOut > 0) {
    cv::Rect interiorOut(ovOut, ovOut, std::max(0, mask.cols - 2 * ovOut),
                         std::max(0, mask.rows - 2 * ovOut));
    if (interiorOut.width > 0 && interiorOut.height > 0) {
      mask(interiorOut).setTo(1.0f);
    }
  }

  // Convert tile to float and apply mask
  cv::Mat tileFloat, maskFloat;
  // If tile is already 8-bit, convert and clamp range to 0 to 255 to prevent negative/overflow artifacts
  if (tile.type() == CV_8UC3) {
    tile.convertTo(tileFloat, CV_32FC3);
  } else if (tile.type() == CV_32FC3) {
    tileFloat = tile;
    cv::min(tileFloat, 255.0f, tileFloat);
    cv::max(tileFloat, 0.0f, tileFloat);
  } else {
    tile.convertTo(tileFloat, CV_32FC3);
  }
  mask.convertTo(maskFloat, CV_32F);

  // Get ROIs for blending Clamp outputRegion to image bounds to avoid partial writes near edges
  cv::Rect bounded = outputRegion & cv::Rect(0, 0, result.cols, result.rows);
  if (bounded.width != outputRegion.width ||
      bounded.height != outputRegion.height) {
    Log(WARNING, "Upscaler",
        "Output region clipped from {}x{}@({}, {}) to {}x{}@({}, {})",
        outputRegion.width, outputRegion.height, outputRegion.x, outputRegion.y,
        bounded.width, bounded.height, bounded.x, bounded.y);
  }
  cv::Mat resultROI = result(bounded);
  cv::Mat weightROI = weightMap(bounded);
  // If clipped, also crop tile/mask to match ROI size
  cv::Mat tileCropped = tile(cv::Rect(0, 0, resultROI.cols, resultROI.rows));
  cv::Mat maskFloatCropped =
      maskFloat(cv::Rect(0, 0, resultROI.cols, resultROI.rows));

  // Inspect weight distribution inside the interior of the ROI BEFORE accumulation
  int ovOutInspect = std::min(overlap_ * PIE4K_CFG.UpScaleFactor,
                              std::min(resultROI.cols, resultROI.rows) / 2);
  if (ovOutInspect > 0 && resultROI.cols > 2 * ovOutInspect &&
      resultROI.rows > 2 * ovOutInspect) {
    cv::Rect innerROI(ovOutInspect, ovOutInspect,
                      resultROI.cols - 2 * ovOutInspect,
                      resultROI.rows - 2 * ovOutInspect);
    double wmin, wmax;
    cv::minMaxLoc(weightROI(innerROI), &wmin, &wmax);
    Log(DEBUG, "Upscaler",
        "WeightROI inner (excluding overlap) min/max: {:.6f}/{:.6f} at ROI "
        "({}, {}) {}x{}",
        wmin, wmax, bounded.x + innerROI.x, bounded.y + innerROI.y,
        innerROI.width, innerROI.height);
  } else {
    Log(DEBUG, "Upscaler",
        "WeightROI inner inspection skipped (ROI too small for overlap "
        "exclusion)");
  }
  // Accumulate now
  {
    std::vector<cv::Mat> maskChannels2 = {maskFloatCropped, maskFloatCropped,
                                          maskFloatCropped};
    cv::Mat maskFloat3_2;
    cv::merge(maskChannels2, maskFloat3_2);
    cv::Mat tileFloatCropped2 =
        tileFloat(cv::Rect(0, 0, resultROI.cols, resultROI.rows));
    cv::Mat maskedTile2 = tileFloatCropped2.mul(maskFloat3_2);
    resultROI += maskedTile2;
    weightROI += maskFloatCropped;
  }
  // Inspect AFTER accumulation so we know weights are applied
  int ovOutInspect2 = std::min(overlap_ * PIE4K_CFG.UpScaleFactor,
                               std::min(resultROI.cols, resultROI.rows) / 2);
  if (ovOutInspect2 > 0 && resultROI.cols > 2 * ovOutInspect2 &&
      resultROI.rows > 2 * ovOutInspect2) {
    cv::Rect innerROI2(ovOutInspect2, ovOutInspect2,
                       resultROI.cols - 2 * ovOutInspect2,
                       resultROI.rows - 2 * ovOutInspect2);
    double wmin2, wmax2;
    cv::minMaxLoc(weightROI(innerROI2), &wmin2, &wmax2);
    Log(DEBUG, "Upscaler",
        "(Post) WeightROI inner min/max: {:.6f}/{:.6f} at ROI ({}, {}) {}x{}",
        wmin2, wmax2, bounded.x + innerROI2.x, bounded.y + innerROI2.y,
        innerROI2.width, innerROI2.height);
  }
}

void Upscaler::normalizeByWeights(cv::Mat &result, const cv::Mat &weightMap) {
  Log(DEBUG, "Upscaler", "Normalizing {}x{} image by weights (vectorized)",
      result.cols, result.rows);
  // Ensure result is float
  if (result.type() != CV_32FC3) {
    result.convertTo(result, CV_32FC3);
  }
  // Avoid division by zero and tiny weights
  cv::Mat safeWeight;
  cv::max(weightMap, 1e-6f, safeWeight);
  // Divide per-channel
  std::vector<cv::Mat> channels;
  cv::split(result, channels);
  for (auto &ch : channels) {
    cv::divide(ch, safeWeight, ch);
  }
  cv::merge(channels, result);
}

ncnn::Mat Upscaler::prepareTileForNcnn(const cv::Mat &tile) const {
  cv::Mat rgbTile;

  // Handle both BGR(3-channel) and BGRA(4-channel) input
  if (tile.channels() == 4) {
    Log(DEBUG, "Upscaler", "Converting 4-channel BGRA tile to RGB");
    cv::cvtColor(tile, rgbTile, cv::COLOR_BGRA2RGB);
  } else if (tile.channels() == 3) {
    Log(DEBUG, "Upscaler", "Converting 3-channel BGR tile to RGB");
    cv::cvtColor(tile, rgbTile, cv::COLOR_BGR2RGB);
  } else {
    Log(ERROR, "Upscaler", "Unsupported number of channels: {}",
        tile.channels());
    return ncnn::Mat();
  }

  ncnn::Mat ncnnMat = ncnn::Mat::from_pixels(rgbTile.data, ncnn::Mat::PIXEL_RGB,
                                             rgbTile.cols, rgbTile.rows);
  const float norm_vals[3] = {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f};
  ncnnMat.substract_mean_normalize(nullptr, norm_vals);
  return ncnnMat;
}

cv::Mat Upscaler::ncnnToOpenCV(const ncnn::Mat &result) const {
  cv::Mat bgr32f(result.h, result.w, CV_32FC3);
  const float *rc = result.channel(0);
  const float *gc = result.channel(1);
  const float *bc = result.channel(2);
  for (int h = 0; h < result.h; ++h) {
    for (int w = 0; w < result.w; ++w) {
      int idx = h * result.w + w;
      cv::Vec3f &pix = bgr32f.at<cv::Vec3f>(h, w);
      pix[0] = bc[idx] * 255.0f; // B
      pix[1] = gc[idx] * 255.0f; // G
      pix[2] = rc[idx] * 255.0f; // R
    }
  }
  cv::Mat bgr8u;
  bgr32f.convertTo(bgr8u, CV_8UC3); // clamps to [0,255]
  return bgr8u;
}

cv::Mat Upscaler::createBlendMask(int width, int height) const {
  // Use the EXACT same cosine blending as the working version
  cv::Mat mask(height, width, CV_32FC1, cv::Scalar(1.0));
  int ov = std::min(overlap_, std::min(width, height) / 2);

  // Horizontal blend (cosine)
  for (int x = 0; x < width; ++x) {
    float wx = 1.0f;
    if (x < ov)
      wx = 0.5f * (1.0f - cosf(M_PI * x / ov));
    else if (x >= width - ov)
      wx = 0.5f * (1.0f - cosf(M_PI * (width - 1 - x) / ov));
    for (int y = 0; y < height; ++y) {
      mask.at<float>(y, x) *= wx;
    }
  }

  // Vertical blend (cosine)
  for (int y = 0; y < height; ++y) {
    float wy = 1.0f;
    if (y < ov)
      wy = 0.5f * (1.0f - cosf(M_PI * y / ov));
    else if (y >= height - ov)
      wy = 0.5f * (1.0f - cosf(M_PI * (height - 1 - y) / ov));
    for (int x = 0; x < width; ++x) {
      mask.at<float>(y, x) *= wy;
    }
  }

  if (ov > 0) {
    cv::Rect interior(ov, ov, std::max(0, width - 2 * ov),
                      std::max(0, height - 2 * ov));
    if (interior.width > 0 && interior.height > 0) {
      mask(interior).setTo(1.0f);
    }
  }

  return mask;
}

} // namespace ProjectIE4k