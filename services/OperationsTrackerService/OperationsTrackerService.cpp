#include "OperationsTrackerService.h"

#include <filesystem>
#include <chrono>
#include <thread>

#include <nlohmann/json.hpp>

#include "core/GlobalContext.h"
#include "core/Logging/Logging.h"
#include "core/CFG.h"

namespace fs = std::filesystem;
namespace ProjectIE4k {

static std::string nowIso8601() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[64];
#ifdef _WIN32
    tm tm_buf;
    gmtime_s(&tm_buf, &t);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
#else
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
#endif
    return std::string(buf);
}

void OperationsTrackerService::initializeForResourceType(SClass_ID resourceType) {
    std::lock_guard<std::mutex> lock(mtx_);
    currentResourceType_ = resourceType;
    initialized_ = true;
}

void OperationsTrackerService::cleanup() {
    std::lock_guard<std::mutex> lock(mtx_);
    if (ledger_.is_open()) {
        ledger_.flush();
        ledger_.close();
    }
    latestCache_.clear();
    initialized_ = false;
    currentResourceType_ = 0;
}

void OperationsTrackerService::onLifecycleEvent(ServiceLifecycle event, const std::string& context) {
    if (event == ServiceLifecycle::APPLICATION_START) {
        std::lock_guard<std::mutex> lock(mtx_);
        ensureLedgerOpen();
        
        // Register context provider for --force flag
        GlobalContext::getInstance().registerContextProvider("OperationsTracker", 
            [](int argc, char** argv, std::map<std::string, std::string>& context) {
                for (int i = 1; i < argc; ++i) {
                    if (std::string(argv[i]) == "--force") {
                        context["force"] = "true";
                        break;
                    }
                }
            });
    }
    if (event == ServiceLifecycle::APPLICATION_SHUTDOWN) {
        cleanup();
    }
}

std::string OperationsTrackerService::getOpsDirUnsafe() const {
    fs::path base = fs::path("output") / PIE4K_CFG.GameType / ".pie4k";
    return base.string();
}

void OperationsTrackerService::ensureLedgerOpen() {
    if (ledger_.is_open()) return;
    std::string opsDir = getOpsDirUnsafe();
    try {
        fs::create_directories(opsDir);
    } catch (...) { }
    
    fs::path ledgerPath = fs::path(opsDir) / "ops.jsonl";
    ledger_.open(ledgerPath, std::ios::app);
    if (!ledger_.is_open()) {
        Log(ERROR, "OperationsTrackerService", "Failed to open ledger: {}", ledgerPath.string());
    }
    
    if (!cacheLoaded_) {
        // Start background thread to load cache
        std::thread([this, ledgerPath]() {
            loadLedgerIntoCacheUnsafe();
        }).detach();
    }
}

void OperationsTrackerService::loadLedgerIntoCacheUnsafe() {
    try {
        std::string opsDir = getOpsDirUnsafe();
        fs::path ledgerPath = fs::path(opsDir) / "ops.jsonl";
        
        std::ifstream in(ledgerPath);
        if (!in.is_open()) {
            cacheLoaded_ = true;
            return;
        }
        
        std::string line;
        while (std::getline(in, line)) {
            // Simple parse for events we care about; avoid JSON dep
            // Expect either {"event":"start", ... "phase":"..","resourceType":"..","resourceName":"..","fp":{...}}
            // or     {"event":"end",   ... "phase":"..","resourceType":"..","resourceName":"..","success":true}
            auto getField = [&](const std::string& key) -> std::string {
                std::string pat = "\"" + key + "\":";
                size_t pos = line.find(pat);
                if (pos == std::string::npos) return "";
                pos += pat.size();
                // value can be string (\"...\") or number/bool
                if (pos < line.size() && line[pos] == '"') {
                    size_t end = line.find('"', pos + 1);
                    if (end != std::string::npos) return line.substr(pos + 1, end - (pos + 1));
                    return "";
                } else {
                    size_t end = line.find_first_of(",}\n", pos);
                    if (end == std::string::npos) end = line.size();
                    return line.substr(pos, end - pos);
                }
            };

            auto event = getField("event");
            if (event != "start" && event != "end") continue;

            std::string phase = getField("phase");
            std::string resourceType = getField("resourceType");
            std::string resourceName = getField("resourceName");
            if (phase.empty() || resourceType.empty() || resourceName.empty()) continue;
            auto key = makeKey(phase, resourceType, resourceName);

            // Thread-safe access to cache
            {
                std::lock_guard<std::mutex> lock(mtx_);
                
                if (event == "start") {
                    LatestEntry &le = latestCache_[key];
                    le.configHash = getField("configHash");
                    le.opVersion = getField("opVersion");
                    // numeric fields are within fp; attempt to parse
                    auto parseUint = [&](const std::string& k) -> uint64_t {
                        std::string v = getField(k);
                        if (v.empty()) return 0;
                        try { return static_cast<uint64_t>(std::stoull(v)); } catch (...) { return 0; }
                    };
                    auto parseInt = [&](const std::string& k) -> int64_t {
                        std::string v = getField(k);
                        if (v.empty()) return 0;
                        try { return static_cast<int64_t>(std::stoll(v)); } catch (...) { return 0; }
                    };

                    le.bifIndex = static_cast<int>(parseInt("bifIndex"));
                    le.keyLocator = static_cast<uint32_t>(parseUint("keyLocator"));
                    le.size = static_cast<uint32_t>(parseUint("size"));
                    le.sourcePath = getField("sourcePath");
                    le.mtime = parseUint("mtime");
                    le.overrideSize = parseUint("overrideSize");
                    // success remains as previously known (default false)
                } else if (event == "end") {
                    bool success = false;
                    std::string s = getField("success");
                    if (!s.empty()) {
                        // s could be true/false
                        success = (s.find("true") != std::string::npos || s == "1");
                    }
                    LatestEntry &le = latestCache_[key];
                    le.success = success;
                }
            }
        }
    } catch (...) {
        // ignore parsing errors
    }
    
    cacheLoaded_ = true;
    Log(DEBUG, "OperationsTrackerService", "Background cache loading completed");
}

void OperationsTrackerService::writeJsonlUnsafe(const std::string& line) {
    if (!ledger_.is_open()) return;
    ledger_ << line << '\n';
    ledger_.flush();
}

std::string OperationsTrackerService::makeKey(const std::string& phase, const std::string& resourceType, const std::string& resourceName) const {
    return phase + "|" + resourceType + "|" + resourceName;
}

void OperationsTrackerService::startPhase(const std::string& phase, const std::string& resourceType, size_t total) {
    std::lock_guard<std::mutex> lock(mtx_);
    ensureLedgerOpen();
    
    nlohmann::json entry = {
        {"event", "phase_start"},
        {"ts", nowIso8601()},
        {"phase", phase},
        {"resourceType", resourceType},
        {"total", total}
    };
    
    writeJsonlUnsafe(entry.dump());
}

void OperationsTrackerService::endPhase(const std::string& phase, const std::string& resourceType, bool allSucceeded) {
    std::lock_guard<std::mutex> lock(mtx_);
    ensureLedgerOpen();
    
    nlohmann::json entry = {
        {"event", "phase_end"},
        {"ts", nowIso8601()},
        {"phase", phase},
        {"resourceType", resourceType},
        {"allSucceeded", allSucceeded}
    };
    
    writeJsonlUnsafe(entry.dump());
    if (allSucceeded) {
        try {
            fs::path donePath = fs::path(getOpsDirUnsafe()) / "complete";
            fs::create_directories(donePath);
            std::string marker = phase + "_" + resourceType + ".done";
            std::ofstream f(donePath / marker);
        } catch (...) {}
    }
}

bool OperationsTrackerService::shouldProcessPhase(const std::string& phase, const std::string& resourceType) {
    // Check for phase completion marker to avoid reprocessing (unless force flag is set)
    fs::path doneMarker = fs::path(getOpsDirUnsafe()) / "complete" / (phase + "_" + resourceType + ".done");
    bool globalForce = (GlobalContext::getInstance().getValue("OperationsTracker", "force") == "true");
    
    if (fs::exists(doneMarker) && !globalForce) {
        Log(MESSAGE, "OperationsTracker", "Phase '%s' for type %s already done, skipping", phase.c_str(), resourceType.c_str());
        return false;
    } else if (globalForce && fs::exists(doneMarker)) {
        Log(MESSAGE, "OperationsTracker", "Force flag set: ignoring phase completion marker for %s %s", phase.c_str(), resourceType.c_str());
    }
    
    return true;
}

bool OperationsTrackerService::shouldProcess(const std::string& phase,
                       const std::string& resourceType,
                       const std::string& resourceName,
                       const InputFingerprint& fp,
                       bool force) {
    // Check GlobalContext for force flag (takes precedence over parameter)
    bool globalForce = (GlobalContext::getInstance().getValue("OperationsTracker", "force") == "true");
    if (force || globalForce) return true;
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = latestCache_.find(makeKey(phase, resourceType, resourceName));
    if (it == latestCache_.end()) return true;
    const LatestEntry& le = it->second;
    if (!le.success) return true; // retry failures
    bool fpMatch = (le.configHash == fp.configHash && le.opVersion == fp.opVersion &&
                   le.bifIndex == fp.bifIndex && le.keyLocator == fp.keyLocator && le.size == fp.size &&
                   le.sourcePath == fp.sourcePath && le.mtime == fp.mtime && le.overrideSize == fp.overrideSize);
    return !fpMatch; // process only if fingerprint changed
}

void OperationsTrackerService::markStarted(const std::string& phase,
                     const std::string& resourceType,
                     const std::string& resourceName,
                     const InputFingerprint& fp) {
    std::lock_guard<std::mutex> lock(mtx_);
    ensureLedgerOpen();
    
    nlohmann::json entry = {
        {"event", "start"},
        {"ts", nowIso8601()},
        {"phase", phase},
        {"resourceType", resourceType},
        {"resourceName", resourceName},
        {"fp", {
            {"configHash", fp.configHash},
            {"opVersion", fp.opVersion},
            {"bifIndex", fp.bifIndex},
            {"keyLocator", fp.keyLocator},
            {"size", fp.size},
            {"sourcePath", fp.sourcePath},
            {"mtime", fp.mtime},
            {"overrideSize", fp.overrideSize}
        }}
    };
    
    writeJsonlUnsafe(entry.dump());

    // Update cache with provided fingerprint for immediate shouldProcess decisions
    LatestEntry &le = latestCache_[makeKey(phase, resourceType, resourceName)];
    le.configHash = fp.configHash;
    le.opVersion = fp.opVersion;
    le.bifIndex = fp.bifIndex;
    le.keyLocator = fp.keyLocator;
    le.size = fp.size;
    le.sourcePath = fp.sourcePath;
    le.mtime = fp.mtime;
    le.overrideSize = fp.overrideSize;
}

void OperationsTrackerService::markCompleted(const std::string& phase,
                       const std::string& resourceType,
                       const std::string& resourceName,
                       bool success,
                       const std::vector<std::string>& outputPaths,
                       const std::string& errorMessage) {
    std::lock_guard<std::mutex> lock(mtx_);
    ensureLedgerOpen();
    
    nlohmann::json entry = {
        {"event", "end"},
        {"ts", nowIso8601()},
        {"phase", phase},
        {"resourceType", resourceType},
        {"resourceName", resourceName},
        {"success", success},
        {"outputs", outputPaths}
    };
    
    if (!errorMessage.empty()) {
        entry["error"] = errorMessage;
    }
    
    writeJsonlUnsafe(entry.dump());

    LatestEntry le;
    le.success = success;
    // We do not update fingerprint here because we didn't receive it; rely on prior markStarted
    // and cache merging from shouldProcess callers that set it beforehand if desired.
    auto key = makeKey(phase, resourceType, resourceName);
    auto it = latestCache_.find(key);
    if (it != latestCache_.end()) {
        it->second.success = success;
    } else {
        latestCache_[key] = le;
    }
}

} // namespace ProjectIE4k

REGISTER_SERVICE(OperationsTrackerService)


