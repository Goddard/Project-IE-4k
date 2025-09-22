// 2DA plugin - plain text format per IESDP 2DA V1.0
#include "2DA.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include "plugins/PluginBase.h"
#include "TwoDATable.h"

namespace fs = std::filesystem;

namespace ProjectIE4k {

TwoDA::TwoDA(const std::string& resourceName)
	: PluginBase(resourceName, IE_2DA_CLASS_ID) {
	if (resourceName.empty()) {
		return;
	}
	if (!loadFromData()) {
		Log(ERROR, "2DA", "Failed to load 2DA data");
		return;
	}
	valid_ = true;
}

TwoDA::~TwoDA() {
}

bool TwoDA::loadFromData() {
	if (originalFileData.empty()) {
		Log(ERROR, "2DA", "No 2DA data loaded");
		return false;
	}
	// For text 2DA we don't need to parse up-front; just ensure it's text or allowed encrypted form
	if (isEncrypted2DA(originalFileData)) {
		Log(WARNING, "2DA", "Encrypted 2DA detected; leaving bytes untouched for extract/assemble");
	}
	return true;
}

bool TwoDA::extract() {
	if (!isValid()) {
		Log(ERROR, "2DA", "2DA resource is not valid");
		return false;
	}

	std::string outDir = getExtractDir(true);
	std::string outPath = outDir + "/" + originalFileName;

	// Write original bytes verbatim (supports encrypted variant too)
	std::ofstream f(outPath, std::ios::binary);
	if (!f.is_open()) {
		Log(ERROR, "2DA", "Failed to open output file: {}", outPath);
		return false;
	}
	f.write(reinterpret_cast<const char*>(originalFileData.data()), originalFileData.size());
	if (!f) {
		Log(ERROR, "2DA", "Failed to write file: {}", outPath);
		return false;
	}

	Log(MESSAGE, "2DA", "Extracted to {} ({} bytes)", outPath, originalFileData.size());
	return true;
}

bool TwoDA::upscale() {
	// Read 2DA text from extracted file if present; else from original bytes
	std::string extractDir = getExtractDir(false);
	std::string srcPath = extractDir + "/" + originalFileName;
	std::string upscaledDir = getUpscaledDir(true);
	std::string dstPath = upscaledDir + "/" + originalFileName;

	std::string text;
	if (fs::exists(srcPath)) {
		if (!readFileText(srcPath, text)) {
			Log(ERROR, "2DA", "Failed to read extracted 2DA: {}", srcPath);
			return false;
		}
	} else {
		// If encrypted, we cannot parse; write original bytes
		if (isEncrypted2DA(originalFileData)) {
			std::ofstream f(dstPath, std::ios::binary);
			if (!f.is_open()) return false;
			f.write(reinterpret_cast<const char*>(originalFileData.data()), originalFileData.size());
			if (!f) return false;
			Log(MESSAGE, "2DA", "Encrypted 2DA pass-through to {}", dstPath);
			return true;
		}
		text.assign(reinterpret_cast<const char*>(originalFileData.data()), originalFileData.size());
	}

	// Robust parse → modify → serialize
	TwoDATable table;
	if (!table.loadFromText(text)) {
		Log(ERROR, "2DA", "Failed to parse 2DA text for upscaling");
		return false;
	}

	int ups = PIE4K_CFG.UpScaleFactor;
	// Scale rows by prefixes START_XPOS and START_YPOS (handles *_TUTORIAL, *_MP, etc.)
	table.scaleRowsByPrefixes({"START_XPOS", "START_YPOS"}, ups);

	// Special-case: fonts.2da – scale PX_SIZE by sqrt(upscale)
	{
		std::string lowerName = originalFileName; for (auto &c : lowerName) c = static_cast<char>(std::tolower((unsigned char)c));
		if (lowerName == "fonts.2da") {
			int pxIdx = table.getColumnIndex("PX_SIZE");
			if (pxIdx != TwoDATable::npos) {
				double f;
				if (ups <= 1) f = 1.0; else if (ups == 4) f = 2.0; else if (ups == 9) f = 3.0; else f = 1.0; // simple sqrt mapping for common cases
				table.scaleIntegerColumnBy(pxIdx, f);
			}
		}
	}

	// Special-case: XL3000.2da – scale LOCX and LOCY coordinates
	{
		std::string lowerName = originalFileName; for (auto &c : lowerName) c = static_cast<char>(std::tolower((unsigned char)c));
		if (lowerName == "xnewarea.2da") {
			int locxIdx = table.getColumnIndex("LOCX");
			int locyIdx = table.getColumnIndex("LOCY");
			
			if (locxIdx != TwoDATable::npos) {
				Log(DEBUG, "2DA", "Upscaling LOCX column by factor {}", ups);
				table.scaleIntegerColumnBy(locxIdx, ups);
			}
			
			if (locyIdx != TwoDATable::npos) {
				Log(DEBUG, "2DA", "Upscaling LOCY column by factor {}", ups);
				table.scaleIntegerColumnBy(locyIdx, ups);
			}
			
			if (locxIdx != TwoDATable::npos || locyIdx != TwoDATable::npos) {
				Log(MESSAGE, "2DA", "Upscaled XL3000.2da coordinates by factor {}", ups);
			}
		}
	}

	std::string outText = table.serializeToText();
	std::ofstream out(dstPath);
	if (!out.is_open()) return false;
	out << outText;
	if (!out) return false;

	Log(MESSAGE, "2DA", "Upscaled 2DA written to {}", dstPath);
	return true;
}

bool TwoDA::assemble() {
	if (!isValid()) {
		Log(ERROR, "2DA", "2DA resource is not valid");
		return false;
	}

	std::string upscaledDir = getUpscaledDir(false);
	std::string upscaledPath = upscaledDir + "/" + originalFileName;
	std::string assembleDir = getAssembleDir(true);
	std::string assemblePath = assembleDir + "/" + originalFileName;

	try {
		if (fs::exists(upscaledPath)) {
			fs::copy_file(upscaledPath, assemblePath, fs::copy_options::overwrite_existing);
		} else {
			return false;
		}
	} catch (const std::exception& e) {
		Log(ERROR, "2DA", "Assembly failed: {}", e.what());
		return false;
	}

	Log(MESSAGE, "2DA", "Assembled 2DA to {}", assemblePath);
	return true;
}

bool TwoDA::extractAll() { return PluginManager::getInstance().extractAllResourcesOfType(IE_2DA_CLASS_ID); }
bool TwoDA::upscaleAll() { return PluginManager::getInstance().upscaleAllResourcesOfType(IE_2DA_CLASS_ID); }
bool TwoDA::assembleAll() { return PluginManager::getInstance().assembleAllResourcesOfType(IE_2DA_CLASS_ID); }

bool TwoDA::cleanExtractDirectory() { return cleanDirectory(getExtractDir(false)); }
bool TwoDA::cleanUpscaleDirectory() { return cleanDirectory(getUpscaledDir(false)); }
bool TwoDA::cleanAssembleDirectory() { return cleanDirectory(getAssembleDir(false)); }

std::string TwoDA::getOutputDir(bool ensureDir) const { return constructPath("-2da", ensureDir); }
std::string TwoDA::getExtractDir(bool ensureDir) const {
	std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-2da-extracted";
	if (ensureDir) ensureDirectoryExists(path);
	return path;
}
std::string TwoDA::getUpscaledDir(bool ensureDir) const {
	std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-2da-upscaled";
	if (ensureDir) ensureDirectoryExists(path);
	return path;
}
std::string TwoDA::getAssembleDir(bool ensureDir) const {
	std::string path = getOutputDir(ensureDir) + "/" + extractBaseName() + "-2da-assembled";
	if (ensureDir) ensureDirectoryExists(path);
	return path;
}

bool TwoDA::writeFileText(const std::string& path, const std::string& text) const {
	std::ofstream f(path);
	if (!f.is_open()) return false;
	f << text;
	return static_cast<bool>(f);
}

bool TwoDA::readFileText(const std::string& path, std::string& out) const {
	std::ifstream f(path);
	if (!f.is_open()) return false;
	std::ostringstream ss;
	ss << f.rdbuf();
	out = ss.str();
	return true;
}

bool TwoDA::isEncrypted2DA(const std::vector<uint8_t>& data) const {
	return data.size() >= 2 && data[0] == 0xFF && data[1] == 0xFF;
}

bool TwoDA::cleanDirectory(const std::string& dir) {
	if (!fs::exists(dir)) {
		return true;
	}
	try {
		for (const auto& entry : fs::directory_iterator(dir)) {
			if (entry.is_regular_file()) {
				fs::remove(entry.path());
			}
		}
		return true;
	} catch (const std::filesystem::filesystem_error& e) {
		Log(ERROR, "2DA", "Failed to clean directory {}: {}", dir, e.what());
		return false;
	}
}

void TwoDA::registerCommands(CommandTable& commandTable) {
	commandTable["2da"] = {
		"2DA file operations",
		{
			{"extract", {"Extract 2DA resource to file (e.g., 2da extract weapprof)",
				[](const std::vector<std::string>& args) -> int {
					if (args.empty()) { std::cerr << "Usage: 2da extract <resource_name>" << std::endl; return 1; }
					TwoDA p(args[0]);
					return p.extract() ? 0 : 1;
				}
			}},
			{"upscale", {"Pass-through upscale for 2DA (e.g., 2da upscale weapprof)",
				[](const std::vector<std::string>& args) -> int {
					if (args.empty()) { std::cerr << "Usage: 2da upscale <resource_name>" << std::endl; return 1; }
					TwoDA p(args[0]);
					return p.upscale() ? 0 : 1;
				}
			}},
			{"assemble", {"Assemble 2DA file (e.g., 2da assemble weapprof)",
				[](const std::vector<std::string>& args) -> int {
					if (args.empty()) { std::cerr << "Usage: 2da assemble <resource_name>" << std::endl; return 1; }
					TwoDA p(args[0]);
					return p.assemble() ? 0 : 1;
				}
			}}
		}
	};
}

REGISTER_PLUGIN(TwoDA, IE_2DA_CLASS_ID);

} // namespace ProjectIE4k

