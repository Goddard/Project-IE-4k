#include <iostream>
#include <string>
#include <vector>

#include "core/CFG.h"
#include "core/Logging/Logging.h"
#include "plugins/CommandRegistry.h"
#include "plugins/PluginManager.h"
#include "services/ServiceManager.h"

using namespace std::filesystem;
using namespace ProjectIE4k;

int main(int argc, char **argv) {
    std::string configFile;
    // Manual scan for -c anywhere
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "-c" && i + 1 < argc) {
            configFile = argv[i + 1];
            ++i;
            continue;
        }
        if (a.rfind("-c=", 0) == 0 && a.size() > 3) {
            configFile = std::string(a.substr(3));
            continue;
        }
    }
    
    // Auto-detect config file if not specified
    if (configFile.empty()) {
        configFile = PIE4K_CFG.findConfigFile();
        if (configFile.empty()) {
            std::cerr << "Error: No config file specified with -c and no .cfg file found in current directory" << std::endl;
            return 1;
        }
    }
    
    // Initialize command registry for help or execution
    CommandTable commandTable;
    PluginManager::getInstance().registerAllCommands(commandTable);

    // Show help if no command specified
    if (argc <= 1) {
        printHelp(commandTable, argv[0]);
        return 0;
    }

    InitializeLogging();
    // Initialize ProjectIE4k CFG
    PIE4K_CFG.initialize(configFile);
    ToggleLogging(PIE4K_CFG.Logging);
    Log(MESSAGE, "Core", "Project IE 4K using GamePath: {}", PIE4K_CFG.GamePath);

    // Trigger application start lifecycle
    ServiceManager::onApplicationStart();

    int result = prepareCommands(commandTable, argc, argv);

    // Trigger application shutdown lifecycle
    ServiceManager::onApplicationShutdown();

    // Shutdown our logging system
    ShutdownLogging();
    return result;
}
