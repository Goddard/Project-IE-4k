#include "GlobalContext.h"

namespace ProjectIE4k {

void GlobalContext::registerContextProvider(const std::string& providerName, ContextParser parser) {
    std::lock_guard<std::mutex> lock(mutex_);
    parsers_[providerName] = parser;
}

void GlobalContext::parseAll(int argc, char** argv) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Clear existing contexts
    contexts_.clear();
    
    // Run each provider's parser
    for (const auto& [providerName, parser] : parsers_) {
        try {
            parser(argc, argv, contexts_[providerName]);
        } catch (const std::exception& e) {
            // Log error but continue with other providers
            // Note: We can't use Log here since it might not be initialized yet
            // This should be called early in main()
        }
    }
}

std::string GlobalContext::getValue(const std::string& provider, const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto providerIt = contexts_.find(provider);
    if (providerIt != contexts_.end()) {
        auto keyIt = providerIt->second.find(key);
        if (keyIt != providerIt->second.end()) {
            return keyIt->second;
        }
    }
    return "";
}

bool GlobalContext::hasProvider(const std::string& provider) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return parsers_.find(provider) != parsers_.end();
}

std::map<std::string, std::string> GlobalContext::getProviderContext(const std::string& provider) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = contexts_.find(provider);
    if (it != contexts_.end()) {
        return it->second;
    }
    return {};
}

void GlobalContext::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    parsers_.clear();
    contexts_.clear();
}

} // namespace ProjectIE4k
