#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "Upscaler.h"
#include "services/ServiceBase.h"
#include <ncnn/net.h>

namespace ProjectIE4k {
class NcnnAllocator;
/**
 * @brief Service that manages upscaler instances per resource type
 * 
 * Provides resource type-specific upscaler configurations and lifecycle management.
 * Only one upscaler instance is active at a time, optimized for the current resource type.
 */
class UpscalerService : public ServiceBase {
public:
    UpscalerService();
    ~UpscalerService() override;
    
    // ServiceBase interface implementation
    void initializeForResourceType(SClass_ID resourceType) override;
    void cleanup() override;
    bool isInitialized() const override;
    SClass_ID getCurrentResourceType() const override;
    
    // Lifecycle management
    ServiceLifecycle getLifecycle() const override { return ServiceLifecycle::BATCH_UPSCALE_START; }
    ServiceScope getScope() const override { return ServiceScope::BATCH_SCOPED; }
    bool shouldAutoInitialize() const override { return false; }
    void onLifecycleEvent(ServiceLifecycle event, const std::string& context = "") override;
    
    /**
     * @brief Upscale a directory of images
     * @param inputDir Directory containing images to upscale
     * @param outputDir Directory to save upscaled images
     * @return true if successful, false otherwise
     */
    bool upscaleDirectory(const std::string& inputDir, const std::string& outputDir) override;

    void allocatorCleanup();

  private:
    SClass_ID currentResourceType_;

    // Model management (moved from Upscaler)
    std::unique_ptr<ncnn::Net> sharedModel_;
    int sharedModelInputWidth_ = 0;
    int sharedModelInputHeight_ = 0;
    std::unique_ptr<ncnn::Net> cpuModel_;
    int cpuModelInputWidth_ = 0;
    int cpuModelInputHeight_ = 0;
    mutable std::mutex modelMutex_;
    std::string currentModelPath_;
    TileSize optimalSettings_;

    // Dynamic VRAM measurements
    uint64_t modelVRAMUsage_;
    uint64_t totalSystemVRAM_;
    uint64_t baselineVRAM_;
    bool vramMeasured_;

    // NCNN memory allocator for better memory management
    std::unique_ptr<NcnnAllocator> ncnnAllocator_;

    struct ModelVramInfo {
      int loadMB;         // Model load VRAM
      int inference512MB; // Inference VRAM for 512x512 → (upscale^2)
      int overheadMB;     // Fixed per-inference floor for tiny images
    };

    const std::map<std::string, ModelVramInfo> modelVramUsageMap = {
        {"upscayl-hfa2k", {364, 5314, 26}},
        {"upscayl-lsdir-4x-compact-c3", {180, 256, 16}},
        {"upscayl-lsdir-4x-plus-c", {364, 5314, 26}},
        {"upscayl-lsdir", {364, 5314, 26}},
        {"upscayl-nmkd-4x-siax-200k", {364, 5314, 26}},
        {"upscayl-nmkd-4x-superscale-sd-178000-g", {364, 5314, 26}},
        {"upscayl-nomos-4x-8k-sc", {364, 5314, 26}},
        {"upscayl-realesrgan-x4-general-wdn-v3", {188, 256, 16}},
        {"upscayl-realesrgan-x4plus-anime", {228, 5306, 18}},
        {"upscayl-realesrgan-x4plus-v3", {188, 256, 16}},
        {"upscayl-realesrgan-x4plus", {364, 5314, 26}},
        {"upscayl-remacri", {364, 5314, 26}},
        {"upscayl-ultramix_balanced", {364, 5314, 26}},
        {"upscayl-ultrasharp-v2", {364, 5314, 26}},
        {"upscayl-ultrasharp", {364, 5314, 26}},
        {"upscayl-uniscale-restore", {364, 5314, 26}},
        {"xintao-realesr-animevideo-x2-v3", {180, 256, 16}},
        {"xintao-realesr-animevideo-x3-v3", {180, 256, 16}},
        {"xintao-realesr-animevideo-x4-v3", {180, 256, 16}},
        {"xintao-realesrgan-x4plus-anime", {228, 5306, 18}},
        {"xintao-realesrgan-x4plus", {364, 5314, 26}},
        {"digital-art-4x", {228, 5306, 18}},
        {"high-fidelity-4x", {364, 5314, 26}},
        {"remacri-4x", {364, 5314, 26}},
        {"ultramix-balanced-4x", {364, 5314, 26}},
        {"ultrasharp-4x", {364, 5314, 26}},
        {"upscayl-lite-4x", {188, 256, 16}},
        {"upscayl-standard-4x", {364, 5314, 26}},
        {"unknown-2.0.1", {364, 5314, 26}},
        {"uniscale_restore", {364, 5314, 26}},
        {"RealESRGAN_General_x4_v3", {188, 256, 16}},
        {"RealESRGAN_General_WDN_x4_v3", {188, 256, 16}},
        {"realesr-animevideov3-x4", {180, 256, 16}},
        {"realesr-animevideov3-x3", {180, 256, 16}},
        {"realesr-animevideov3-x2", {180, 256, 16}},
        {"4x_NMKD-Superscale-SP_178000_G", {364, 5314, 26}},
        {"4x_NMKD-Siax_200k", {364, 5314, 26}},
        {"4xNomos8kSC", {364, 5314, 26}},
        {"4xLSDIRplusC", {364, 5314, 26}},
        {"4xLSDIRCompactC3", {180, 256, 16}},
        {"4xLSDIR", {364, 5314, 26}},
        {"4xHFA2k", {364, 5314, 26}}};

    /**
     * @brief Calculate optimal tile size for GPU processing based on available
     * VRAM
     * @param modelName Model name to use with modelVramUsageMap
     * @param totalVRAM Total VRAM available on the GPU
     * @param baselineUsedVRAM VRAM already in use by the system
     * @param maxVRAMPercent Maximum percentage of VRAM to use (from config)
     * @param upscaleFactor The upscale factor (2x, 4x, etc.)
     * @return Recommendation with optimal tile size and concurrency settings
     */
    TileSize calculateTileSize(std::string modelName, uint64_t totalVRAM,
                               uint64_t baselineUsedVRAM, double maxVRAMPercent,
                               int upscaleFactor);

    /**
     * @brief Calculate VRAM usage for processing an image of given dimensions
     * @param modelName Model name to use with modelVramUsageMap
     * @param imageWidth Input image width in pixels
     * @param imageHeight Input image height in pixels
     * @param upscaleFactor The upscale factor (2x, 4x, etc.)
     * @return VRAM usage breakdown: {modelLoadVRAM, inferenceVRAM, totalVRAM}
     */
    std::tuple<uint64_t, uint64_t, uint64_t>
    calculateVramUsage(std::string modelName, int imageWidth, int imageHeight,
                       int upscaleFactor);

    /**
     * @brief Load NCNN model for the current configuration
     * @return true if model loaded successfully
     */
    bool loadModel();

    /**
     * @brief Get the current loaded model (thread-safe)
     * @return Pointer to the loaded model, or nullptr if not loaded
     */
    ncnn::Net *getModel() const;

    /**
     * @brief Process a single image file
     * @param inputPath Path to input image
     * @param outputPath Path to save output image
     * @return true if successful
     */
    bool upscaleSingleImage(const std::string &inputPath,
                            const std::string &outputPath);

    /**
     * @brief Start VRAM measurement before model loading
     * Records baseline VRAM usage and validates system VRAM detection
     */
    void measureModelVRAMStart();

    /**
     * @brief Stop VRAM measurement after model loading
     * Calculates model VRAM usage from baseline
     */
    void measureModelVRAMStop();

    // helper
    std::string captureStderrOutput(std::function<void()> operation);
};

} // namespace ProjectIE4k 