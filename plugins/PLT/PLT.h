#ifndef PLT_H
#define PLT_H

#pragma once

#include <string>

#include "PLTV1.hpp"
#include "core/SClassID.h"
#include "plugins/CommandRegistry.h"
#include "plugins/PluginBase.h"

namespace ProjectIE4k {

class PLT : public PluginBase {
public:
  PLT(const std::string &resourceName = "");
  ~PLT() override;

  // Core operations
  bool extract() override;
  bool assemble() override;

  // PluginBase interface
  std::string getResourceName() const override { return resourceName_; }
  bool isValid() const override { return valid_; }

  // Batch operations (delegated to PluginManager)
  bool extractAll() override;
  bool upscaleAll() override;
  bool assembleAll() override;

  // Cleaning
  bool cleanExtractDirectory() override;
  bool cleanUpscaleDirectory() override;
  bool cleanAssembleDirectory() override;

  // Metadata
  std::string getPluginName() const override { return "PLT"; }
  SClass_ID getResourceType() const override { return IE_PLT_CLASS_ID; }

  // Paths
  std::string getOutputDir(bool ensureDir = true) const override;
  std::string getExtractDir(bool ensureDir = true) const override;
  std::string getUpscaledDir(bool ensureDir = true) const override;
  std::string getAssembleDir(bool ensureDir = true) const override;

  static void registerCommands(CommandTable &commandTable);

private:
  bool valid_ = false;

  // PLT data
  PLTV1File pltFile;

  // Helpers
  bool loadData();
  bool convertPltToPng();
  bool convertPngToPlt();

  bool cleanDirectory(const std::string &dir);
};

} // namespace ProjectIE4k

#endif // PLT_H
