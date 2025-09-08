#include "RulesEngine.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/Logging/Logging.h"

namespace fs = std::filesystem;

namespace ProjectIE4k {

RulesEngine& RulesEngine::getInstance() {
    static RulesEngine instance;
    return instance;
}

void RulesEngine::load(const std::string& path) {
    if (loaded_) return; // simple one-time load; can be extended for reload later

    auto loadFile = [&](const fs::path& p, const std::string& contextType, const std::string& contextName){
        try {
            std::ifstream f(p);
            nlohmann::json j; f >> j;
            if (j.contains("rules") && j["rules"].is_array()) {
                for (const auto& jr : j["rules"]) {
                    Rule r;
                    // Defaults from file context
                    r.resourceType = jr.value("resourceType", contextType.empty() ? std::string("*") : contextType);
                    if (jr.contains("operations")) {
                        for (const auto& op : jr["operations"]) r.operations.push_back(op.get<std::string>());
                    }
                    if (jr.contains("include")) {
                        for (const auto& p2 : jr["include"]) r.include.push_back(p2.get<std::string>());
                    }
                    if (jr.contains("exclude")) {
                        for (const auto& p2 : jr["exclude"]) r.exclude.push_back(p2.get<std::string>());
                    }
                    // If no include specified and we have a resource context, default to that resource name
                    if (r.include.empty() && !contextName.empty()) {
                        r.include.push_back(contextName);
                    }
                    rules_.push_back(std::move(r));
                }
            }
            Log(DEBUG, "Rules", "Loaded rules from {} (type='{}', name='{}')", p.string(), contextType, contextName);
        } catch (const std::exception& e) {
            Log(ERROR, "Rules", "Failed to load rules from {}: {}", p.string(), e.what());
        }
    };

    // 1) Optional explicit file path
    if (!path.empty() && fs::exists(path)) {
        loadFile(path, "", "");
    }

    // 2) Global rules.json in root
    fs::path base = "rules.json";
    if (fs::exists(base)) {
        loadFile(base, "", "");
    }

    // 3) Per-type and per-resource rule files under rules/
    fs::path rulesDir = "rules";
    if (fs::exists(rulesDir) && fs::is_directory(rulesDir)) {
        // Collect *.json and sort lex so type files (e.g., 2DA.json) appear before resource files (e.g., START.2DA.json)
        std::vector<fs::path> files;
        for (const auto& entry : fs::directory_iterator(rulesDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        std::sort(files.begin(), files.end());

        for (const auto& p : files) {
            std::string stem = p.stem().string(); // e.g., "2DA" or "START.2DA"
            std::string contextType;
            std::string contextName;
            auto dot = stem.find('.');
            if (dot == std::string::npos) {
                // Type-level file, e.g., 2DA.json
                contextType = stem;
                contextName.clear();
            } else {
                // Resource-level, e.g., START.2DA.json → name=START, type=2DA
                contextName = stem.substr(0, dot);
                contextType = stem.substr(dot + 1);
            }
            loadFile(p, contextType, contextName);
        }
    }

    loaded_ = true;
}

bool RulesEngine::stringEqualsIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

bool RulesEngine::matchesOperation(const Rule& r, const std::string& op) {
    if (r.operations.empty()) return true;
    for (const auto& o : r.operations) {
        if (o == "*" || stringEqualsIgnoreCase(o, op)) return true;
    }
    return false;
}

bool RulesEngine::matchesType(const Rule& r, const std::string& type) {
    return r.resourceType == "*" || stringEqualsIgnoreCase(r.resourceType, type);
}

// Very small globbing: supports '*' wildcard only
bool RulesEngine::matchGlob(const std::string& pattern, const std::string& text) {
    // Split pattern on '*'
    if (pattern == "*") return true;
    size_t pi = 0, ti = 0, star = std::string::npos, match = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || std::tolower((unsigned char)pattern[pi]) == std::tolower((unsigned char)text[ti]))) {
            ++pi; ++ti;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star = ++pi; match = ti;
        } else if (star != std::string::npos) {
            pi = star; ti = ++match;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

bool RulesEngine::matchAnyGlob(const std::vector<std::string>& patterns, const std::string& text) {
    for (const auto& p : patterns) {
        if (matchGlob(p, text)) return true;
    }
    return false;
}

bool RulesEngine::shouldProcess(const std::string& operation,
                               const std::string& resourceType,
                               const std::string& resourceName) const {
    if (!loaded_ || rules_.empty()) return true; // default allow

    // Evaluate rules in order; last matching rule wins (common, flexible behavior)
    std::optional<bool> decision;
    bool anyApplicable = false; // any rules that target this (type, operation)
    for (const auto& r : rules_) {
        if (!matchesType(r, resourceType)) continue;
        if (!matchesOperation(r, operation)) continue;
        anyApplicable = true;
        // Try matching against plain resource name and name with extension (e.g., START vs START.2DA)
        std::string nameWithExt = resourceName + "." + resourceType;
        bool included = r.include.empty() ? true : (matchAnyGlob(r.include, resourceName) || matchAnyGlob(r.include, nameWithExt));
        bool excluded = matchAnyGlob(r.exclude, resourceName) || matchAnyGlob(r.exclude, nameWithExt);
        if (excluded) decision = false;
        else if (included) decision = true;
        // else: no decision from this rule
    }
    // If there are applicable rules but none matched, default to deny; otherwise allow.
    return decision.value_or(!anyApplicable);
}

} // namespace ProjectIE4k


