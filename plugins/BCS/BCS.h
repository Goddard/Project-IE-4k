#ifndef BCS_H
#define BCS_H

#include <string>
#include <vector>

#include "core/SClassID.h"
#include "plugins/PluginBase.h"
#include "plugins/CommandRegistry.h"

namespace ProjectIE4k {

// BCS Object structure (matches the IESDP specification)
struct BCSObject {
    int ea;           // Enemy-Ally field
    int faction;      // Faction (PST only)
    int team;         // Team (PST only)
    int general;      // General type
    int race;         // Race
    int class_;       // Class
    int specific;     // Specific type
    int gender;       // Gender
    int alignment;    // Alignment
    int identifiers;  // Object identifiers
    int area[4];      // Area coordinates (IWD/IWD2)
    std::string name; // Object name
    
    BCSObject() : ea(0), faction(0), team(0), general(0), race(0), class_(0), 
                  specific(0), gender(0), alignment(0), identifiers(0) {
        for (int i = 0; i < 4; i++) area[i] = -1;
    }
};

// BCS Trigger structure
struct BCSTrigger {
    int opcode;           // Trigger ID from TRIGGER.IDS
    int param1;           // First integer parameter
    int flags;            // Flags (bit 0: negate condition)
    int param2;           // Second integer parameter
    int param3;           // Third integer parameter (unknown purpose)
    std::string var1;     // First string parameter
    std::string var2;     // Second string parameter
    BCSObject object;     // Object parameter
    
    BCSTrigger() : opcode(-1), param1(0), flags(0), param2(0), param3(0) {}
};

// BCS Action structure
struct BCSAction {
    int opcode;           // Action ID from ACTION.IDS
    BCSObject obj[3];     // Three object parameters
    int param1;           // First integer parameter
    int param2;           // Second integer parameter (point x)
    int param3;           // Third integer parameter (point y)
    int param4;           // Fourth integer parameter
    int param5;           // Fifth integer parameter
    std::string var1;     // First string parameter
    std::string var2;     // Second string parameter
    
    BCSAction() : opcode(-1), param1(0), param2(0), param3(0), param4(0), param5(0) {}
};

// BCS Response structure
struct BCSResponse {
    int weight;                           // Probability weight
    std::vector<BCSAction> actions;       // List of actions
    
    BCSResponse() : weight(100) {}
};

// BCS Block structure (Condition-Response block)
struct BCSBlock {
    std::vector<BCSTrigger> triggers;     // List of triggers (conditions)
    std::vector<BCSResponse> responses;   // List of responses
    
    BCSBlock() {}
};

// Main BCS Plugin Class
class BCS : public PluginBase {
public:
    BCS(const std::string& resourceName_);
    ~BCS();

    bool loadFromData();
    bool extract() override;
    bool upscale() override;
    bool assemble() override;

    // Directory management
    bool cleanExtractDirectory() override;
    bool cleanUpscaleDirectory() override;
    bool cleanAssembleDirectory() override;

    // Command registration
    static void registerCommands(CommandTable& commandTable);

    // PluginBase overrides
    std::string getResourceName() const override { return resourceName_; }
    bool isValid() const override { return valid_; }
    bool extractAll() override { return false; }
    bool upscaleAll() override { return false; }
    bool assembleAll() override { return false; }
    std::string getPluginName() const override { return "BCS"; }
    SClass_ID getResourceType() const override { return IE_BCS_CLASS_ID; }
    std::string getOutputDir(bool ensureDir = true) const override;
    std::string getExtractDir(bool ensureDir = true) const override;
    std::string getUpscaledDir(bool ensureDir = true) const override;
    std::string getAssembleDir(bool ensureDir = true) const override;

private:
    // BCS data structures
    struct BCSObject {
        int ea, faction, team, general, race, class_, specific, gender, alignment, identifiers;
        int area[4];
        std::string name;
    };

    struct BCSTrigger {
        int opcode, param1, param2, param3, flags;
        std::string var1, var2;
        BCSObject object;
    };

    struct BCSAction {
        int opcode, param1, param2, param3, param4, param5;
        std::string var1, var2;
        BCSObject obj[3];
    };

    struct BCSResponse {
        int weight;
        std::vector<BCSAction> actions;
    };

    struct BCSBlock {
        std::vector<BCSTrigger> triggers;
        std::vector<BCSResponse> responses;
    };

    std::vector<BCSBlock> blocks;

    // Universal IDS lookup system (like dltcep)
    std::map<std::string, std::map<int, std::string>> idsMaps; // Map<IDSFileName, Map<IDValue, IDName>>

    // Parsing methods
    bool parseScript();
    bool parseBlock(BCSBlock& block, size_t& offset);
    bool parseTrigger(BCSTrigger& trigger, size_t& offset);
    bool parseAction(BCSAction& action, size_t& offset);
    bool parseResponse(BCSResponse& response, size_t& offset);
    bool parseObject(BCSObject& object, size_t& offset);

    // Low-level reading methods
    bool readToken(const std::string& expected, size_t& offset);
    bool readNumber(int& value, size_t& offset);
    bool readString(std::string& value, size_t& offset);
    bool readArea(int area[4], size_t& offset);
    bool skipWhitespace(size_t& offset);
    bool findNextBlock(size_t& offset);

    // Writing methods
    bool writeScriptToFile(const std::string& filename);
    bool writeBlock(std::ofstream& file, const BCSBlock& block);
    bool writeTrigger(std::ofstream& file, const BCSTrigger& trigger);
    bool writeAction(std::ofstream& file, const BCSAction& action);
    bool writeResponse(std::ofstream& file, const BCSResponse& response);
    bool writeObject(std::ofstream& file, const BCSObject& object);
    bool writeNumber(std::ofstream& file, int value);
    bool writeString(std::ofstream& file, const std::string& value);
    bool writeArea(std::ofstream& file, const int area[4]);

    // Decompilation methods
    bool decompileToText(const std::string& filename);
    std::string decompileTrigger(const BCSTrigger& trigger);
    std::string decompileAction(const BCSAction& action);
    std::string decompileResponse(const BCSResponse& response);
    std::string decompileObject(const BCSObject& object);

    // Universal IDS lookup methods (like dltcep)
    bool loadIDSFiles();
    bool loadIDSFileFromResource(const std::string& resourceName);
    std::string getIDSName(const std::string& idsFile, int value);
    int getIDSValue(const std::string& idsFile, const std::string& name);

    // Upscaling methods
    void upscaleLine(std::string& line, int upscaleFactor);
    bool applyUpscaledCoordinates(const std::string& filename);
    void applyCoordinatesToAction(BCSAction& action, const std::string& line);

    // Directory helpers
    bool cleanDirectory(const std::string& dir);
};

} // namespace ProjectIE4k
#endif // BCS_H


