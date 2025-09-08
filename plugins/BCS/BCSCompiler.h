#pragma once

#include <string>
#include <vector>
#include <memory>
#include "Signatures.h"

namespace ProjectIE4k {
    // Forward declarations
    struct BCSBlock;
    struct BCSTrigger;
    struct BCSAction;
    struct BCSObject;
    struct BCSResponse;
    class Signatures;

/**
 * BCS text-to-binary compiler
 * Converts human-readable BCS script text back to binary format
 * Implements Near Infinity's complete compilation pipeline
 */
class BCSCompiler {
public:
    BCSCompiler();
    ~BCSCompiler();

    /** Initialize compiler with signatures */
    bool initialize();
    
    /** Compile BCS text to binary blocks */
    bool compileText(const std::string& textContent, std::vector<BCSBlock>& blocks);
    
private:
    // Core compilation methods
    bool parseScriptStructure(const std::string& textContent, std::vector<BCSBlock>& blocks);
    bool compileTriggerFromText(const std::string& line, BCSTrigger& trigger);
    bool compileActionFromText(const std::string& line, BCSAction& action);
    
    // Near Infinity-style parameter parsing methods
    bool parseFunctionParameters(const std::string& line, void* function, BCSTrigger& trigger);
    bool parseFunctionParameters(const std::string& line, void* function, BCSAction& action, bool nearInfinityMode = false);
    
    // Complete parameter assignment methods (Near Infinity style)
    void setNumericParam(BCSTrigger& trigger, int index, int value);
    void setNumericParam(BCSAction& action, int index, int value);
    void setObjectParam(BCSTrigger& trigger, int index, const BCSObject& object);
    void setObjectParam(BCSAction& action, int index, const BCSObject& object);
    void setStringParams(BCSTrigger& trigger, const std::vector<std::string>& strings);
    void setStringParams(BCSAction& action, const std::vector<std::string>& strings);
    // Overloads with absolute positions and function context
    void setStringParams(BCSTrigger& trigger, const std::vector<std::string>& strings,
                         const std::vector<int>& positions, const Signatures::Function& func);
    void setStringParams(BCSAction& action, const std::vector<std::string>& strings,
                         const std::vector<int>& positions, const Signatures::Function& func);
    
    // Target/Identifier processing (Near Infinity style)
    void setTargetValue(BCSObject& object, int index, int value);
    void setIdentifierValue(BCSObject& object, int index, int value);
    
    // Helper parsing methods
    std::vector<std::string> splitParameters(const std::string& paramStr);
    bool parseObjectFromText(const std::string& text, BCSObject& object);
    int parseIntegerFromText(const std::string& text, void* param);
    std::string parseStringFromText(const std::string& text);
    
    // Point parameter handling
    bool parsePointFromText(const std::string& text, int& x, int& y);
    
    // Text generation methods (Near Infinity style)
    std::string generateTriggerText(const BCSTrigger& trigger);
    std::string generateActionText(const BCSAction& action);
    std::string generateObjectText(const BCSObject& object);
    
    // Parse code handling (Near Infinity style)
    int getParseCode(const std::string& paramType);
    
    // Negation and override handling
    bool parseNegation(const std::string& text, bool& negated);
    bool parseOverride(const std::string& text, int& overrideType);
    
    // Response probability handling
    bool parseResponseProbability(const std::string& text, int& weight);
    
    // Signature lookups
    std::shared_ptr<Signatures> triggers_;
    std::shared_ptr<Signatures> actions_;
    
    bool initialized_;
};

} // namespace ProjectIE4k
