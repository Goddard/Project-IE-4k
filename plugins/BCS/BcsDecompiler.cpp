#include "BcsDecompiler.h"
#include "BCS.h" // For BCS structures
#include "core/Logging/Logging.h"
#include <algorithm>
#include <sstream>
#include <set>
#include <iostream>
#include <climits>

namespace ProjectIE4k {

// Bitwise IDS files (from Near Infinity)
static const std::set<std::string> BITWISE_IDS = {
    "AREATYPE", "AREAFLAG", "BITS", "CLASSMSK", "CREAREFL", "DAMAGES", 
    "DOORFLAG", "DMGTYPE", "EXTSTATE", "INVITEM", "ITEMFLAG", "JOURTYPE", 
    "MAGESPEC", "SPLCAST", "STATE", "WMPFLAG"
};

BcsDecompiler::BcsDecompiler() 
    : generateErrors_(false), generateComments_(true), upscalingEnabled_(false), upscaleFactor_(4) { 
    
    // Initialize upscale skip list for functions that shouldn't have coordinates upscaled
    upscaleSkipList_ = {
        "FadeToColor", "FadeFromColor", "ScreenShake",
        "SetViewport", "SetCameraFacing", "SetWeather",
        "SetRestEncounterProbabilityDay", "SetRestEncounterProbabilityNight",
        "SetAreaRestFlag", "SetMasterArea", "CreatePartyGold",
        "GivePartyGold", "TakePartyGold", "SetNumTimesTalkedTo",
        "IncrementChapter", "SetGlobalTimer", "RealSetGlobalTimer",
        "SetAreaScript", "ChangeAIScript", "ChangeClass",
        "ChangeStat", "SetStat"
        // Add more functions as needed
    };
}

BcsDecompiler::~BcsDecompiler() = default;

void BcsDecompiler::setUpscaling(bool enabled, int factor) {
    upscalingEnabled_ = enabled;
    upscaleFactor_ = factor;
}

bool BcsDecompiler::initialize() {
    triggers_ = Signatures::getTriggers();
    actions_ = Signatures::getActions();
    
    if (!triggers_ || !actions_) {
        Log(DEBUG, "BcsDecompiler", "Failed to load trigger or action signatures");
        return false;
    }
    
    Log(DEBUG, "BcsDecompiler", "Initialized BCS decompiler successfully");
    
    return true;
}

std::string BcsDecompiler::decompileTrigger(const BCSTrigger& trigger) {
    // Debug output for trigger parameters
    Log(DEBUG, "BcsDecompiler", "TRIGGER {} - param1={}, param2={}, param3={}, flags={}, var1='{}', var2='{}'", 
        trigger.opcode, trigger.param1, trigger.param2, trigger.param3, trigger.flags, trigger.var1, trigger.var2);
    
    auto functions = triggers_->getFunction(trigger.opcode);
    if (functions.empty()) {
        // Try with flipped bit (Near Infinity pattern)
        int altOpcode = trigger.opcode ^ 0x4000;
        functions = triggers_->getFunction(altOpcode);
        if (functions.empty()) {
            if (generateErrors_) {
                Log(DEBUG, "BcsDecompiler", "No signature found for trigger 0x{:X}", trigger.opcode);
            }
            return "// Error - Could not find trigger 0x" + std::to_string(trigger.opcode);
        }
    }
    
    // Choose best matching function signature based on actual parameter data
    Signatures::Function* function = getBestMatchingFunction(functions, trigger);
    
    // Set current function context for upscaling decisions
    setCurrentFunction(function->getName());
    
    std::vector<std::string> params;
    int curNum = 0, curString = 0, curObj = 0, curPoint = 0;
    bool handledConcatenatedStrings = false;
    
    // Process parameters according to signature
    for (size_t i = 0; i < function->getNumParameters(); ++i) {
        const auto& param = function->getParameter(i);
        std::string paramValue;
        
        switch (param.getType()) {
            case Signatures::TYPE_INTEGER: {
                long value;
                if (curNum == 0) value = trigger.param1;
                else if (curNum == 1) value = trigger.param2;
                else value = trigger.param3;
                curNum++;
                
                paramValue = decompileNumber(value, param);
                break;
            }
            
            case Signatures::TYPE_STRING: {
                std::string value = (curString == 0) ? trigger.var1 : trigger.var2;
                curString++;
                paramValue = decompileString(value, param);
                break;
            }
            
            case Signatures::TYPE_OBJECT: {
                BCSObject* objPtr = nullptr;
                if (curObj == 0) objPtr = const_cast<BCSObject*>(&trigger.object);
                curObj++;
                
                if (objPtr) {
                    paramValue = decompileObject(*objPtr, function, i);
                    // If decompiled value is [Symbol] where Symbol is an OBJECT.IDS identifier, strip brackets (NI style)
                    if (paramValue.size() > 2 && paramValue.front() == '[' && paramValue.back() == ']') {
                        std::string inner = paramValue.substr(1, paramValue.size() - 2);
                        if (!inner.empty()) {
                            bool isObjectIdsSymbol = false;
                            if (auto* objMap = IdsMapCache::get("OBJECT.IDS")) {
                                if (objMap->get(inner) != nullptr) {
                                    isObjectIdsSymbol = true;
                                } else {
                                    std::string upper = inner;
                                    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                                    if (objMap->get(upper) != nullptr) {
                                        isObjectIdsSymbol = true;
                                    }
                                }
                            }
                            if (isObjectIdsSymbol) {
                                paramValue = inner;
                            }
                        }
                    }
                    if (paramValue.empty()) {
                        // Default to [ANYONE] for empty objects
                        paramValue = "[ANYONE]";
                    }
                } else {
                    paramValue = "[ANYONE]";
                }
                break;
            }
            
            case Signatures::TYPE_POINT: {
                // P-type parameters: Point structures [x.y]
                long x, y;
                if (curNum == 0) { x = trigger.param1; y = trigger.param2; curNum += 2; }
                else if (curNum == 1) { x = trigger.param2; y = trigger.param3; curNum += 2; }
                else { x = trigger.param3; y = 0; curNum++; } // Single value point
                
                // Apply coordinate upscaling
                if (upscalingEnabled_ && upscaleSkipList_.find(currentFunction_) == upscaleSkipList_.end()) {
                    long originalX = x, originalY = y;
                    x *= upscaleFactor_;
                    y *= upscaleFactor_;
                    
                    // Log upscaled coordinates
                    Log(DEBUG, "BcsDecompiler", "UPSCALED COORDINATE: {}.{} [{}.{}] -> [{}.{}]", 
                        currentFunction_, param.getName(), originalX, originalY, x, y);
                }
                
                paramValue = "[" + std::to_string(x) + "." + std::to_string(y) + "]";
                break;
            }
                
            default:
                paramValue = "0";
                break;
        }
        
        // Always include the parameter. Empty strings must be emitted as "" per NI.
        params.push_back(paramValue);
    }
    
    // Handle SpellRES/HaveSpellRES transformations
    auto [finalName, finalParams] = handleSpellTransformation(function->getName(), params, trigger.param1);
    
    // Handle concatenated strings
    finalParams = handleConcatenatedStrings(finalName, finalParams, trigger.var1, trigger.var2);
    
    // Build result
    std::string result = finalName + "(";
    for (size_t i = 0; i < finalParams.size(); ++i) {
        if (i > 0) result += ",";
        result += finalParams[i];
    }
    result += ")";
    
    return (trigger.flags & 1) ? "!" + result : result;
}

std::string BcsDecompiler::decompileAction(const BCSAction& action) {
    // Debug output for action parameters
    Log(DEBUG, "BcsDecompiler", "ACTION {} - param1={}, param2={}, param3={}, param4={}, param5={}, var1='{}', var2='{}'", 
        action.opcode, action.param1, action.param2, action.param3, action.param4, action.param5, action.var1, action.var2);
    
    auto functions = actions_->getFunction(action.opcode);
    if (functions.empty()) {
        if (generateErrors_) {
            Log(DEBUG, "BcsDecompiler", "No signature found for action {}", action.opcode);
        }
        return "// Error - Could not find action " + std::to_string(action.opcode);
    }
    

    
    // Choose best matching function signature based on actual parameter data
    Signatures::Function* function = getBestMatchingFunction(functions, action);
    
    // Set current function context for upscaling decisions
    setCurrentFunction(function->getName());
    
    std::vector<std::string> params;
    int curNum = 0, curString = 0, curObj = 1; // Skip first object (ActionOverride)
    bool handledConcatenatedStrings = false;
    
    // Process parameters according to signature
    for (size_t i = 0; i < function->getNumParameters(); ++i) {
        const auto& param = function->getParameter(i);
        std::string paramValue;
        
        switch (param.getType()) {
            case Signatures::TYPE_INTEGER: {
                long value;
                if (curNum == 0) value = action.param1;
                else if (curNum == 1) value = action.param4;  // Skip point params (param2, param3)
                else value = action.param5;
                curNum++;
                
                paramValue = decompileNumber(value, param);
                break;
            }
            
            case Signatures::TYPE_STRING: {
                std::string value = (curString == 0) ? action.var1 : action.var2;
                curString++;
                paramValue = decompileString(value, param);
                break;
            }
            
            case Signatures::TYPE_OBJECT: {
                const BCSObject* objPtr = nullptr;
                if (curObj < 3) objPtr = &action.obj[curObj];
                curObj++;
                
                if (objPtr) {
                    paramValue = decompileObject(*objPtr, function, i);
                    // If [Symbol] and Symbol is an OBJECT.IDS identifier, strip brackets
                    if (paramValue.size() > 2 && paramValue.front() == '[' && paramValue.back() == ']') {
                        std::string inner = paramValue.substr(1, paramValue.size() - 2);
                        if (!inner.empty()) {
                            bool isObjectIdsSymbol = false;
                            if (auto* objMap = IdsMapCache::get("OBJECT.IDS")) {
                                if (objMap->get(inner) != nullptr) {
                                    isObjectIdsSymbol = true;
                                } else {
                                    std::string upper = inner;
                                    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
                                    if (objMap->get(upper) != nullptr) {
                                        isObjectIdsSymbol = true;
                                    }
                                }
                            }
                            if (isObjectIdsSymbol) {
                                paramValue = inner;
                            }
                        }
                    }
                    if (paramValue.empty()) {
                        // Default to [ANYONE] for empty objects
                        paramValue = "[ANYONE]";
                    }
                } else {
                    paramValue = "[ANYONE]";
                }
                break;
            }
            
            case Signatures::TYPE_POINT: {
                // P-type parameters: Point structures [x.y]
                // Based on Near Infinity: point is always at param2 (x) and param3 (y)
                long x = action.param2;
                long y = action.param3;
                
                // Apply coordinate upscaling
                if (upscalingEnabled_ && upscaleSkipList_.find(currentFunction_) == upscaleSkipList_.end()) {
                    long originalX = x, originalY = y;
                    x *= upscaleFactor_;
                    y *= upscaleFactor_;
                    
                    // Log upscaled coordinates
                    Log(DEBUG, "BcsDecompiler", "UPSCALED COORDINATE: {}.{} [{}.{}] -> [{}.{}]", 
                        currentFunction_, param.getName(), originalX, originalY, x, y);
                }
                
                paramValue = "[" + std::to_string(x) + "." + std::to_string(y) + "]";
                break;
            }
            

                
            default:
                paramValue = "0";
                break;
        }
        
        // Always include the parameter. Empty strings must be emitted as "" per NI.
        params.push_back(paramValue);
    }
    
    // Handle SpellRES/HaveSpellRES transformations for actions
    auto [finalName, finalParams] = handleSpellTransformation(function->getName(), params, action.param1);
    
    // Handle concatenated strings
    finalParams = handleConcatenatedStrings(finalName, finalParams, action.var1, action.var2);
    
    // Build result
    std::string result = finalName + "(";
    for (size_t i = 0; i < finalParams.size(); ++i) {
        if (i > 0) result += ",";
        result += finalParams[i];
    }
    result += ")";
    
    // Handle ActionOverride - if obj[0] is not empty, wrap in ActionOverride
    if (!isEmptyObject(action.obj[0])) {
        std::string overrideObject = decompileObject(action.obj[0]);
        result = "ActionOverride(" + overrideObject + "," + result + ")";
    }
    
    return result;
}

std::string BcsDecompiler::decompileObject(const BCSObject& object) {
    // Near Infinity-style object decompilation
    // Process: target -> string -> identifiers -> assembly
    

    
    std::string target;
    std::vector<std::string> identifiers;
    
    // 1. Handle string names first (quoted names like "Door01") - highest priority
    if (!object.name.empty()) {
        target = "\"" + object.name + "\"";

    } else {
        // 2. Get target (EA.GENERAL.RACE... format or simple identifiers)
        target = decompileObjectTarget(object);

    }
    
    // 3. Get OBJECT.IDS identifiers (functions like NearestEnemyOf)
    for (int i = 4; i >= 0; i--) { // Most specific to least specific
        if (object.identifiers[i] != 0) {
            std::string objectName = IdsMapCache::getIdsSymbol("OBJECT.IDS", object.identifiers[i]);
            if (!objectName.empty()) {
                identifiers.push_back(getNormalizedSymbol(objectName));
            } else {
                identifiers.push_back("UnknownObject" + std::to_string(object.identifiers[i]));
            }
        }
    }
    
    // 4. Use default if nothing found
    if (target.empty() && identifiers.empty()) {
        target = "[ANYONE]";
    }
    
    // 5. Assemble nested structure: identifiers(target)
    std::string result;
    
    // Build nested function calls
    for (size_t i = 0; i < identifiers.size(); i++) {
        result += identifiers[i];
        if (i + 1 < identifiers.size() || !target.empty()) {
            result += "(";
        }
    }
    
    // Add target
    if (!target.empty()) {
        result += target;
    }
    
    // Close parentheses
    for (size_t i = 0; i < identifiers.size(); i++) {
        if (i + 1 < identifiers.size() || !target.empty()) {
            result += ")";
        }
    }
    
    return result.empty() ? "[ANYONE]" : result;
}

std::string BcsDecompiler::decompileObject(const BCSObject& object, const Signatures::Function* function, int paramIndex) {
    // Check if object is empty and provide context-aware defaults
    if (isEmptyObject(object) && function != nullptr) {
        std::string functionName = function->getName();
        
        // Smart defaults based on function and parameter context
        if (functionName == "CreateCreatureObject" && paramIndex == 1) {
            // Second parameter (target object) defaults to Myself
            return "Myself";
        }
        // Add more context-specific defaults as needed
        
        // Fall back to generic default
        return "[ANYONE]";
    }
    
    // Use standard decompilation for non-empty objects
    return decompileObject(object);
}

std::string BcsDecompiler::decompileObjectTarget(const BCSObject& object) {
    // Build [EA.GENERAL.RACE.CLASS.SPECIFIC.GENDER.ALIGNMENT] format
    
    // Check if target fields are empty
    if (object.ea == 0 && object.general == 0 && object.race == 0 && 
        object.class_ == 0 && object.specific == 0 && object.gender == 0 &&
        object.alignment == 0) {
        return ""; // Empty target
    }
    
    // Note: Near Infinity keeps bracket form even for simple EA-only targets (e.g., [GOODCUTOFF]).
    // We only return bare "Myself" when resolved via OBJECT.IDS identifiers elsewhere.
    
    // Build full target specification [EA.GENERAL.RACE...]
    std::vector<std::string> targetParts;
    
    // EA
    if (object.ea != 0) {
        std::string eaName = IdsMapCache::getIdsSymbol("EA.IDS", object.ea);
        targetParts.push_back(eaName.empty() ? std::to_string(object.ea) : getNormalizedSymbol(eaName));
    } else {
        targetParts.push_back("0");
    }
    
    // GENERAL  
    if (object.general != 0) {
        std::string generalName = IdsMapCache::getIdsSymbol("GENERAL.IDS", object.general);
        targetParts.push_back(generalName.empty() ? std::to_string(object.general) : getNormalizedSymbol(generalName));
    } else {
        targetParts.push_back("0");
    }
    
    // RACE
    if (object.race != 0) {
        std::string raceName = IdsMapCache::getIdsSymbol("RACE.IDS", object.race);
        targetParts.push_back(raceName.empty() ? std::to_string(object.race) : getNormalizedSymbol(raceName));
    } else {
        targetParts.push_back("0");
    }
    
    // CLASS
    if (object.class_ != 0) {
        std::string className = IdsMapCache::getIdsSymbol("CLASS.IDS", object.class_);
        targetParts.push_back(className.empty() ? std::to_string(object.class_) : getNormalizedSymbol(className));
    } else {
        targetParts.push_back("0");
    }
    
    // SPECIFIC
    if (object.specific != 0) {
        std::string specificName = IdsMapCache::getIdsSymbol("SPECIFIC.IDS", object.specific);
        targetParts.push_back(specificName.empty() ? std::to_string(object.specific) : getNormalizedSymbol(specificName));
    } else {
        targetParts.push_back("0");
    }
    
    // GENDER
    if (object.gender != 0) {
        std::string genderName = IdsMapCache::getIdsSymbol("GENDER.IDS", object.gender);
        targetParts.push_back(genderName.empty() ? std::to_string(object.gender) : getNormalizedSymbol(genderName));
    } else {
        targetParts.push_back("0");
    }
    
    // ALIGNMENT
    if (object.alignment != 0) {
        std::string alignName = IdsMapCache::getIdsSymbol("ALIGN.IDS", object.alignment);
        targetParts.push_back(alignName.empty() ? std::to_string(object.alignment) : getNormalizedSymbol(alignName));
    } else {
        targetParts.push_back("0");
    }
    
    // Remove trailing zeros
    while (!targetParts.empty() && targetParts.back() == "0") {
        targetParts.pop_back();
    }
    
    // Build [EA.GENERAL.RACE...] format
    if (targetParts.empty()) {
        return "[ANYONE]";
    }
    
    std::string result = "[";
    for (size_t i = 0; i < targetParts.size(); i++) {
        if (i > 0) result += ".";
        result += targetParts[i];
    }
    result += "]";
    
    return result;
}

std::string BcsDecompiler::decompileString(const std::string& value, const Signatures::Parameter& param) {
    // Simple string formatting - just wrap in quotes
    return "\"" + value + "\"";
}

std::string BcsDecompiler::decompileNumber(long value, const Signatures::Parameter& param) {
    // Handle coordinate upscaling for P-type parameters (Point structures)
    if (upscalingEnabled_ && param.getType() == 'P' && 
        upscaleSkipList_.find(currentFunction_) == upscaleSkipList_.end()) {
        
        // All P-type parameters in BCS represent coordinate data that should be upscaled
        long originalValue = value;
        value *= upscaleFactor_;
        
        // Debug output for upscaled coordinates
        std::cout << "UPSCALED COORDINATE: " << currentFunction_ << "." << param.getName() << " " << originalValue << " -> " << value << std::endl;
    }
    
    std::string idsRef = param.getIdsRef();
    if (!idsRef.empty() && idsRef != "STRREF") {
        // Handle IDS reference aliases
        if (idsRef == "TimeODay") {
            idsRef = "TIMEODAY";
        }
        std::string idsName = idsRef + ".IDS";
        std::transform(idsName.begin(), idsName.end(), idsName.begin(), ::toupper);
        

        
        IdsMap* map = IdsMapCache::get(idsName);
        if (map) {
            IdsMapEntry* entry = map->get(value);
            if (entry) {
                return getNormalizedSymbol(entry->getSymbol());
            }
            
            if (isBitwiseIds(idsRef)) {
                // Handle bitwise flags
                std::vector<std::string> flags;
                long remainingValue = value & 0xffffffffL;
                
                for (int bit = 0; bit < 32 && remainingValue > 0; bit++) {
                    long mask = 1L << bit;
                    if ((remainingValue & mask) == mask) {
                        IdsMapEntry* flagEntry = map->get(mask);
                        if (flagEntry) {
                            flags.push_back(getNormalizedSymbol(flagEntry->getSymbol()));
                        } else {
                            flags.push_back("0x" + std::to_string(mask));
                        }
                        remainingValue &= ~mask;
                    }
                }
                
                if (!flags.empty()) {
                    std::string result;
                    for (size_t i = 0; i < flags.size(); ++i) {
                        if (i > 0) result += " | ";
                        result += flags[i];
                    }
                    return result;
                }
            }
        }
    }
    
    // Handle special default values
    if (value == 0) {
        if (idsRef == "boolean") return "FALSE";
        if (idsRef == "instant") return "INSTANT";
    }
    
    // Return raw integer value if no specific IDS reference is provided
    return std::to_string(static_cast<int>(value));
}

std::string BcsDecompiler::getNormalizedSymbol(const std::string& symbol) {
    std::string result = symbol;
    // Trim whitespace
    result.erase(result.find_last_not_of(" \t\r\n") + 1);
    return result;
}

bool BcsDecompiler::isBitwiseIds(const std::string& idsName) {
    std::string upperName = idsName;
    std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);
    return BITWISE_IDS.count(upperName) > 0;
}

std::pair<std::string, std::vector<std::string>> BcsDecompiler::handleSpellTransformation(
    const std::string& functionName, 
    const std::vector<std::string>& params,
    long spellId) {
    
    if ((functionName == "SpellRES" || functionName == "HaveSpellRES") && 
        !params.empty() && params[0] == "\"\"") {
        
        std::string spellName = IdsMapCache::getIdsSymbol("SPELL.IDS", spellId);
        if (!spellName.empty() && spellName != std::to_string(spellId)) {
            std::string newName = (functionName == "SpellRES") ? "Spell" : "HaveSpell";
            std::vector<std::string> newParams;
            
            if (functionName == "SpellRES" && params.size() > 1) {
                // SpellRES(resource, target) -> Spell(target, spellname)
                newParams.push_back(params[1]);  // target
                newParams.push_back(getNormalizedSymbol(spellName));  // spell name
            } else {
                // HaveSpellRES(resource) -> HaveSpell(spellname)
                newParams.push_back(getNormalizedSymbol(spellName));  // spell name
            }
            
            return {newName, newParams};
        }
    }
    
    return {functionName, params};
}

std::vector<std::string> BcsDecompiler::handleConcatenatedStrings(
    const std::string& functionName,
    const std::vector<std::string>& params,
    const std::string& var1,
    const std::string& var2) {
    
    // Handle functions that store two strings concatenated in var1 (per Near Infinity ScriptInfo.functionConcatMap)
    auto shouldSplit = [&](const std::string& name) -> bool {
        return name == "Global" || name == "SetGlobal" || name == "SetGlobalTimer" || name == "GlobalTimer" ||
               name == "IncrementGlobal" || name == "GlobalGT" || name == "GlobalLT" || name == "GlobalsGT" ||
               name == "GlobalsLT" || name == "GlobalsEqual" || name == "GlobalMAX" || name == "GlobalMIN" ||
               name == "GlobalAND" || name == "GlobalOR" || name == "GlobalXOR" || name == "GlobalBAND" ||
               name == "GlobalBOR" || name == "GlobalShL" || name == "GlobalShR" || name == "SetGlobalRandom" ||
               name == "RealSetGlobalTimer" || name == "CheckAreaVariable" || name == "SetAreaVariable" ||
               name == "IncrementAreaVariable" ||
               // NI explicit concat flags for actions
               name == "CreateCreatureAtLocation" || name == "CreateItemGlobal" ||
               name == "MoveToSavedLocation" || name == "SetTokenGlobal";
    };

    if (!var1.empty() && var1.length() > 6 && shouldSplit(functionName)) {
        // Case-insensitive prefix checks, but preserve original casing in output
        std::string var1Upper = var1;
        std::transform(var1Upper.begin(), var1Upper.end(), var1Upper.begin(), ::toupper);
        
        if (var1Upper.substr(0, 6) == "GLOBAL") {
            // Replace first two string parameters with split values
            std::vector<std::string> result = params;
            if (result.size() >= 2) {
                result[0] = "\"" + var1.substr(6) + "\"";
                result[1] = "\"GLOBAL\"";
                // Remove any empty string parameter that follows
                if (result.size() > 2 && result[2] == "\"\"") {
                    result.erase(result.begin() + 2);
                }
            }
            return result;
        } else if (var1Upper.substr(0, 6) == "LOCALS") {
            std::vector<std::string> result = params;
            if (result.size() >= 2) {
                result[0] = "\"" + var1.substr(6) + "\"";
                result[1] = "\"LOCALS\"";
                if (result.size() > 2 && result[2] == "\"\"") {
                    result.erase(result.begin() + 2);
                }
            }
            return result;
        } else if (var1Upper.substr(0, 5) == "LOCAL") {
            // Handle LOCAL* prefixes (like LOCALC, LOCALD, etc.) with 6-character split
            std::vector<std::string> result = params;
            if (result.size() >= 2) {
                result[0] = "\"" + var1.substr(6) + "\"";
                result[1] = "\"" + var1.substr(0, 6) + "\"";
                if (result.size() > 2 && result[2] == "\"\"") {
                    result.erase(result.begin() + 2);
                }
            }
            return result;
        } else if (var1Upper.substr(0, 2) == "AR" && var1.length() >= 6) {
            std::vector<std::string> result = params;
            if (result.size() >= 2) {
                result[0] = "\"" + var1.substr(6) + "\"";
                result[1] = "\"" + var1.substr(0, 6) + "\"";
                if (result.size() > 2 && result[2] == "\"\"") {
                    result.erase(result.begin() + 2);
                }
            }
            return result;
        } else if (var1Upper.substr(0, 6) == "MYAREA") {
            std::vector<std::string> result = params;
            if (result.size() >= 2) {
                result[0] = "\"" + var1.substr(6) + "\"";
                result[1] = "\"MYAREA\"";
                if (result.size() > 2 && result[2] == "\"\"") {
                    result.erase(result.begin() + 2);
                }
            }
            return result;
        } else {
            // Near Infinity standard concatenation: first 6 chars = area, rest = variable name
            // This applies to any Global* function that doesn't match specific prefixes above
            std::vector<std::string> result = params;
            if (result.size() >= 2) {
                result[0] = "\"" + var1.substr(6) + "\"";  // variable name (remaining chars)
                result[1] = "\"" + var1.substr(0, 6) + "\"";  // area (first 6 chars)
                if (result.size() > 2 && result[2] == "\"\"") {
                    result.erase(result.begin() + 2);
                }
            }
            return result;
        }
    }
    
    return params; // Return unchanged if no concatenation detected
}

bool BcsDecompiler::isEmptyObject(const BCSObject& object) {
    return object.ea == 0 && object.general == 0 && object.race == 0 && 
           object.class_ == 0 && object.specific == 0 && object.gender == 0 &&
           object.alignment == 0 && object.name.empty() &&
           object.identifiers[0] == 0 && object.identifiers[1] == 0 &&
           object.identifiers[2] == 0 && object.identifiers[3] == 0 &&
           object.identifiers[4] == 0;
}

Signatures::Function* BcsDecompiler::getBestMatchingFunction(const std::vector<Signatures::Function*>& functions, const BCSAction& action) {
    if (functions.size() == 1) {
        return functions[0];
    }
    
    // Near Infinity-style signature matching logic
    // Score each function based on how well it matches the actual parameter data
    int bestScore = INT_MAX;
    int bestParamCount = INT_MAX;
    Signatures::Function* bestFunction = functions[0]; // fallback
    
    for (Signatures::Function* function : functions) {
        
        // Debug logging for signature matching
        if (functions.size() > 1) {
            Log(DEBUG, "BcsDecompiler", "Evaluating function: {} (opcode {})", function->getName(), action.opcode);
        }
        
        // Count expected parameter types
        int expectedStrings = 0;
        int expectedIntegers = 0;
        int expectedObjects = 0;
        int expectedPoints = 0;
        
        for (size_t i = 0; i < function->getNumParameters(); ++i) {
            const auto& param = function->getParameter(i);
            switch (param.getType()) {
                case Signatures::TYPE_STRING:
                    expectedStrings++;
                    break;
                case Signatures::TYPE_INTEGER:
                    expectedIntegers++;
                    break;
                case Signatures::TYPE_OBJECT:
                    expectedObjects++;
                    break;
                case Signatures::TYPE_POINT:
                    expectedPoints++;
                    break;
            }
        }
        
        // Near Infinity exact algorithm (BcsAction.java lines 155-189)
        int pi = expectedIntegers; // default: all integer slots unused
        int ps = expectedStrings;  // default: all string slots unused  
        int po = expectedObjects;  // default: all object slots unused
        int pp = expectedPoints;   // default: all point slots unused
        
        // Integer parameter evaluation (lines 156-161)
        for (int i = 2; i >= 0; i--) {
            long numericParam = (i == 0) ? action.param1 : (i == 1) ? action.param4 : action.param5;
            if (numericParam != 0) {
                pi = expectedIntegers - i - 1;
                break;
            }
        }
        
        // String parameter evaluation (lines 163-178)
        int sidx = 0;
        for (size_t i = 0; i < function->getNumParameters(); ++i) {
            const auto& param = function->getParameter(i);
            if (param.getType() == Signatures::TYPE_STRING) {
                sidx += 2; // Near Infinity: param.isCombinedString() ? 1 : 2
            }
        }
        
        bool a8Empty = action.var1.empty();
        bool a9Empty = action.var2.empty();
        
        if ((sidx < 2 && !a8Empty) || (sidx < 4 && !a9Empty)) {
            // don't choose if string arguments are left
            int psCount = expectedStrings;
            psCount--;
            ps = psCount - 1;
        } else {
            // Check string parameters using Near Infinity's getStringParam logic
            for (int i = 3; i >= 0; i--) {
                std::string stringParam;
                // Simple mapping: 0->var1, 1->var2, 2,3->empty for now
                if (i == 0 && !action.var1.empty()) stringParam = action.var1;
                else if (i == 1 && !action.var2.empty()) stringParam = action.var2;
                
                if (!stringParam.empty()) {
                    ps = expectedStrings - i - 1;
                    break;
                }
            }
        }
        
        // Object parameter evaluation (lines 180-185)
        for (int i = 2; i >= 1; i--) {
            if (!isEmptyObject(action.obj[i])) {
                po = expectedObjects - i;
                break;
            }
        }
        
        // Point parameter evaluation (lines 187-189)
        if (action.param2 != 0 || action.param3 != 0) {
            pp = expectedPoints - 1;
        }
        
        // Near Infinity final scoring: all individual scores must be >= 0 for match
        bool isMatch = pi >= 0 && ps >= 0 && po >= 0 && pp >= 0;
        int paramCount = expectedIntegers + expectedStrings + expectedObjects + expectedPoints;
        int score = pi + ps + po + pp; // lower is better in Near Infinity
        
        // Debug logging for signature matching
        if (functions.size() > 1) {
            Log(DEBUG, "BcsDecompiler", "  Expected: {} strings, {} ints, {} objs, {} points", 
                expectedStrings, expectedIntegers, expectedObjects, expectedPoints);
            Log(DEBUG, "BcsDecompiler", "  Available data: var1='{}', var2='{}', param1={}, param4={}, param5={}, obj[1]='{}', obj[2]='{}', point=({},{})", 
                action.var1, action.var2, action.param1, action.param4, action.param5, action.obj[1].name, action.obj[2].name, action.param2, action.param3);
            Log(DEBUG, "BcsDecompiler", "  Scores: pi={}, ps={}, po={}, pp={} | match={}, total={}, paramCount={}", 
                pi, ps, po, pp, isMatch, score, paramCount);
        }
        
        // Near Infinity selection criteria with specific overrides for opcode ambiguity
        bool shouldSelect = false;
        bool singleStringCase = !action.var1.empty() && action.var2.empty();
        
        if (functions.size() > 1 && (action.opcode == 227 || action.opcode == 250)) {
            // Special case: prefer functions with more parameters when there's 1 string (var1 non-empty, var2 empty)
            // This handles CreateCreatureObject vs CreateCreatureObjectEffect (227)
            // and CreateCreatureObjectCopy vs CreateCreatureObjectCopyEffect (250)
            std::string functionName = function->getName();
            
            if (singleStringCase) {
                // For single string case, prefer functions with more parameters (Object + integers)
                if ((action.opcode == 227 && functionName == "CreateCreatureObject") ||
                    (action.opcode == 250 && functionName == "CreateCreatureObjectCopy")) {
                    shouldSelect = true;
                } else if (bestFunction == nullptr || 
                          (bestFunction->getName().find("Effect") != std::string::npos)) {
                    shouldSelect = isMatch && (score < bestScore || 
                                   (score == bestScore && paramCount < bestParamCount));
                }
            } else {
                // For two string case, use standard logic (favors Effect functions)
                shouldSelect = isMatch && (score < bestScore || 
                               (score == bestScore && paramCount < bestParamCount));
            }
        } else {
            // Standard Near Infinity logic
            shouldSelect = isMatch && (score < bestScore || 
                           (score == bestScore && paramCount < bestParamCount));
        }
        
        if (shouldSelect) {
            bestScore = score;
            bestParamCount = paramCount;
            bestFunction = function;
        }
    }
    
    // Debug logging for final choice
    if (functions.size() > 1) {
        Log(DEBUG, "BcsDecompiler", "Selected function: {} with score {}", bestFunction->getName(), bestScore);
    }
    
    return bestFunction;
}

Signatures::Function* BcsDecompiler::getBestMatchingFunction(const std::vector<Signatures::Function*>& functions, const BCSTrigger& trigger) {
    if (functions.size() == 1) {
        return functions[0];
    }
    
    // Near Infinity-style signature matching logic for triggers
    int bestScoreVal = INT_MAX;   // lower is better; zero indicates a perfect match
    int bestScoreAvg = INT_MAX;   // lower is better; zero indicates a perfect match  
    int bestNumParams = INT_MAX;  // cosmetic: lower value indicates a shorter function signature
    Signatures::Function* bestFunction = functions[0]; // fallback
    
    for (Signatures::Function* function : functions) {
        // Count expected parameter types  
        int expectedStrings = 0;
        int expectedIntegers = 0;
        int expectedObjects = 0;
        int expectedPoints = 0;
        
        for (size_t i = 0; i < function->getNumParameters(); ++i) {
            const auto& param = function->getParameter(i);
            switch (param.getType()) {
                case Signatures::TYPE_STRING:
                    expectedStrings++;
                    break;
                case Signatures::TYPE_INTEGER:
                    expectedIntegers++;
                    break;
                case Signatures::TYPE_OBJECT:
                    expectedObjects++;
                    break;
                case Signatures::TYPE_POINT:
                    expectedPoints++;
                    break;
            }
        }
        
        // Count available parameters (Near Infinity approach)
        // Integers: count non-zero params
        int scoreInt = 0;
        if (trigger.param1 != 0) scoreInt++;
        if (trigger.param2 != 0) scoreInt++;
        if (trigger.param3 != 0) scoreInt++;
        
        // Strings: count non-empty strings
        int scoreStr = 0;
        if (!trigger.var1.empty()) scoreStr++;
        if (!trigger.var2.empty()) scoreStr++;
        
        // Objects: count non-empty objects (triggers have only 1 object)
        int scoreObj = (!isEmptyObject(trigger.object)) ? 1 : 0;
        
        // Points: check if point coordinates are non-zero (triggers use param2/param3 for point)
        int scorePt = (trigger.param2 != 0 || trigger.param3 != 0) ? 1 : 0;
        
        // Subtract expected counts (Near Infinity approach)
        scoreInt -= expectedIntegers;
        scoreStr -= expectedStrings;
        scoreObj -= expectedObjects;
        scorePt -= expectedPoints;
        
        // Calculate scores like Near Infinity
        int numParams = function->getNumParameters();
        int scoreVal = std::max({scoreInt, scoreStr, scoreObj, scorePt});
        int scoreAvg = std::max(0, scoreInt + scoreStr + scoreObj + scorePt);
        
        // Debug logging for signature matching
        if (functions.size() > 1) {
            Log(DEBUG, "BcsDecompiler", "Trigger Function: {} - Expected: {}i,{}s,{}o,{}p - Available: {}i,{}s,{}o,{}p - Scores: val={}, avg={}", 
                function->getName(), expectedIntegers, expectedStrings, expectedObjects, expectedPoints,
                scoreInt + expectedIntegers, scoreStr + expectedStrings, scoreObj + expectedObjects, scorePt + expectedPoints,
                scoreVal, scoreAvg);
        }
        
        // Near Infinity selection criteria
        if (scoreVal < bestScoreVal ||
            (scoreVal == bestScoreVal && (scoreAvg < bestScoreAvg ||
                                         (scoreAvg == bestScoreAvg && numParams < bestNumParams)))) {
            bestNumParams = numParams;
            bestScoreVal = scoreVal;
            bestScoreAvg = scoreAvg;
            bestFunction = function;
        }
    }
    
    // Debug logging for final choice
    if (functions.size() > 1) {
        Log(DEBUG, "BcsDecompiler", "Selected trigger function: {} with scoreVal={}, scoreAvg={}, numParams={}", 
            bestFunction->getName(), bestScoreVal, bestScoreAvg, bestNumParams);
    }
    
    return bestFunction;
}

} // namespace ProjectIE4k

