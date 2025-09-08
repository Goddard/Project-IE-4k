#ifndef BCS_H
#define BCS_H

#include <string>
#include <vector>
#include <map>
#include <memory>

#include "core/SClassID.h"
#include "core/CFG.h"
#include "plugins/PluginBase.h"
#include "plugins/CommandRegistry.h"
#include "BcsDecompiler.h"
#include "BCSCompiler.h"

namespace ProjectIE4k {

// BCS Object structure (matches Near Infinity specification)
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
    int subrace;      // Subrace (IWD2 only)
    int identifiers[5]; // Object identifiers array (OBJECT.IDS)
    int area[4];      // Area coordinates (IWD/IWD2)
    std::string name; // Object name

    BCSObject() : ea(0), faction(0), team(0), general(0), race(0), class_(0),
                  specific(0), gender(0), alignment(0), subrace(0) {
        for (int i = 0; i < 5; i++) identifiers[i] = 0;
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
    
    // ActionOverride support: for functions that take actions as parameters
    bool hasNestedAction;
    std::unique_ptr<BCSAction> nestedAction;
    
    BCSAction() : opcode(-1), param1(0), param2(0), param3(0), param4(0), param5(0), hasNestedAction(false) {}
    
    // Copy constructor and assignment operator for proper handling of unique_ptr
    BCSAction(const BCSAction& other)
        : opcode(other.opcode), param1(other.param1), param2(other.param2), param3(other.param3),
          param4(other.param4), param5(other.param5), var1(other.var1), var2(other.var2),
          hasNestedAction(other.hasNestedAction) {
        for (int i = 0; i < 3; ++i) obj[i] = other.obj[i];
        if (other.nestedAction) {
            nestedAction = std::make_unique<BCSAction>(*other.nestedAction);
        }
    }
    
    BCSAction& operator=(const BCSAction& other) {
        if (this != &other) {
            opcode = other.opcode;
            param1 = other.param1;
            param2 = other.param2;
            param3 = other.param3;
            param4 = other.param4;
            param5 = other.param5;
            var1 = other.var1;
            var2 = other.var2;
            hasNestedAction = other.hasNestedAction;
            for (int i = 0; i < 3; ++i) obj[i] = other.obj[i];
            if (other.nestedAction) {
                nestedAction = std::make_unique<BCSAction>(*other.nestedAction);
            } else {
                nestedAction.reset();
            }
        }
        return *this;
    }
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
    
    // Shared resource management
    bool initializeSharedResources() override;
    void cleanupSharedResources() override;
    bool hasSharedResources() const override { return true; }

private:
    std::vector<BCSBlock> blocks;

    // Near Infinity-style decompiler
    std::unique_ptr<BcsDecompiler> decompiler_;
    std::unique_ptr<BCSCompiler> compiler_;
    
    /** Load all available IDS files dynamically */
    bool loadIDSFiles();

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
    bool writeBlockBinary(std::ofstream& file, const BCSBlock& block);
    bool writeTrigger(std::ofstream& file, const BCSTrigger& trigger);
    bool writeTriggerBinary(std::ofstream& file, const BCSTrigger& trigger);
    bool writeAction(std::ofstream& file, const BCSAction& action);
    bool writeActionBinary(std::ofstream& file, const BCSAction& action);
    bool writeResponse(std::ofstream& file, const BCSResponse& response);
    bool writeResponseBinary(std::ofstream& file, const BCSResponse& response);
    bool writeObject(std::ofstream& file, const BCSObject& object);
    bool writeObjectBinary(std::ofstream& file, const BCSObject& object);
    bool writeNumber(std::ofstream& file, int value);
    bool writeString(std::ofstream& file, const std::string& value);
    bool writeArea(std::ofstream& file, const int area[4]);

    // Decompilation methods
    bool decompileToText(const std::string& filename);

    // Compilation methods
    bool compileTextToBinary(const std::string& textContent);
    
    // Upscaling methods
    void upscaleLine(std::string& line, int upscaleFactor);
    bool applyUpscaledCoordinates(const std::string& filename);
    void applyCoordinatesToAction(BCSAction& action, const std::string& line);

    // Lazy initialization
    bool ensureDecompilerInitialized();
    bool decompilerInitialized_ = false;
    bool ensureCompilerInitialized();
    bool compilerInitialized_ = false;
    
    // Directory helpers
    bool cleanDirectory(const std::string& dir);
};

} // namespace ProjectIE4k
#endif // BCS_H


