#pragma once

#include "Signatures.h"
#include "IdsMapCache.h"
#include <string>
#include <memory>
#include <set>

namespace ProjectIE4k {

// Forward declarations
struct BCSObject;
struct BCSAction;
struct BCSTrigger;

/**
 * Near Infinity-style BCS decompiler
 */
class BcsDecompiler {
public:
    BcsDecompiler();
    ~BcsDecompiler();

    /** Initialize decompiler with signatures */
    bool initialize();
    
    /** Set upscaling mode with factor */
    void setUpscaling(bool enabled, int factor = 4);
    
    /** Check if upscaling is enabled */
    bool isUpscalingEnabled() const { return upscalingEnabled_; }
    
    /** Set current function context for upscaling decisions */
    void setCurrentFunction(const std::string& functionName) { currentFunction_ = functionName; }

    /** Decompile trigger to string */
    std::string decompileTrigger(const BCSTrigger& trigger);

    /** Decompile action to string */
    std::string decompileAction(const BCSAction& action);

    /** Decompile object to string */
    std::string decompileObject(const BCSObject& object);
    std::string decompileObject(const BCSObject& object, const Signatures::Function* function, int paramIndex);

    /** Set whether to generate errors for missing signatures */
    void setGenerateErrors(bool generate) { generateErrors_ = generate; }

    /** Set whether to generate comments */
    void setGenerateComments(bool generate) { generateComments_ = generate; }

private:
    std::shared_ptr<Signatures> triggers_;
    std::shared_ptr<Signatures> actions_;
    bool generateErrors_;
    bool generateComments_;
    bool upscalingEnabled_;
    int upscaleFactor_;
    
    /** Functions to skip during upscaling */
    std::set<std::string> upscaleSkipList_;
    
    /** Current function being decompiled (for upscaling context) */
    std::string currentFunction_;

    /** Decompile number with IDS resolution */
    std::string decompileNumber(long value, const Signatures::Parameter& param);

    /** Decompile string parameter */
    std::string decompileString(const std::string& value, const Signatures::Parameter& param);

    /** Decompile object target (EA, GENERAL, RACE, etc.) Near Infinity style */
    std::string decompileObjectTarget(const BCSObject& object);

    /** Generate normalized symbol from IDS entry */
    std::string getNormalizedSymbol(const std::string& symbol);

    /** Check if IDS file contains bitwise flags */
    bool isBitwiseIds(const std::string& idsName);

    /** Handle SpellRES/HaveSpellRES with empty resources */
    std::pair<std::string, std::vector<std::string>> handleSpellTransformation(
        const std::string& functionName, 
        const std::vector<std::string>& params,
        long spellId);
    
    std::vector<std::string> handleConcatenatedStrings(
        const std::string& functionName,
        const std::vector<std::string>& params,
        const std::string& var1,
        const std::string& var2);
        
    /** Check if an object is empty (all fields are zero/empty) */
    bool isEmptyObject(const BCSObject& object);
    
    /** Choose best matching function signature based on actual parameter data */
    Signatures::Function* getBestMatchingFunction(const std::vector<Signatures::Function*>& functions, const BCSAction& action);
    Signatures::Function* getBestMatchingFunction(const std::vector<Signatures::Function*>& functions, const BCSTrigger& trigger);
};

} // namespace ProjectIE4k
