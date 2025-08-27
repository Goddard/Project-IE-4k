#ifndef COMMAND_REGISTRY_H
#define COMMAND_REGISTRY_H

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <iostream>
#include <getopt.h>

namespace ProjectIE4k {

struct Action 
{
    std::string help;
    std::function<int(const std::vector<std::string>& args)> handler;
};

struct Command 
{
    std::string help;
    std::map<std::string, Action> actions;
};

using CommandTable = std::map<std::string, Command>;

// Function signature for plugin command registration
using CommandRegistrationFunction = void(*)(CommandTable&);

// Helper functions
static int prepareCommands(CommandTable& commandTable, int& argc, char** argv) {
    // Parse command arguments
    std::string type = argv[optind];
    std::string action = (optind + 1 < argc) ? argv[optind + 1] : "";
    std::string file = (optind + 2 < argc) ? argv[optind + 2] : "";

    // Find and execute command
    auto commandIt = commandTable.find(type);
    if (commandIt == commandTable.end()) {
        std::cerr << "Unknown command type: " << type << std::endl;
        std::cerr << "Available commands:" << std::endl;
        for (const auto& cmd : commandTable) {
            std::cerr << "  " << cmd.first << " - " << cmd.second.help << std::endl;
        }
        return 1;
    }

    auto actionIt = commandIt->second.actions.find(action);
    if (actionIt == commandIt->second.actions.end()) {
        std::cerr << "Unknown action: " << action << " for command: " << type << std::endl;
        std::cerr << "Available actions for " << type << ":" << std::endl;
        for (const auto& act : commandIt->second.actions) {
            std::cerr << "  " << act.first << " - " << act.second.help << std::endl;
        }
        
        return 1;
    }

    // // Prepare arguments and execute command
    std::vector<std::string> args;
    if (!file.empty()) args.push_back(file);
    for (int i = optind + 3; i < argc; i++) {
        args.push_back(argv[i]);
    }

    return actionIt->second.handler(args);
}

static void printHelp(const CommandTable& commandTable, const char* programName) {
    std::cout << "Usage: " << programName << " <type> <action> [-c config_file]" << std::endl;
    std::cout << "\nTypes:" << std::endl;
    for (const auto& cmd : commandTable) {
        std::cout << "  " << cmd.first << " - " << cmd.second.help << std::endl;
    }
    std::cout << "\nActions:" << std::endl;
    for (const auto& cmd : commandTable) {
        std::cout << cmd.first << " actions:" << std::endl;
        for (const auto& act : cmd.second.actions) {
            std::cout << "  " << act.first << " - " << act.second.help << std::endl;
        }
        std::cout << std::endl;
    }
    std::cout << "Optional:\n  -c <config_file> - GemRB configuration file (auto-detected if not specified)" << std::endl;
}

} // namespace ProjectIE4
#endif // COMMAND_REGISTRY_H 