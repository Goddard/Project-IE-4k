#pragma once

#include <string>
#include <map>
#include <functional>
#include <mutex>

namespace ProjectIE4k {

/**
 * @brief Distributed context system for command-line flags and global state
 * 
 * This class allows plugins and services to register their own context providers
 * for parsing command-line flags and other global state without centralizing
 * all flag knowledge in one place.
 */
class GlobalContext {
public:
    using ContextParser = std::function<void(int argc, char** argv, std::map<std::string, std::string>&)>;
    
    /**
     * @brief Get the singleton instance
     */
    static GlobalContext& getInstance() {
        static GlobalContext instance;
        return instance;
    }
    
    /**
     * @brief Register a context provider
     * @param providerName Unique namespace for this provider (e.g., "OperationsTracker")
     * @param parser Function that parses command-line args and populates context map
     */
    void registerContextProvider(const std::string& providerName, ContextParser parser);
    
    /**
     * @brief Parse all registered providers against command-line arguments
     * @param argc Argument count
     * @param argv Argument vector
     */
    void parseAll(int argc, char** argv);
    
    /**
     * @brief Get a context value for a specific provider
     * @param provider Provider namespace
     * @param key Key within that provider's context
     * @return Value string, or empty string if not found
     */
    std::string getValue(const std::string& provider, const std::string& key) const;
    
    /**
     * @brief Check if a provider has been registered
     * @param provider Provider namespace
     * @return true if registered
     */
    bool hasProvider(const std::string& provider) const;
    
    /**
     * @brief Get all context for a specific provider
     * @param provider Provider namespace
     * @return Map of all key-value pairs for that provider
     */
    std::map<std::string, std::string> getProviderContext(const std::string& provider) const;
    
    /**
     * @brief Clear all context (for testing)
     */
    void clear();

private:
    GlobalContext() = default;
    ~GlobalContext() = default;
    GlobalContext(const GlobalContext&) = delete;
    GlobalContext& operator=(const GlobalContext&) = delete;
    
    mutable std::mutex mutex_;
    std::map<std::string, ContextParser> parsers_;
    std::map<std::string, std::map<std::string, std::string>> contexts_;
};

} // namespace ProjectIE4k
