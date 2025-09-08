#ifndef TWO_DA_H
#define TWO_DA_H

#pragma once

#include <string>
#include <vector>

#include "plugins/PluginBase.h"
#include "plugins/CommandRegistry.h"
#include "core/SClassID.h"

namespace ProjectIE4k {

class TwoDA : public PluginBase {
public:
	explicit TwoDA(const std::string& resourceName = "");
	~TwoDA() override;

	// Core operations
	bool extract() override;
	bool upscale() override; // no-op for text 2DA
	bool assemble() override;

	// PluginBase interface
	std::string getResourceName() const override { return resourceName_; }
	bool isValid() const override { return valid_; }

	// Batch operations
	bool extractAll() override;
	bool upscaleAll() override;
	bool assembleAll() override;

	// Directories
	bool cleanExtractDirectory() override;
	bool cleanUpscaleDirectory() override;
	bool cleanAssembleDirectory() override;

	// Metadata
	std::string getPluginName() const override { return "2DA"; }
	SClass_ID getResourceType() const override { return IE_2DA_CLASS_ID; }

	// Path helpers
	std::string getOutputDir(bool ensureDir = true) const override;
	std::string getExtractDir(bool ensureDir = true) const override;
	std::string getUpscaledDir(bool ensureDir = true) const override;
	std::string getAssembleDir(bool ensureDir = true) const override;

	// Command registration
	static void registerCommands(CommandTable& commandTable);

private:
	bool loadFromData();
	bool writeFileText(const std::string& path, const std::string& text) const;
	bool readFileText(const std::string& path, std::string& out) const;
	bool isEncrypted2DA(const std::vector<uint8_t>& data) const;
	bool cleanDirectory(const std::string& dir);
};

} // namespace ProjectIE4k

#endif // TWO_DA_H


