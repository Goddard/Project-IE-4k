#pragma once

#include <string>
#include <vector>
#include <optional>

namespace ProjectIE4k {

// Lightweight, JSON-driven rules engine for filtering batch operations
class RulesEngine {
public:
    static RulesEngine& getInstance();

    // Load rules from a JSON file path. Safe to call multiple times.
    // If path is empty, attempts to load "rules.json" from the working directory.
    void load(const std::string& path = "");

    // Returns true if the resource should be processed for the given operation.
    // operation: "extract" | "upscale" | "assemble" | "batch" (free-form accepted)
    // resourceType: extension-like type string, e.g. "2DA", "GAM"
    bool shouldProcess(const std::string& operation,
                       const std::string& resourceType,
                       const std::string& resourceName) const;

private:
    RulesEngine() = default;
    RulesEngine(const RulesEngine&) = delete;
    RulesEngine& operator=(const RulesEngine&) = delete;

    struct Rule {
        std::string resourceType;                // e.g. "2DA" or "*"
        std::vector<std::string> operations;     // e.g. ["upscale"], ["*"], or empty (means all)
        std::vector<std::string> include;        // glob patterns; empty means allow all names
        std::vector<std::string> exclude;        // glob patterns
    };

    bool loaded_ = false;
    std::vector<Rule> rules_;
    std::optional<std::string> sourcePath_;

    static bool stringEqualsIgnoreCase(const std::string& a, const std::string& b);
    static bool matchesOperation(const Rule& r, const std::string& op);
    static bool matchesType(const Rule& r, const std::string& type);
    static bool matchAnyGlob(const std::vector<std::string>& patterns, const std::string& text);
    static bool matchGlob(const std::string& pattern, const std::string& text);
};

} // namespace ProjectIE4k


