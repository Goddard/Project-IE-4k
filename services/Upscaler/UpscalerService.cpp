#include "UpscalerService.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <unistd.h>

#include <opencv2/core/mat.hpp>
#include <opencv2/imgcodecs.hpp>

#include "core/CFG.h"
#include "core/Logging/Logging.h"
#include "core/OperationsMonitor/OperationsMonitor.h"
#include "core/SClassID.h"
#include "services/Upscaler/NcnnAllocator.h"
#include <ncnn/gpu.h>

namespace fs = std::filesystem;

namespace ProjectIE4k {

UpscalerService::UpscalerService()
    : currentResourceType_(0), modelVRAMUsage_(0), totalSystemVRAM_(0),
      baselineVRAM_(0), vramMeasured_(false),
      ncnnAllocator_(std::make_unique<NcnnAllocator>()) {}

UpscalerService::~UpscalerService() {
    cleanup();
}

void UpscalerService::initializeForResourceType(SClass_ID resourceType) {
  if (currentResourceType_ == resourceType && isInitialized()) {
    Log(MESSAGE, "UpscalerService",
        "Upscaler already initialized for resource type: {}",
        SClass::getExtension(resourceType));
    return;
  }

    // Cleanup previous instance if different type
    if (isInitialized() && currentResourceType_ != resourceType) {
      Log(MESSAGE, "UpscalerService",
          "Switching from {} to {}, cleaning up previous instance",
          SClass::getExtension(currentResourceType_),
          SClass::getExtension(resourceType));
      cleanup();
    }

    currentResourceType_ = resourceType;

    // Load the NCNN model for this configuration
    if (!loadModel()) {
      Log(ERROR, "UpscalerService",
          "Failed to load model for resource type: {}",
          SClass::getExtension(resourceType));
      return;
    }

    // No need to create upscaler instance - we'll create them as needed per
    // image

    Log(MESSAGE, "UpscalerService", "UpscalerService initialized for resource type: {}", SClass::getExtension(resourceType));

    Log(MESSAGE, "UpscalerService", "Initialized upscaler for {}",
        SClass::getExtension(resourceType));
}

void UpscalerService::allocatorCleanup() {
  if (ncnnAllocator_) {
    Log(DEBUG, "UpscalerService",
        "Using NcnnAllocator for memory cleanup (before: {} bytes, {} "
        "allocations)",
        ncnnAllocator_->getTotalAllocated(),
        ncnnAllocator_->getAllocationCount());
    ncnnAllocator_->forceCleanup();
    ncnnAllocator_->waitForCleanup();
    Log(DEBUG, "UpscalerService",
        "NcnnAllocator cleanup complete (after: {} bytes, {} allocations)",
        ncnnAllocator_->getTotalAllocated(),
        ncnnAllocator_->getAllocationCount());
  }
}

void UpscalerService::cleanup() {
  Log(MESSAGE, "UpscalerService",
      "Cleaning up upscaler service for resource type: {}",
      SClass::getExtension(currentResourceType_));

  currentResourceType_ = 0;

  // Clean up models - must clear Vulkan resources before destroying
  std::lock_guard<std::mutex> lock(modelMutex_);

  // Use NcnnAllocator for proper memory cleanup
  allocatorCleanup();

  if (sharedModel_) {
    Log(DEBUG, "UpscalerService",
        "Releasing sharedModel without cleanup to avoid Vulkan issues");
    ncnn::Net *releasedModel = sharedModel_.release();
    if (releasedModel) {
      Log(DEBUG, "UpscalerService",
          "Successfully released sharedModel pointer: {}",
          static_cast<void *>(releasedModel));
    } else {
      Log(DEBUG, "UpscalerService", "sharedModel was already null");
    }
  }

  if (cpuModel_) {
    Log(DEBUG, "UpscalerService",
        "Releasing cpuModel without cleanup to avoid Vulkan issues");
    ncnn::Net *releasedModel = cpuModel_.release();
    if (releasedModel) {
      Log(DEBUG, "UpscalerService",
          "Successfully released cpuModel pointer: {}",
          static_cast<void *>(releasedModel));
    } else {
      Log(DEBUG, "UpscalerService", "cpuModel was already null");
    }
  }

  // Reset the unique_ptrs
  sharedModel_.reset();
  cpuModel_.reset();

  // Hard teardown of Vulkan/NCNN to free sticky allocations
  Log(DEBUG, "UpscalerService",
      "Destroying NCNN gpu instance for full VRAM release");
  ncnn::destroy_gpu_instance();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  currentModelPath_.clear();
}

bool UpscalerService::isInitialized() const {
  return currentResourceType_ != 0 && (sharedModel_ || cpuModel_);
}

SClass_ID UpscalerService::getCurrentResourceType() const {
    return currentResourceType_;
}

// currently estimateVRAMForTileSize lambda only tested upscaly-ultrasharp-v2
// need to add correct estimates for other models.
TileSize UpscalerService::calculateTileSize(std::string modelName,
                                            uint64_t totalVRAM,
                                            uint64_t baselineUsedVRAM,
                                            double maxVRAMPercent,
                                            int upscaleFactor) {
  // Look up model VRAM usage from the map
  auto it = modelVramUsageMap.find(modelName);
  if (it == modelVramUsageMap.end()) {
    // Fallback to default values if model not found
    Log(DEBUG, "UpscalerService",
        "Model {} not found in VRAM map, using default values", modelName);
    it =
        modelVramUsageMap.find("upscayl-ultrasharp-v2"); // Use a common default
  }

  const ModelVramInfo &info = it->second;
  uint64_t modelLoadVRAM =
      static_cast<uint64_t>(info.loadMB) * 1024 * 1024; // Convert MB to bytes
  uint64_t modelInferenceVRAM = static_cast<uint64_t>(info.inference512MB) *
                                1024 * 1024; // Convert MB to bytes

  // Calculate available VRAM budget
  uint64_t usableVRAM = static_cast<uint64_t>((totalVRAM - baselineUsedVRAM) *
                                              (maxVRAMPercent / 100.0));
  uint64_t availableForTiles = usableVRAM - modelLoadVRAM;

  // Empirical formula based on 512x512 → 2048x2048 using model-specific
  // inference VRAM Base VRAM per pixel is roughly: modelInferenceVRAM /
  // (512*512 + 2048*2048) pixels
  double baseVRAMPerPixel = static_cast<double>(modelInferenceVRAM) /
                            (1024.0 * 1024.0) /
                            (512.0 * 512.0 + 2048.0 * 2048.0);

  // Estimated VRAM usage per tile based on empirical data from testing
  // From test with reference model: 512x512 tile uses model-specific inference
  // VRAM VRAM usage scales roughly with tile area and upscale factor squared
  auto estimateVRAMForTileSize = [upscaleFactor,
                                  baseVRAMPerPixel](int tileSize) -> uint64_t {
    // Base calculation: VRAM scales with input tile area and output area
    double inputPixels = static_cast<double>(tileSize * tileSize);
    double outputPixels = inputPixels * upscaleFactor * upscaleFactor;

    // Add overhead for intermediate buffers, model weights access, etc.
    // double overhead = 200.0; // Base overhead in MB

    uint64_t estimatedVRAM = static_cast<uint64_t>(
        (inputPixels + outputPixels) * baseVRAMPerPixel); // + overhead
    return estimatedVRAM * 1024 * 1024;                   // Convert to bytes
  };

  // Calculate optimal tile size dynamically based on available VRAM
  // and then round to GPU-friendly multiples
  double totalPixelsPerTile = 1.0 + (upscaleFactor * upscaleFactor);
  double optimalTileSize =
      sqrt(availableForTiles /
           (totalPixelsPerTile * baseVRAMPerPixel * 1024.0 * 1024.0));

  // Round to nearest multiple of 64 for GPU efficiency (most GPUs prefer
  // multiples of 64) if it isn't it can result in artifacts in inference, too
  // close to VRAM max will also result in artifacts in inference or OOM
  int roundedTileSize = static_cast<int>((optimalTileSize / 64.0) + 0.5) * 64;

  // Ensure we don't exceed reasonable bounds
  int minTileSize = 64;
  int maxTileSize = std::max(minTileSize, roundedTileSize); // ensure high >= low
  int finalTileSize = std::clamp(roundedTileSize, minTileSize, maxTileSize);

  // Calculate actual VRAM usage for the chosen tile size
  uint64_t vramPerTile = estimateVRAMForTileSize(finalTileSize);
  int maxConcurrent = static_cast<int>(availableForTiles / vramPerTile);

  // Build the result
  TileSize best;
  best.tileSize = finalTileSize;
  best.maxConcurrentTiles = maxConcurrent;
  best.vramPerTile = vramPerTile;
  best.availableVRAM = availableForTiles;
  best.isSafe = (maxConcurrent >= 1);

  if (maxConcurrent >= 2) {
    best.reasoning = "Dynamic calculation: " + std::to_string(finalTileSize) +
                     "x" + std::to_string(finalTileSize) + " tiles with " +
                     std::to_string(maxConcurrent) +
                     " concurrent (optimal VRAM utilization)";
  } else if (maxConcurrent == 1) {
    best.reasoning = "Dynamic calculation: " + std::to_string(finalTileSize) +
                     "x" + std::to_string(finalTileSize) +
                     " tiles, single tile processing";
  } else {
    best.reasoning =
        "WARNING: Even calculated tile size may cause OOM! Available VRAM: " +
        std::to_string(availableForTiles / (1024 * 1024)) + " MB";
  }

  // Safety check for very low VRAM systems
  if (!best.isSafe) {
    best.reasoning =
        "WARNING: Even smallest tile size may cause OOM! Available VRAM: " +
        std::to_string(availableForTiles / (1024 * 1024)) + " MB";
  }

  return best;
}

std::tuple<uint64_t, uint64_t, uint64_t>
UpscalerService::calculateVramUsage(std::string modelName, int imageWidth,
                                    int imageHeight, int upscaleFactor) {
  // Look up model VRAM usage from the map
  auto it = modelVramUsageMap.find(modelName);
  if (it == modelVramUsageMap.end()) {
    // Fallback to default values if model not found
    Log(DEBUG, "UpscalerService",
        "Model {} not found in VRAM map, using default values", modelName);
    it =
        modelVramUsageMap.find("upscayl-ultrasharp-v2"); // Use a common default
  }

  const ModelVramInfo &info = it->second;
  uint64_t modelLoadVRAM = static_cast<uint64_t>(info.loadMB) * 1024 * 1024;
  uint64_t baseInferenceVRAM =
      static_cast<uint64_t>(info.inference512MB) * 1024 * 1024;
  uint64_t overheadVRAM = static_cast<uint64_t>(info.overheadMB) * 1024 * 1024;

  // Calculate input and output pixel counts
  uint64_t inputPixels =
      static_cast<uint64_t>(imageWidth) * static_cast<uint64_t>(imageHeight);
  uint64_t outputPixels = inputPixels * upscaleFactor * upscaleFactor;

  // Calculate VRAM per pixel based on the reference model (512x512 → 2048x2048)
  // Reference: 512*512 + 2048*2048 = 4,456,960 pixels uses baseInferenceVRAM
  double vramPerPixel = static_cast<double>(baseInferenceVRAM) /
                        (512.0 * 512.0 + 2048.0 * 2048.0);

  // Calculate inference VRAM for this specific image size with floor
  uint64_t scaledVRAM =
      static_cast<uint64_t>((inputPixels + outputPixels) * vramPerPixel);
  uint64_t inferenceVRAM = std::max(overheadVRAM, scaledVRAM);

  // Total VRAM needed = model load + inference
  uint64_t totalVRAM = modelLoadVRAM + inferenceVRAM;

  return {modelLoadVRAM, inferenceVRAM, totalVRAM};
}

bool UpscalerService::loadModel() {
  // Set up cleanup callback for memory tracking
  if (ncnnAllocator_) {
    ncnnAllocator_->setCleanupCallback([this](size_t freedBytes) {
      Log(DEBUG, "UpscalerService", "NcnnAllocator freed {} bytes", freedBytes);
    });
  }

  // Get model name for current resource type
  std::string resourceExtension = SClass::getExtension(currentResourceType_);
  std::string modelName =
      PIE4K_CFG.getUpscalerModelByResourceType(resourceExtension);
  std::string modelPath = PIE4K_CFG.NCNNModelPath + modelName;

  // Check if model is already loaded
  if (currentModelPath_ == modelPath && (sharedModel_ || cpuModel_)) {
    Log(DEBUG, "UpscalerService", "Model already loaded: {}", modelPath);
    return true;
  }

  Log(MESSAGE, "UpscalerService", "Loading NCNN model: {}", modelPath);

  // Start VRAM measurement before loading
  measureModelVRAMStart();

  // Check if model files exist
  if (!fs::exists(modelPath + ".param")) {
    Log(ERROR, "UpscalerService", "Model parameter file not found: {}.param",
        modelPath);
    return false;
  }
  if (!fs::exists(modelPath + ".bin")) {
    Log(ERROR, "UpscalerService", "Model binary file not found: {}.bin",
        modelPath);
    return false;
  }

  std::lock_guard<std::mutex> lock(modelMutex_);

  // Initialize NCNN Vulkan GPU instance (capture NCNN device prints)
  std::string vkInitOut = captureStderrOutput([&]() {
    ncnn::create_gpu_instance();
  });
  if (!vkInitOut.empty()) {
    Log(DEBUG, "UpscalerService", "Captured Vulkan init output: {}", vkInitOut);
  }
  int gpuCount = ncnn::get_gpu_count();
  int selectedGpu = PIE4K_CFG.UsableGPUIDs[0];
  if (gpuCount > 0) {
    if (selectedGpu < 0 || selectedGpu >= gpuCount) {
      Log(WARNING, "UpscalerService",
          "Configured GPU index {} out of range [0,{}). Using 0.", selectedGpu,
          gpuCount);
      selectedGpu = 0;
    }
    // Some ncnn versions use set_default_gpu, others expose only per-Net set_vulkan_device
    // We rely on per-Net set_vulkan_device below; keep default for allocators if available.
    Log(MESSAGE, "UpscalerService", "Using NCNN Vulkan GPU index: {}",
        selectedGpu);
  } else {
    Log(WARNING, "UpscalerService",
        "NCNN reports 0 Vulkan GPUs. GPU model load may fall back to CPU.");
  }

  // Clean up existing models
  sharedModel_.reset();
  cpuModel_.reset();

  // Try to load GPU model first (with Vulkan)
  auto gpuNet = std::make_unique<ncnn::Net>();
  gpuNet->opt.use_vulkan_compute = true;
  // Set per-Net GPU device
  {
    int gpuIndex = PIE4K_CFG.UsableGPUIDs[0];
    if (gpuIndex >= 0 && gpuIndex < ncnn::get_gpu_count()) {
      gpuNet->set_vulkan_device(gpuIndex);
      Log(DEBUG, "UpscalerService", "Assigned Net to Vulkan GPU index {}", gpuIndex);
    }
  }
  gpuNet->opt.num_threads =
      1; // Single thread per model instance since we have multiple threads
  gpuNet->opt.use_packing_layout = true; // Optimize memory access for GPU
  gpuNet->opt.use_fp16_storage = true;   // Reduce memory footprint
  gpuNet->opt.use_fp16_arithmetic = true;

  // Set the custom allocator for better memory management
  if (ncnnAllocator_) {
    gpuNet->opt.blob_allocator = ncnnAllocator_.get();
    gpuNet->opt.workspace_allocator = ncnnAllocator_.get();
  }

  Log(DEBUG, "UpscalerService", "Attempting to load GPU model with Vulkan");

  bool gpuLoadSuccess = false;
  std::string gpuOutput = captureStderrOutput([&]() {
    int paramResult = gpuNet->load_param((modelPath + ".param").c_str());
    int modelResult = gpuNet->load_model((modelPath + ".bin").c_str());

    Log(DEBUG, "UpscalerService",
        "GPU model loading results: param={}, model={}", paramResult,
        modelResult);

    if (paramResult == 0 && modelResult == 0) {
      gpuLoadSuccess = true;
      Log(DEBUG, "UpscalerService", "GPU model loaded successfully");
    } else {
      Log(ERROR, "UpscalerService",
          "GPU model loading failed: param={}, model={}", paramResult,
          modelResult);
      gpuNet.reset(); // Clear the failed GPU model
    }
  });

  // Log any captured stderr output for debugging
  if (!gpuOutput.empty()) {
    Log(DEBUG, "UpscalerService", "Captured GPU model loading output: {}",
        gpuOutput);
  }

  if (gpuLoadSuccess) {
    sharedModel_ = std::move(gpuNet);
    Log(MESSAGE, "UpscalerService", "GPU model loaded successfully: {}",
        modelPath);
  } else {
    Log(DEBUG, "UpscalerService",
        "GPU model loading failed, trying CPU fallback");

    // Try CPU model as fallback
    auto cpuNet = std::make_unique<ncnn::Net>();
    cpuNet->opt.use_vulkan_compute = false;
    cpuNet->opt.num_threads = 1;
    cpuNet->opt.lightmode = true;

    // Set the custom allocator for CPU model as well
    if (ncnnAllocator_) {
      cpuNet->opt.blob_allocator = ncnnAllocator_.get();
      cpuNet->opt.workspace_allocator = ncnnAllocator_.get();
    }

    if (cpuNet->load_param((modelPath + ".param").c_str()) == 0 &&
        cpuNet->load_model((modelPath + ".bin").c_str()) == 0) {
      cpuModel_ = std::move(cpuNet);
      Log(MESSAGE, "UpscalerService", "CPU model loaded successfully: {}",
          modelPath);
      gpuLoadSuccess = true; // We have a working model
    } else {
      Log(ERROR, "UpscalerService",
          "Failed to load both GPU and CPU models: {}", modelPath);
      return false;
    }
  }

  // Now calculate optimal settings based on actual VRAM and model loading
  // Use the modelVramUsageMap for model-specific VRAM requirements
  optimalSettings_ =
      calculateTileSize(modelName, totalSystemVRAM_, baselineVRAM_,
                        PIE4K_CFG.MaxVRAM, PIE4K_CFG.UpScaleFactor);

  Log(MESSAGE, "UpscalerService",
      "Optimal settings calculated: tile={}x{}, maxConcurrent={}, "
      "vramPerTile={}MB, reasoning: {}",
      optimalSettings_.tileSize, optimalSettings_.tileSize,
      optimalSettings_.maxConcurrentTiles,
      optimalSettings_.vramPerTile / (1024 * 1024), optimalSettings_.reasoning);

  if (gpuLoadSuccess) {
    currentModelPath_ = modelPath;

    // Log allocator memory usage
    if (ncnnAllocator_) {
      Log(DEBUG, "UpscalerService",
          "NcnnAllocator memory after model load: {} bytes ({} allocations)",
          ncnnAllocator_->getTotalAllocated(),
          ncnnAllocator_->getAllocationCount());
    }

    // Stop VRAM measurement and calculate model usage
    measureModelVRAMStop();

    return true;
  }

  return false;
}

ncnn::Net *UpscalerService::getModel() const {
  std::lock_guard<std::mutex> lock(modelMutex_);
  if (sharedModel_) {
    return sharedModel_.get();
  } else if (cpuModel_) {
    return cpuModel_.get();
  }
  return nullptr;
}

void UpscalerService::measureModelVRAMStart() {
  // Get current VRAM state using OperationsMonitor
  OperationsMonitor &monitor = OperationsMonitor::getInstance();

  // Get fresh metrics
  monitor.updateMetrics();
  SystemMetrics metrics = monitor.getCurrentMetrics();

  // Validate we got meaningful VRAM data
  if (metrics.totalVRAM == 0) {
    Log(ERROR, "UpscalerService",
        "Failed to detect system VRAM - ResourceMonitor returned 0");
    throw std::runtime_error("System VRAM detection failed - check GPU drivers "
                             "and ResourceMonitor configuration");
  }

  totalSystemVRAM_ = metrics.totalVRAM;
  baselineVRAM_ = metrics.usedVRAM;

  Log(MESSAGE, "UpscalerService",
      "VRAM measurement started - Total: {:.1f} GB, Baseline used: {:.1f} GB",
      totalSystemVRAM_ / (1024.0 * 1024.0 * 1024.0),
      baselineVRAM_ / (1024.0 * 1024.0 * 1024.0));
}

void UpscalerService::measureModelVRAMStop() {
  // Get current VRAM state after model loading
  OperationsMonitor &monitor = OperationsMonitor::getInstance();

  monitor.updateMetrics(); // Force fresh metrics
  SystemMetrics metrics = monitor.getCurrentMetrics();
  uint64_t currentVRAM = metrics.usedVRAM;

  if (currentVRAM > baselineVRAM_) {
    modelVRAMUsage_ = currentVRAM - baselineVRAM_;
    Log(MESSAGE, "UpscalerService", "Model VRAM usage measured: {:.1f} MB",
        modelVRAMUsage_ / (1024.0 * 1024.0));
    vramMeasured_ = true;
  } else {
    Log(ERROR, "UpscalerService",
        "Failed to measure model VRAM usage - Baseline: {:.1f} MB, Current: "
        "{:.1f} MB",
        baselineVRAM_ / (1024.0 * 1024.0), currentVRAM / (1024.0 * 1024.0));
    throw std::runtime_error("Model VRAM measurement failed - this indicates a "
                             "critical system or measurement issue");
  }
}

bool UpscalerService::upscaleSingleImage(const std::string &inputPath,
                                         const std::string &outputPath) {
  if (!isInitialized() || !getModel()) {
    Log(ERROR, "UpscalerService", "Service not properly initialized");
    return false;
  }

  // Load input image
  cv::Mat inputImage = cv::imread(inputPath, cv::IMREAD_UNCHANGED);
  if (inputImage.empty()) {
    Log(ERROR, "UpscalerService", "Failed to load input image: {}", inputPath);
    return false;
  }

  Log(DEBUG, "UpscalerService", "Processing image: {} ({}x{}, {} channels)",
      inputPath, inputImage.cols, inputImage.rows, inputImage.channels());

  // Create upscaler for this specific image
  int activeGpu = PIE4K_CFG.UsableGPUIDs[0];
  Upscaler upscaler(inputImage, getModel(), optimalSettings_.tileSize, activeGpu);
  // upscaler clones image
  inputImage.release();

  cv::Mat result;
  result = upscaler.upscale();

  if (result.empty()) {
    Log(ERROR, "UpscalerService", "Failed to process image: {}", inputPath);
    return false;
  }

  // Save the result
  if (!cv::imwrite(outputPath, result)) {
    Log(ERROR, "UpscalerService", "Failed to save output image: {}",
        outputPath);
    result.release();
    return false;
  }
  result.release();

  return true;
}

void UpscalerService::onLifecycleEvent(ServiceLifecycle event, const std::string& context) {
    switch (event) {
        case ServiceLifecycle::BATCH_UPSCALE_START:
            Log(DEBUG, "UpscalerService", "Batch upscale start event received");
            break;
            
        case ServiceLifecycle::BATCH_UPSCALE_END:
            Log(DEBUG, "UpscalerService", "Batch upscale end event received");
            cleanup();
            break;

        case ServiceLifecycle::RESOURCE_TYPE_START: {
          if (!context.empty()) {
            try {
              SClass_ID resourceType =
                  static_cast<SClass_ID>(std::stoi(context));
              Log(DEBUG, "UpscalerService",
                  "Resource type start event received for type: {}",
                  resourceType);

              initializeForResourceType(resourceType);

            } catch (const std::exception &e) {
              Log(ERROR, "UpscalerService",
                  "Failed to parse resource type from context: {}", context);
            }
          }
          break;
        }

        case ServiceLifecycle::RESOURCE_TYPE_END:
            Log(DEBUG, "UpscalerService", "Resource type end event received");
            allocatorCleanup();
            break;
            
        default:
            // Ignore other events for now
            break;
    }
}

bool UpscalerService::upscaleDirectory(const std::string& inputDir, const std::string& outputDir) {
    if (!isInitialized()) {
        Log(ERROR, "UpscalerService", "Service not initialized");
        return false;
    }

    if (!getModel()) {
      Log(ERROR, "UpscalerService", "No model loaded");
      return false;
    }

    Log(MESSAGE, "UpscalerService", "Upscaling directory from {} to {}",
        inputDir, outputDir);

    // Find all image files in input directory
    std::vector<std::string> imageFiles;
    for (const auto &entry : fs::recursive_directory_iterator(inputDir)) {
      if (entry.is_regular_file()) {
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
            ext == ".tiff" || ext == ".tif") {
          imageFiles.push_back(entry.path().string());
        }
      }
    }

    if (imageFiles.empty()) {
      Log(WARNING, "UpscalerService", "No image files found in directory: {}",
          inputDir);
      return true; // Not an error, just nothing to process
    }

    Log(MESSAGE, "UpscalerService", "Found {} image files to process",
        imageFiles.size());

    // Get model name and upscale factor for resource calculation
    std::string modelName = fs::path(currentModelPath_).stem().string();

    OperationsMonitor &monitor = OperationsMonitor::getInstance();

    // Submit each image as a separate task with its own resource requirements
    std::vector<std::future<bool>> taskFutures;
    int totalCount = imageFiles.size();

    for (const auto &inputPath : imageFiles) {
      // Generate output path
      fs::path relativePath = fs::relative(inputPath, inputDir);
      fs::path outputPath = fs::path(outputDir) / relativePath;

      // Quick dimension check using header only
      cv::Mat header = cv::imread(inputPath, cv::IMREAD_UNCHANGED);
      if (header.empty()) {
        Log(ERROR, "UpscalerService", "Failed to read image header: {}",
            inputPath);
        continue;
      }

      if (header.cols <= 4 && header.rows <= 4) {
        // Handle extremely small frame images with simple resize as they likely contain no real data
        Log(DEBUG, "UpscalerService",
            "Processing small image {} ({}x{}) with simple resize",
            fs::path(inputPath).filename().string(), header.cols, header.rows);

        // Load the full image for resizing
        cv::Mat smallImage = cv::imread(inputPath, cv::IMREAD_UNCHANGED);
        if (smallImage.empty()) {
          Log(ERROR, "UpscalerService", "Failed to load small image: {}",
              inputPath);
          header.release();
          continue;
        }

        // Resize by the configured upscale factor
        int newWidth = header.cols * PIE4K_CFG.UpScaleFactor;
        int newHeight = header.rows * PIE4K_CFG.UpScaleFactor;
        cv::Mat resizedImage;
        cv::resize(smallImage, resizedImage, cv::Size(newWidth, newHeight), 0,
                   0, cv::INTER_NEAREST);

        // Save the resized image
        if (!cv::imwrite(outputPath.string(), resizedImage)) {
          Log(ERROR, "UpscalerService",
              "Failed to save resized small image: {}", outputPath.string());
        } else {
          Log(DEBUG, "UpscalerService",
              "Successfully resized and saved small image to: {}",
              outputPath.string());
        }

        smallImage.release();
        resizedImage.release();
        header.release();
        totalCount--;
        continue;
      }

      // Calculate VRAM requirements for this specific image
      // Scale dimensions back up since we read a reduced version
      auto [loadVRAM, inferenceVRAM, totalVRAM] = calculateVramUsage(
          modelName, header.cols, header.rows, PIE4K_CFG.UpScaleFactor);

      uint64_t taskVRAM = totalVRAM;
      // task execution VRAM depends on method
      if (header.cols > optimalSettings_.tileSize ||
          header.rows > optimalSettings_.tileSize) {
        taskVRAM = optimalSettings_.vramPerTile * imageFiles.size();
      }

      // Create resource requirements for this task
      OperationRequirements req;
      req.estimatedVRAMUsage = taskVRAM;
      req.domain = ExecutionDomain::GPU;
      req.operationType = "upscale";
      req.resourceName = fs::path(inputPath).filename().string();
      req.resourceAccess = ResourceAccess::RESERVED;
      req.priority = TaskPriority::NORMAL;

      Log(DEBUG, "UpscalerService",
          "Submitting task for {} ({}x{}) - VRAM: {:.1f} MB", req.resourceName,
          header.cols, header.rows, totalVRAM / (1024.0 * 1024.0));
      header.release();

      // Submit task with specific requirements
      auto future = monitor.submitTaskWithRequirements(
          [this, inputPath, outputPath]() -> bool {
            return upscaleSingleImage(inputPath, outputPath.string());
          },
          req, "upscale_" + fs::path(inputPath).stem().string());

      taskFutures.push_back(std::move(future));
    }

    // Wait for all tasks to complete and collect results
    int successCount = 0;
    for (auto &future : taskFutures) {
      try {
        if (future.get()) {
          successCount++;
        }
      } catch (const std::exception &e) {
        Log(ERROR, "UpscalerService", "Task failed with exception: {}",
            e.what());
      }
    }

    Log(MESSAGE, "UpscalerService",
        "Directory processing completed: {}/{} files successful", successCount,
        totalCount);

    return successCount == totalCount;
}

// Helper function to capture stderr output
std::string UpscalerService::captureStderrOutput(std::function<void()> operation) {
  // Create a temporary file to capture stderr
  std::string tempFile =
      "/tmp/ncnn_output_" + std::to_string(getpid()) + ".tmp";

  // Save original stderr
  int originalStderr = dup(STDERR_FILENO);
  if (originalStderr == -1) {
    Log(WARNING, "Upscaler", "Failed to save original stderr");
    operation(); // Run operation without redirection
    return "";
  }

  // Redirect stderr to temporary file
  FILE *tempFp = fopen(tempFile.c_str(), "w");
  if (!tempFp) {
    Log(WARNING, "Upscaler",
        "Failed to create temporary file for stderr capture");
    close(originalStderr);
    operation(); // Run operation without redirection
    return "";
  }

  int tempFd = fileno(tempFp);
  dup2(tempFd, STDERR_FILENO);
  operation();

  // Restore original stderr
  fflush(stderr);
  dup2(originalStderr, STDERR_FILENO);
  close(originalStderr);
  fclose(tempFp);

  // Read captured output
  std::string capturedOutput;
  std::ifstream file(tempFile);
  if (file.is_open()) {
    std::string line;
    while (std::getline(file, line)) {
      if (!line.empty()) {
        capturedOutput += line + "\n";
      }
    }
    file.close();
  }

  // Clean up temp file
  unlink(tempFile.c_str());

  return capturedOutput;
}

} // namespace ProjectIE4k

// Register the service dynamically
REGISTER_SERVICE(UpscalerService)