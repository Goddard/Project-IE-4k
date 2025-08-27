#ifndef UPSCALER_H
#define UPSCALER_H

#include <ncnn/net.h>
#include <opencv2/opencv.hpp>
#include <vector>

namespace ProjectIE4k {

/**
 * @brief Pure image processing class that represents an image and provides
 * operations on it
 *
 * This class is designed to be a simple representation of an image with
 * processing capabilities. All orchestration, tiling decisions, and model
 * management is handled by UpscalerService.
 */
class Upscaler {
private:
  cv::Mat imageData_;
  cv::Mat tiledImageData_;
  cv::Mat weightMap_;
  ncnn::Net *model_;
  int overlap_;
  int tileSize_;
  bool needsTiling_;
  int outputWidth_;
  int outputHeight_;

public:
  /**
   * @brief Construct Upscaler with image data and model
   * @param imageData The image to operate on
   * @param model Pre-loaded NCNN model
   */
  Upscaler(const cv::Mat &imageData, ncnn::Net *model, int tileSize);

  ~Upscaler();

  /**
   * @brief Get the current image data
   * @return Reference to the image data
   */
  const cv::Mat &getImageData() const;

  /**
   * @brief Upscale the image or a specific tile region
   * @param tileRegion Optional tile region to extract and upscale. If empty,
   * upscales the entire image
   * @return Upscaled image or tile as cv::Mat
   */
  cv::Mat upscale();

  /**
   * @brief Extract a tile region from the image
   * @param tileRegion The region to extract
   * @return Extracted tile as cv::Mat
   */
  cv::Mat extractTile(const cv::Rect &tileRegion) const;

  /**
   * @brief Generate tile regions for the current image
   * @param tileSize Size of each tile
   * @return Vector of tile regions
   */
  std::vector<cv::Rect> generateTileRegions() const;

  bool stichTile(const cv::Rect &region);
  bool upscaleTile(cv::Mat &inputData);

  /**
   * @brief Blend a tile into a result image with proper weight handling
   * @param result The result image to blend into
   * @param weightMap The weight map for blending
   * @param tile The tile to blend
   * @param outputRegion The region in the output where this tile goes
   */
  void blendTileIntoResult(cv::Mat &result, cv::Mat &weightMap,
                           const cv::Mat &tile, const cv::Rect &outputRegion,
                           int regionW, int regionH) const;

  /**
   * @brief Normalize an image by its weight map
   * @param result The image to normalize
   * @param weightMap The weight map to normalize by
   */
  static void normalizeByWeights(cv::Mat &result, const cv::Mat &weightMap);

  /**
   * @brief Get the dimensions of the current image
   * @return Size of the image (width, height)
   */
  cv::Size getImageSize() const;

  /**
   * @brief Check if the upscaler has valid image data
   * @return true if image data is present and valid
   */
  bool hasImageData() const;

  bool needsTiling() const;

private:
  /**
   * @brief Convert OpenCV Mat to NCNN Mat format
   * @param tile Input OpenCV image
   * @return NCNN-formatted image
   */
  ncnn::Mat prepareTileForNcnn(const cv::Mat &tile) const;

  /**
   * @brief Convert NCNN Mat back to OpenCV Mat format
   * @param result NCNN output
   * @return OpenCV-formatted image
   */
  cv::Mat ncnnToOpenCV(const ncnn::Mat &result) const;

  /**
   * @brief Create a blend mask for seamless tile stitching
   * @param width Width of the tile
   * @param height Height of the tile
   * @return Blend mask as cv::Mat
   */
  cv::Mat createBlendMask(int width, int height) const;
};

} // namespace ProjectIE4k

#endif // UPSCALER_H
