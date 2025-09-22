#include "BCSCompiler.h"
#include "BCS.h"
#include "Signatures.h"
#include "core/Logging/Logging.h"
#include "core/CFG.h"
#include <sstream>
#include "IdsMapCache.h"

namespace ProjectIE4k {

BCSCompiler::BCSCompiler() : initialized_(false) {
}

BCSCompiler::~BCSCompiler() {
}

bool BCSCompiler::initialize() {
    if (initialized_) return true;
    
    triggers_ = Signatures::getTriggers();
    actions_ = Signatures::getActions();
    
    if (!triggers_ || !actions_) {
        Log(ERROR, "BCS", "Failed to load BCS signatures");
        return false;
    }
    
    initialized_ = true;
    return true;
}

bool BCSCompiler::compileText(const std::string& textContent, std::vector<BCSBlock>& blocks) {
    if (!initialized_) {
        Log(ERROR, "BCS", "BCSCompiler not initialized");
        return false;
    }
    
    return parseScriptStructure(textContent, blocks);
}

bool BCSCompiler::parseScriptStructure(const std::string& textContent, std::vector<BCSBlock>& blocks) {
    std::istringstream iss(textContent);
    std::string line;
    BCSBlock currentBlock;
    bool inResponse = false;
    bool blockStarted = false;
    
    while (std::getline(iss, line)) {
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        if (line.empty() || line[0] == '#') continue;
        
        // Check for block boundaries
        if (line == "IF") {
            // Start a new block
            currentBlock = BCSBlock();
            inResponse = false;
            blockStarted = true;
            continue;
        }
        
        if (line == "THEN") {
            inResponse = true;
            continue;
        }
        
        if (line == "END" || line == "ENDIF") {
            if (blockStarted && (!currentBlock.triggers.empty() || !currentBlock.responses.empty())) {
                Log(DEBUG, "BCS", "Adding block with {} triggers and {} responses", 
                    currentBlock.triggers.size(), currentBlock.responses.size());
                blocks.push_back(currentBlock);
            }
            inResponse = false;
            blockStarted = false;
            continue;
        }
        
        // Parse triggers and actions
        if (!inResponse) {
            BCSTrigger trigger;
            if (compileTriggerFromText(line, trigger)) {
                Log(DEBUG, "BCS", "Added trigger: {}", line);
                currentBlock.triggers.push_back(trigger);
            }
        } else {
            // Check for response probability
            if (line.find("RESPONSE #") == 0) {
                BCSResponse response;
                if (parseResponseProbability(line, response.weight)) {
                    currentBlock.responses.push_back(response);
                }
                continue;
            }
            
            // Parse action
            BCSAction action;
            if (compileActionFromText(line, action)) {
                if (currentBlock.responses.empty()) {
                    // Default response with weight 100
                    currentBlock.responses.push_back(BCSResponse());
                }
                Log(DEBUG, "BCS", "Added action: {}", line);
                
                // Debug: Show object state before adding to response
                Log(DEBUG, "BCS", "Action object state before adding:");
                for (int i = 0; i < 3; i++) {
                    Log(DEBUG, "BCS", "  obj[{}]: ea={}, general={}, name='{}', identifiers[0]={}", 
                        i, action.obj[i].ea, action.obj[i].general, action.obj[i].name, action.obj[i].identifiers[0]);
                }
                
                // Handle ActionOverride specially: only generate the nested action
                if (action.hasNestedAction) {
                    Log(DEBUG, "BCS", "Processing ActionOverride: adding only the nested action with override target");
                    
                    // Debug: Show nested action object state  
                    Log(DEBUG, "BCS", "Nested action object state:");
                    for (int i = 0; i < 3; i++) {
                        Log(DEBUG, "BCS", "  nested obj[{}]: ea={}, general={}, name='{}', identifiers[0]={}", 
                            i, action.nestedAction->obj[i].ea, action.nestedAction->obj[i].general, 
                            action.nestedAction->obj[i].name, action.nestedAction->obj[i].identifiers[0]);
                    }
                    
                    // Add only the nested action with the override target
                    // ActionOverride itself is not written to bytecode - only the nested action
                    currentBlock.responses.back().actions.push_back(*action.nestedAction);
                    
                    Log(DEBUG, "BCS", "ActionOverride processing complete: added nested action (opcode {})",
                        action.nestedAction->opcode);
                } else {
                    // Standard action
                    currentBlock.responses.back().actions.push_back(action);
                    Log(DEBUG, "BCS", "Added standard action (opcode {}) with objects:", action.opcode);
                    for (int i = 0; i < 3; i++) {
                        Log(DEBUG, "BCS", "  final obj[{}]: ea={}, general={}, name='{}', identifiers[0]={}", 
                            i, action.obj[i].ea, action.obj[i].general, action.obj[i].name, action.obj[i].identifiers[0]);
                    }
                }
            }
        }
    }
    
    return true;
}

bool BCSCompiler::compileTriggerFromText(const std::string& line, BCSTrigger& trigger) {
    // Parse negation
    bool negated = false;
    std::string cleanLine = line;
    if (line.find("!") == 0) {
        negated = true;
        cleanLine = line.substr(1);
    }
    
    // Find function name and parameters
    size_t parenPos = cleanLine.find('(');
    if (parenPos == std::string::npos) {
        Log(ERROR, "BCS", "Invalid trigger format: {}", line);
        return false;
    }
    
    std::string funcName = cleanLine.substr(0, parenPos);
    std::string paramStr = cleanLine.substr(parenPos + 1);
    if (!paramStr.empty() && paramStr.back() == ')') {
        paramStr.pop_back();
    }
    
    // Look up function signature
    auto function = triggers_->getFunction(funcName);
    if (!function) {
        Log(ERROR, "BCS", "Unknown trigger function: {}", funcName);
        return false;
    }
    trigger.opcode = function->getId();
    Log(DEBUG, "BCS", "Trigger function '{}' mapped to opcode {}", funcName, trigger.opcode);
    trigger.flags = negated ? 1 : 0;
    
    return parseFunctionParameters(paramStr, function, trigger);
}

bool BCSCompiler::compileActionFromText(const std::string& line, BCSAction& action) {
    // Find function name and parameters
    size_t parenPos = line.find('(');
    if (parenPos == std::string::npos) {
        Log(ERROR, "BCS", "Invalid action format: {}", line);
        return false;
    }
    
    std::string funcName = line.substr(0, parenPos);
    std::string paramStr = line.substr(parenPos + 1);
    if (!paramStr.empty() && paramStr.back() == ')') {
        paramStr.pop_back();
    }
    
    Log(DEBUG, "BCS", "Parsing action line: '{}'", line);
    Log(DEBUG, "BCS", "Extracted function name: '{}', params: '{}'", funcName, paramStr);
    
    // Look up function signature
    auto function = actions_->getFunction(funcName);
    if (!function) {
        Log(ERROR, "BCS", "Unknown action function: '{}' (line: '{}')", funcName, line);
        Log(DEBUG, "BCS", "Actions signatures object valid: {}", actions_ != nullptr);
        action.opcode = 1; // Default action opcode when lookup fails
        return false;
    }

    Log(DEBUG, "BCS", "Found action function '{}' with {} parameters", funcName, function->getNumParameters());
    action.opcode = function->getId();
    Log(DEBUG, "BCS", "Action function '{}' mapped to opcode {}", funcName, action.opcode);

    // Following Near Infinity approach: Always set overrideTarget at objIndex 0 first
    // For regular actions, overrideTarget is null/empty, for ActionOverride it's the target
    BCSObject emptyObject; // null/empty object for regular actions
    action.obj[0] = emptyObject;  // Always start with empty object at position 0
    
    // Check if this is an ActionOverride function (contains Action parameter type 'A')
    bool isActionOverride = false;
    Log(DEBUG, "BCS", "Checking if {} is ActionOverride, has {} parameters", funcName, function->getNumParameters());
    for (size_t i = 0; i < function->getNumParameters(); ++i) {
        char paramType = function->getParameter(i).getType();
        Log(DEBUG, "BCS", "Parameter {}: type='{}'", i, paramType);
        if (paramType == 'A') {
            isActionOverride = true;
            Log(DEBUG, "BCS", "Found Action parameter type 'A' at position {}", i);
            break;
        }
    }

    if (isActionOverride) {
        Log(DEBUG, "BCS", "Detected ActionOverride function: {}", funcName);
        // For ActionOverride: parse parameters but handle nested action specially
        if (!parseFunctionParameters(paramStr, function, action, true)) {
            return false;
        }
        
        // ActionOverride requires special handling: the action should have been stored
        // in action.nestedAction by the parameter parsing
        if (!action.hasNestedAction) {
            Log(ERROR, "BCS", "ActionOverride function {} missing nested action", funcName);
            return false;
        }
        
        // Near Infinity approach: The ActionOverride target becomes the first object parameter,
        // but we need to preserve any existing objects from the nested action
        Log(DEBUG, "BCS", "Before Near Infinity ActionOverride fix:");
        Log(DEBUG, "BCS", "  Override target: ea={}, general={}", action.obj[0].ea, action.obj[0].general);
        Log(DEBUG, "BCS", "  Nested action obj[0]: ea={}, general={}, name={}", 
            action.nestedAction->obj[0].ea, action.nestedAction->obj[0].general, action.nestedAction->obj[0].name);
        Log(DEBUG, "BCS", "  Nested action obj[1]: ea={}, general={}, name={}", 
            action.nestedAction->obj[1].ea, action.nestedAction->obj[1].general, action.nestedAction->obj[1].name);
        Log(DEBUG, "BCS", "  Nested action obj[2]: ea={}, general={}, name={}", 
            action.nestedAction->obj[2].ea, action.nestedAction->obj[2].general, action.nestedAction->obj[2].name);
        
        // Save the original nested action objects before we modify them
        BCSObject originalObj0 = action.nestedAction->obj[0];
        BCSObject originalObj1 = action.nestedAction->obj[1];
        BCSObject originalObj2 = action.nestedAction->obj[2];
        
        // Place ActionOverride target in obj[0] (written first by BCS writer)
        Log(DEBUG, "BCS", "ActionOverride: copying target object '{}' to nested obj[0]", action.obj[0].name);
        action.nestedAction->obj[0] = action.obj[0];
        Log(DEBUG, "BCS", "ActionOverride: after copy, nested obj[0] name='{}'", action.nestedAction->obj[0].name);
        
        // If the nested action had objects, shift them to remaining positions
        // Check if original objects had meaningful data (non-empty names or non-zero values)
        bool hasOriginalObj0 = !originalObj0.name.empty() || originalObj0.ea != 0 || originalObj0.general != 0;
        bool hasOriginalObj1 = !originalObj1.name.empty() || originalObj1.ea != 0 || originalObj1.general != 0;
        bool hasOriginalObj2 = !originalObj2.name.empty() || originalObj2.ea != 0 || originalObj2.general != 0;

        // Place original objects in remaining slots: obj[1] (written second) and obj[2] (written third)
        if (hasOriginalObj0) {
            action.nestedAction->obj[1] = originalObj0;  // Original first object goes to second position
        }
        if (hasOriginalObj1 && !hasOriginalObj0) {
            action.nestedAction->obj[1] = originalObj1;  // If no obj[0], use obj[1] in second position
        } else if (hasOriginalObj1) {
            action.nestedAction->obj[2] = originalObj1;  // Original second object goes to third position
        }
        if (hasOriginalObj2) {
            action.nestedAction->obj[2] = originalObj2;  // Original third object goes to third position
        }
        
        Log(DEBUG, "BCS", "After Near Infinity ActionOverride fix:");
        Log(DEBUG, "BCS", "  Nested action obj[0]: ea={}, general={}, name={} (override target - written first)",
            action.nestedAction->obj[0].ea, action.nestedAction->obj[0].general, action.nestedAction->obj[0].name);
        Log(DEBUG, "BCS", "  Nested action obj[1]: ea={}, general={}, name={} (preserved original - written second)",
            action.nestedAction->obj[1].ea, action.nestedAction->obj[1].general, action.nestedAction->obj[1].name);
        Log(DEBUG, "BCS", "  Nested action obj[2]: ea={}, general={}, name={} (preserved original - written third)",
            action.nestedAction->obj[2].ea, action.nestedAction->obj[2].general, action.nestedAction->obj[2].name);
        
        return true;
    }

    // Standard action compilation - use Near Infinity approach with objIndex starting at 1
    return parseFunctionParameters(paramStr, function, action, true);
}

bool BCSCompiler::parseFunctionParameters(const std::string& line, void* function, BCSTrigger& trigger) {
    auto func = static_cast<Signatures::Function*>(function);
    auto params = splitParameters(line);
    
    std::vector<std::string> stringParams;
    std::vector<int> stringPositions; // absolute positions within signature
    int numericIndex = 0;
    int objectIndex = 0;
    
    // Use public methods to access parameters
    for (size_t i = 0; i < func->getNumParameters() && i < params.size(); ++i) {
        const auto& param = func->getParameter(i);
        const auto& value = params[i];
        
        switch (param.getType()) {
            case 'I': // Integer
                {
                    // Pass parameter context for IDS resolution
                    int intValue = parseIntegerFromText(value, (void*)&param);

                    // Check if this is a special parameter that should go into object identifiers
                    std::string paramName = param.getName();
                    std::string idsRef = param.getIdsRef();
                    Log(DEBUG, "BCS", "Processing trigger parameter: name='{}', idsRef='{}', type={}, value={}", paramName, idsRef, param.getType(), intValue);

                    if (idsRef.find('*') != std::string::npos) {
                        // This is a special parameter that goes into object identifiers
                        Log(DEBUG, "BCS", "Special parameter '{}' (idsRef='{}') with value {} goes to object identifiers", paramName, idsRef, intValue);
                        // For now, assume it goes to identifiers[0]
                        trigger.object.identifiers[0] = intValue;
                    } else {
                        setNumericParam(trigger, numericIndex++, intValue);
                    }
                }
                break;
            case 'P': // Point
                {
                    int x, y;
                    if (parsePointFromText(value, x, y)) {
                        setNumericParam(trigger, numericIndex++, x);
                        setNumericParam(trigger, numericIndex++, y);
                    }
                }
                break;
            case 'O': // Object
                {
                    BCSObject obj;
                    if (parseObjectFromText(value, obj)) {
                        setObjectParam(trigger, objectIndex++, obj);
                    }
                }
                break;
            case 'S': // String
                stringParams.push_back(parseStringFromText(value));
                stringPositions.push_back(static_cast<int>(i));
                break;
        }
    }
    
    // Handle string concatenation
    if (!stringParams.empty()) {
        // Use function context and absolute parameter positions
        if (trigger.opcode >= 0) {
            setStringParams(trigger, stringParams, stringPositions, *func);
        } else {
            setStringParams(trigger, stringParams);
        }
    }
    
    return true;
}

bool BCSCompiler::parseFunctionParameters(const std::string& line, void* function, BCSAction& action, bool nearInfinityMode) {
    auto func = static_cast<Signatures::Function*>(function);
    auto params = splitParameters(line);
    
    Log(DEBUG, "BCS", "parseFunctionParameters: line='{}', params.size()={}", line, params.size());
    for (size_t i = 0; i < params.size(); ++i) {
        Log(DEBUG, "BCS", "  param[{}]='{}'", i, params[i]);
    }
    
    // Debug: Print what splitParameters returned
    Log(DEBUG, "BCS", "splitParameters returned {} parameters:", params.size());
    for (size_t i = 0; i < params.size(); ++i) {
        Log(DEBUG, "BCS", "  params[{}] = '{}'", i, params[i]);
    }
    
    std::vector<std::string> stringParams;
    std::vector<int> stringPositions; // absolute positions within signature
    int numericIndex = 0;
    int objectIndex = 0;
    
    if (nearInfinityMode) {
        // Near Infinity's generateAC method: overrideTarget always goes to objIndex 0
        // For ActionOverride, the target goes to obj[0], so don't set it to empty
        // For other functions, set obj[0] to empty and start objects at index 1
        bool isActionOverrideFunc = false;
        if (func && func->getNumParameters() > 0) {
            // Check if this function has an Action parameter (indicating ActionOverride)
            for (size_t i = 0; i < func->getNumParameters(); ++i) {
                if (func->getParameter(i).getType() == 'A') {
                    isActionOverrideFunc = true;
                    break;
                }
            }
        }

        if (!isActionOverrideFunc) {
            // Check if this action has object parameters in its signature
            bool hasObjectParams = false;
            Log(DEBUG, "BCS", "Checking parameters for action '{}':", func->getName());
            for (size_t i = 0; i < func->getNumParameters(); ++i) {
                char paramType = func->getParameter(i).getType();
                std::string paramName = func->getParameter(i).getName();
                Log(DEBUG, "BCS", "  Parameter {}: type='{}', name='{}'", i, paramType, paramName);
                if (paramType == 'O') {
                    hasObjectParams = true;
                }
            }
            Log(DEBUG, "BCS", "Action '{}' has {} parameters, hasObjectParams={}", func->getName(), func->getNumParameters(), hasObjectParams);

            BCSObject emptyObj;

            // Near Infinity handles all actions uniformly - no special object naming
            action.obj[0] = emptyObj;
            action.obj[1] = emptyObj;
            action.obj[2] = emptyObj;
            Log(DEBUG, "BCS", "Near Infinity mode: All action objects initialized as empty", func->getName());

            objectIndex = 1; // Start objects at index 1
        } else {
            Log(DEBUG, "BCS", "Near Infinity mode: ActionOverride function detected, obj[0] will contain target");
        }
    }
    
    // Use public methods to access parameters
    Log(DEBUG, "BCS", "Action function has {} parameters, {} provided", func->getNumParameters(), params.size());

    // First pass: process all provided parameters
    for (size_t i = 0; i < func->getNumParameters() && i < params.size(); ++i) {
        const auto& param = func->getParameter(i);
        const auto& value = params[i];
        Log(DEBUG, "BCS", "Processing action param[{}]: type='{}', value='{}'", i, param.getType(), value);

        // Debug: Print the raw parameter value
        Log(DEBUG, "BCS", "Raw parameter value: '{}'", value);

        switch (param.getType()) {
            case 'I': // Integer
                {
                    // Pass parameter context for IDS resolution
                    int intValue = parseIntegerFromText(value, (void*)&param);
                    Log(DEBUG, "BCS", "Integer param[{}]: '{}' -> {}", i, value, intValue);
                    setNumericParam(action, numericIndex++, intValue);
                }
                break;
            case 'P': // Point
                {
                    int x, y;
                    if (parsePointFromText(value, x, y)) {
                        // Points go to param2 (a5.x) and param3 (a5.y) in Near Infinity
                        action.param2 = x; // a5.x
                        action.param3 = y; // a5.y
                        Log(DEBUG, "BCS", "Point param: x={}, y={} -> param2={}, param3={}", x, y, action.param2, action.param3);
                    }
                }
                break;
            case 'O': // Object
                {
                    BCSObject obj;
                    if (parseObjectFromText(value, obj)) {
                        // Note: Identifier reversal is handled internally by parseObjectFromText for functional objects
                        // No additional reversal needed for simple objects
                        setObjectParam(action, objectIndex, obj);
                        Log(DEBUG, "BCS", "Parsed object[{}]: ea={}, general={}, name='{}', identifiers[0]={} from value '{}'",
                            objectIndex, obj.ea, obj.general, obj.name, obj.identifiers[0], value);
                        objectIndex++;
                    } else {
                        Log(ERROR, "BCS", "Failed to parse object from '{}'", value);
                    }
                }
                break;
            case 'S': // String
                stringParams.push_back(parseStringFromText(value));
                stringPositions.push_back(static_cast<int>(i));
                break;
            case 'A': // Action
                {
                    // For ActionOverride: compile nested action and store it for special handling
                    BCSAction nestedAction;
                    if (compileActionFromText(value, nestedAction)) {
                        Log(DEBUG, "BCS", "Compiled nested action: opcode={}", nestedAction.opcode);
                        
                        // Store the nested action for ActionOverride processing
                        action.nestedAction = std::make_unique<BCSAction>(nestedAction);
                        action.hasNestedAction = true;
                    } else {
                        Log(ERROR, "BCS", "Failed to compile nested action: {}", value);
                    }
                }
                break;
        }
    }

    // Near Infinity handles missing object parameters uniformly - no special default object creation

    // Handle string concatenation
    if (!stringParams.empty()) {
        // Use function context and absolute parameter positions
        if (action.opcode >= 0) {
            setStringParams(action, stringParams, stringPositions, *func);
        } else {
            setStringParams(action, stringParams);
        }
    }
    
    return true;
}

// Near Infinity-style parameter assignment methods
void BCSCompiler::setNumericParam(BCSTrigger& trigger, int index, int value) {
    switch (index) {
        case 0: trigger.param1 = value; break;
        case 1: trigger.param2 = value; break;
        case 2: trigger.param3 = value; break;
    }
}

void BCSCompiler::setNumericParam(BCSAction& action, int index, int value) {
    Log(DEBUG, "BCS", "setNumericParam: action index={}, value={}", index, value);
    // Match Near Infinity's BcsAction structure: a4, a6, a7
    switch (index) {
        case 0: action.param1 = value; Log(DEBUG, "BCS", "Set action.param1 (a4) = {}", value); break;
        // Note: param2/param3 are reserved for point (a5.x/a5.y). Next integers go to a6 and a7.
        case 1: action.param4 = value; Log(DEBUG, "BCS", "Set action.param4 (a6) = {}", value); break;
        case 2: action.param5 = value; Log(DEBUG, "BCS", "Set action.param5 (a7) = {}", value); break;
        default: Log(ERROR, "BCS", "Invalid numeric parameter index: {}", index); break;
    }
}

void BCSCompiler::setObjectParam(BCSTrigger& trigger, int index, const BCSObject& object) {
    Log(DEBUG, "BCS", "Setting trigger object[{}]: ea={}, general={}", index, object.ea, object.general);
    trigger.object = object;
    Log(DEBUG, "BCS", "Trigger object after setting: ea={}, general={}", trigger.object.ea, trigger.object.general);
}

void BCSCompiler::setObjectParam(BCSAction& action, int index, const BCSObject& object) {
    Log(DEBUG, "BCS", "setObjectParam: storing object with name='{}' at index {}", object.name, index);
    if (index < 3) {
        // Ensure object name is preserved during assignment
        std::string originalName = object.name;
        action.obj[index] = object;
        // Double-check that name was preserved
        if (action.obj[index].name != originalName) {
            Log(DEBUG, "BCS", "Object name was lost during assignment! Restoring '{}' to action.obj[{}]", originalName, index);
            action.obj[index].name = originalName;
        }
        Log(DEBUG, "BCS", "setObjectParam: stored object name='{}' in action.obj[{}]", action.obj[index].name, index);
    }
}

void BCSCompiler::setStringParams(BCSTrigger& trigger, const std::vector<std::string>& strings) {
    if (strings.size() >= 1) trigger.var1 = strings[0];
    if (strings.size() >= 2) trigger.var2 = strings[1];
    
    // Handle concatenation based on function ID
    if (trigger.opcode >= 0) {
        auto functions = triggers_->getFunction(trigger.opcode);
        if (!functions.empty()) {
            auto func = functions[0];
            if (Signatures::isCombinedString(func->getId(), 0, func->getNumParameters()) && strings.size() >= 2) {
                trigger.var1 = strings[1] + strings[0]; // Near Infinity order: second + first
                trigger.var2.clear();
            }
        }
    }
}

void BCSCompiler::setStringParams(BCSAction& action, const std::vector<std::string>& strings) {
    if (strings.size() >= 1) action.var1 = strings[0];
    if (strings.size() >= 2) action.var2 = strings[1];
    
    // Handle concatenation based on function ID
    if (action.opcode >= 0) {
        auto functions = actions_->getFunction(action.opcode);
        if (!functions.empty()) {
            auto func = functions[0];
            if (Signatures::isCombinedString(func->getId(), 0, func->getNumParameters()) && strings.size() >= 2) {
                action.var1 = strings[1] + strings[0]; // Near Infinity order: second + first
                action.var2.clear();
            }
        }
    }
}

// New overloads that use absolute parameter positions and function context
void BCSCompiler::setStringParams(BCSTrigger& trigger, const std::vector<std::string>& strings,
                                  const std::vector<int>& positions, const Signatures::Function& func) {
    if (strings.size() >= 1) trigger.var1 = strings[0];
    if (strings.size() >= 2) trigger.var2 = strings[1];
    
    if (strings.size() >= 2 && positions.size() >= 2) {
        int firstPos = positions[0];
        bool combined = Signatures::isCombinedString(func.getId(), firstPos, static_cast<int>(func.getNumParameters()));
        bool colonSep = Signatures::isColonSeparatedString(func.getId(), firstPos, static_cast<int>(func.getNumParameters()));
        if (combined || colonSep) {
            trigger.var1 = strings[1] + (colonSep ? ":" : "") + strings[0];
            trigger.var2.clear();
        }
    }
}

void BCSCompiler::setStringParams(BCSAction& action, const std::vector<std::string>& strings,
                                  const std::vector<int>& positions, const Signatures::Function& func) {
    if (strings.size() >= 1) action.var1 = strings[0];
    if (strings.size() >= 2) action.var2 = strings[1];

    if (strings.size() >= 2 && positions.size() >= 2) {
        int firstPos = positions[0];
        bool combined = Signatures::isCombinedString(func.getId(), firstPos, static_cast<int>(func.getNumParameters()));
        bool colonSep = Signatures::isColonSeparatedString(func.getId(), firstPos, static_cast<int>(func.getNumParameters()));
        if (combined || colonSep) {
            action.var1 = strings[1] + (colonSep ? ":" : "") + strings[0];
            // If there's a third string parameter, move it to var2, otherwise clear it
            if (strings.size() >= 3) {
                action.var2 = strings[2];
            } else {
                action.var2.clear();
            }
        }
    }
}

// Target/Identifier processing (Near Infinity style)
void BCSCompiler::setTargetValue(BCSObject& object, int index, int value) {
    switch (index) {
        case 0: object.ea = value; break;
        case 1: object.faction = value; break;
        case 2: object.team = value; break;
        case 3: object.general = value; break;
        case 4: object.race = value; break;
        case 5: object.class_ = value; break;
        case 6: object.specific = value; break;
        case 7: object.gender = value; break;
        case 8: object.alignment = value; break;
    }
}

void BCSCompiler::setIdentifierValue(BCSObject& object, int index, int value) {
    Log(DEBUG, "BCS", "setIdentifierValue called: index={}, value={}", index, value);
    if (index < 5) {
        object.identifiers[index] = value;
        Log(DEBUG, "BCS", "Set object.identifiers[{}] = {}", index, object.identifiers[index]);
    } else {
        Log(DEBUG, "BCS", "Index {} out of range (max 4)", index);
    }
}

// Helper parsing methods
std::vector<std::string> BCSCompiler::splitParameters(const std::string& paramStr) {
    std::vector<std::string> params;
    std::string current;
    int parenDepth = 0;
    bool inQuotes = false;
    char prev = '\0';
    
    for (char c : paramStr) {
        if (!inQuotes && c == '"') {
            inQuotes = true;
            current += c;
        } else if (inQuotes && c == '"' && prev != '\\') {
            inQuotes = false;
            current += c;
        } else if (!inQuotes && c == '(') {
            parenDepth++;
            current += c;
        } else if (!inQuotes && c == ')') {
            parenDepth--;
            current += c;
        } else if (c == ',' && !inQuotes && parenDepth == 0) {
            // Trim whitespace
            current.erase(0, current.find_first_not_of(" \t"));
            current.erase(current.find_last_not_of(" \t") + 1);
            if (!current.empty()) {
                params.push_back(current);
            }
            current.clear();
        } else {
            current += c;
        }
        prev = c;
    }
    
    // Add final parameter
    current.erase(0, current.find_first_not_of(" \t"));
    current.erase(current.find_last_not_of(" \t") + 1);
    if (!current.empty()) {
        params.push_back(current);
    }
    
    return params;
}

bool BCSCompiler::parseObjectFromText(const std::string& text, BCSObject& object) {
    object = BCSObject();
    
    // Check if text is empty
    if (text.empty()) {
        Log(WARNING, "BCS", "parseObjectFromText called with empty string");
        return false;
    }
    
    // Handle special object identifiers
    if (text == "Myself") {
        setIdentifierValue(object, 0, 1); // OBJECT.IDS: 1 = Myself
        return true;
    }
    
    // Handle functional objects like NearestEnemyOf(Myself)
    size_t parenPos = text.find('(');
    if (parenPos != std::string::npos) {
        std::string funcName = text.substr(0, parenPos);
        std::string argText = text.substr(parenPos + 1);
        if (!argText.empty() && argText.back() == ')') {
            argText.pop_back();
        }



        // Parse inner argument
        BCSObject innerObj;
        if (parseObjectFromText(argText, innerObj)) {
            Log(DEBUG, "BCS", "Functional object '{}' parsed inner '{}': ea={}, general={}", funcName, argText, innerObj.ea, innerObj.general);
            // First, copy all target values from inner object to preserve EA, etc.
            object.ea = innerObj.ea;
            object.faction = innerObj.faction;
            object.team = innerObj.team;
            object.general = innerObj.general;
            object.race = innerObj.race;
            object.class_ = innerObj.class_;
            object.specific = innerObj.specific;
            object.gender = innerObj.gender;
            object.alignment = innerObj.alignment;
            object.name = innerObj.name;
            Log(DEBUG, "BCS", "After copying target values: ea={}, general={}", object.ea, object.general);
            
            // Simulate Near Infinity's numbers2 stack approach
            std::vector<int> numbers2;
            
            // Look up function ID from OBJECT.IDS dynamically
            int funcId = static_cast<int>(IdsMapCache::getIdsValue("OBJECT", funcName));
            
            // To match original game compiler output: [innerIds..., funcId]
            // Push inner identifiers first, then function id
            for (int i = 0; i < 5; ++i) {
                if (innerObj.identifiers[i] != 0) {
                    numbers2.push_back(innerObj.identifiers[i]);
                }
            }
            // Push function id even if zero
            numbers2.push_back(funcId);
            
            // Assign in NORMAL order to match original game compiler:
            // Original game does: object.setIdentifierValue(i, numbers2[i])
            size_t count = std::min<size_t>(numbers2.size(), 5);
            for (size_t i = 0; i < count; ++i) {
                setIdentifierValue(object, i, numbers2[i]);
            }
            
            Log(DEBUG, "BCS", "Final functional object '{}': ea={}, general={}", funcName, object.ea, object.general);
            return true;
        }
    }
    
    // Handle bracketed objects: either OBJECT.IDS symbol (e.g., [PC]) or IDS target list
    if (text.front() == '[' && text.back() == ']') {
        std::string objBody = text.substr(1, text.length() - 2);
        
        // First, check if this is an OBJECT.IDS symbol (NearInfinity does this test first)
        {
            long objId = IdsMapCache::getIdsValue("OBJECT", objBody);
            if (objId >= 0) {
                setIdentifierValue(object, 0, static_cast<int>(objId));
                return true;
            }
        }
        
        // Otherwise, treat as IDS target list: EA.GENERAL.RACE.CLASS.SPECIFIC.GENDER.ALIGN
        // Split by '.' and resolve each piece with IDS lookup and numeric fallback
        static const char* targetIds[7] = {"EA", "GENERAL", "RACE", "CLASS", "SPECIFIC", "GENDER", "ALIGN"};
        std::vector<std::string> parts;
        {
            std::string cur;
            for (char c : objBody) {
                if (c == '.') { parts.push_back(cur); cur.clear(); }
                else { cur += c; }
            }
            parts.push_back(cur);
        }
        Log(DEBUG, "BCS", "Bracketed object '{}', parts count = {}", objBody, parts.size());
        
        // Determine positional mapping based on number of parts to match NI semantics
        // count: 1->[EA], 2->[EA, GENERAL], 3->[EA, GENERAL, CLASS], 4->[EA, GENERAL, RACE, CLASS],
        // 5-> +SPECIFIC, 6-> +GENDER, 7-> +ALIGN
        size_t count = std::min<size_t>(parts.size(), 7);
        int fieldIndexForPos[7] = {0};
        if (count >= 1) fieldIndexForPos[0] = 0;            // EA -> ea(0)
        if (count == 2) {
            fieldIndexForPos[1] = 3;                        // GENERAL -> general(3)
        } else if (count >= 3) {
            fieldIndexForPos[1] = 3;                        // GENERAL -> general(3)
            if (count >= 3) fieldIndexForPos[2] = 4;        // RACE -> race(4)
            if (count >= 4) fieldIndexForPos[3] = 5;        // CLASS -> class_(5)
            if (count >= 5) fieldIndexForPos[4] = 6;        // SPECIFIC -> specific(6)
            if (count >= 6) fieldIndexForPos[5] = 7;        // GENDER -> gender(7)
            if (count >= 7) fieldIndexForPos[6] = 8;        // ALIGN -> alignment(8)
        }

        for (size_t i = 0; i < count; ++i) {
            const std::string& tok = parts[i];
            if (tok.empty()) continue;
            // Determine which IDS namespace to use for this position
            int targetFieldIndex = fieldIndexForPos[i];
            const char* idsName = "";
            switch (targetFieldIndex) {
                case 0: idsName = targetIds[0]; break; // EA
                case 3: idsName = targetIds[1]; break; // GENERAL
                case 4: idsName = targetIds[2]; break; // RACE
                case 5: idsName = targetIds[3]; break; // CLASS
                case 6: idsName = targetIds[4]; break; // SPECIFIC
                case 7: idsName = targetIds[5]; break; // GENDER
                case 8: idsName = targetIds[6]; break; // ALIGN
                default: idsName = targetIds[0]; break; // safe default
            }

            long v = IdsMapCache::getIdsValue(idsName, tok);
            if (v < 0) {
                // numeric fallback (dec/hex/bin/oct via base 0)
                try {
                    v = std::stol(tok, nullptr, 0);
                } catch (...) {
                    v = -1;
                }
            }
            Log(DEBUG, "BCS", "  part[{}]='{}' -> ids='{}' value={} -> fieldIndex={}", i, tok, idsName, v, targetFieldIndex);
            if (v >= 0) {
                setTargetValue(object, targetFieldIndex, static_cast<int>(v));
            }
        }
        return true;
    }
    
    // Handle simple identifiers
    {
        long v = IdsMapCache::getIdsValue("OBJECT", text);
        if (v >= 0) {
            Log(DEBUG, "BCS", "Simple identifier '{}' resolved to ID {}", text, v);
            setIdentifierValue(object, 0, static_cast<int>(v));
            Log(DEBUG, "BCS", "After setIdentifierValue: object.identifiers[0]={}, [1]={}, [2]={}, [3]={}, [4]={}",
                object.identifiers[0], object.identifiers[1], object.identifiers[2],
                object.identifiers[3], object.identifiers[4]);
            return true;
        }
    }
    
    // Handle simple quoted strings like "Door01" as named objects
    if (!text.empty() && text.front() == '"' && text.back() == '"') {
        std::string name = text.substr(1, text.length() - 2);
        object.name = name;
        Log(DEBUG, "BCS", "Parsed quoted string object: name='{}', object.name='{}'", name, object.name);
        return true;
    }
    
    return false;
}

int BCSCompiler::parseIntegerFromText(const std::string& text, void* param) {
    Log(DEBUG, "BCS", "parseIntegerFromText called with: '{}'", text);
    
    // Remove quotes if present
    std::string cleanText = text;
    if (!cleanText.empty() && cleanText.front() == '"' && cleanText.back() == '"') {
        cleanText = cleanText.substr(1, cleanText.length() - 2);
    }
    
    // Try direct integer parsing
    try {
        int result = std::stoi(cleanText);
        // Do not adjust large values; Near Infinity reference keeps them as-is
        Log(DEBUG, "BCS", "parseIntegerFromText: '{}' -> {}", text, result);
        return result;
    } catch (...) {
        // Try IDS lookup using parameter context
        const Signatures::Parameter* pinfo = static_cast<const Signatures::Parameter*>(param);
        if (pinfo) {
            std::string ids = pinfo->getIdsRef();
            if (!ids.empty()) {
                long v = IdsMapCache::getIdsValue(ids, cleanText);
                Log(DEBUG, "BCS", "IDS lookup: {}('{}') -> {}", ids, cleanText, v);
                return static_cast<int>(v);
            }
        }
        Log(ERROR, "BCS", "parseIntegerFromText failed with no IDS mapping: '{}'", text);
        return -1;
    }
}

std::string BCSCompiler::parseStringFromText(const std::string& text) {
    if (text.front() == '"' && text.back() == '"') {
        return text.substr(1, text.length() - 2);
    }
    return text;
}

bool BCSCompiler::parsePointFromText(const std::string& text, int& x, int& y) {
    if (text.front() == '[' && text.back() == ']') {
        std::string pointStr = text.substr(1, text.length() - 2);
        size_t dotPos = pointStr.find('.');
        if (dotPos != std::string::npos) {
            try {
                x = std::stoi(pointStr.substr(0, dotPos));
                y = std::stoi(pointStr.substr(dotPos + 1));
                return true;
            } catch (...) {
                return false;
            }
        }
    }
    return false;
}

// Bytecode generation methods (Near Infinity style)
std::string BCSCompiler::generateTriggerText(const BCSTrigger& trigger) {
    std::string text = "TR\n";

    // Write opcode and parameters (Near Infinity format)
    text += std::to_string(trigger.opcode) + " ";
    text += std::to_string(trigger.param1) + " ";
    text += std::to_string(trigger.flags) + " ";
    text += std::to_string(trigger.param2) + " ";
    text += std::to_string(trigger.param3) + " ";

    // Write strings
    if (!trigger.var1.empty()) {
        text += "\"" + trigger.var1 + "\" ";
    } else {
        text += "\"\" ";
    }
    if (!trigger.var2.empty()) {
        text += "\"" + trigger.var2 + "\" ";
    } else {
        text += "\"\" ";
    }

    // Write object
    text += generateObjectText(trigger.object);
    text += "TR\n";

    return text;
}

std::string BCSCompiler::generateActionText(const BCSAction& action) {
    std::string text = "AC\n";

    // Near Infinity parse code: "X123456789"
    // X = action ID, 1-3 = objects, 4 = param1, 5 = point(param2,param3), 6 = param4, 7 = param5, 8-9 = strings

    // Write opcode (X)
    text += std::to_string(action.opcode) + " ";

    // Write objects (1, 2, 3)
    for (int i = 0; i < 3; ++i) {
        text += generateObjectText(action.obj[i]);
    }

    // Write parameters in Near Infinity order: a4, a5.x, a5.y, a6, a7 (4, 5, 6, 7)
    text += std::to_string(action.param1) + " ";        // a4 (param1)
    text += std::to_string(action.param2) + " ";        // a5.x (param2)
    text += std::to_string(action.param3) + " ";        // a5.y (param3)
    text += std::to_string(action.param4) + " ";        // a6 (param4)
    text += std::to_string(action.param5) + " ";        // a7 (param5)

    // Write strings (8, 9)
    if (!action.var1.empty()) {
        text += "\"" + action.var1 + "\" ";
    } else {
        text += "\"\" ";
    }
    if (!action.var2.empty()) {
        text += "\"" + action.var2 + "\" ";
    } else {
        text += "\"\" ";
    }

    text += "AC\n";
    return text;
}

std::string BCSCompiler::generateObjectText(const BCSObject& object) {
    std::string text = "OB\n";

    // Near Infinity BG parse code: "T0:T1:T2:T3:T4:T5:T6:T7:T8:I0:I1:I2:I3:I4:S0"
    // T0=ea, T1=faction, T2=team, T3=general, T4=race, T5=class, T6=specific, T7=gender, T8=alignment
    // I0-I4=identifiers, S0=name

    // Write target fields in Near Infinity format
    text += std::to_string(object.ea) + " ";        // T0

    // BG games don't include faction/team, PST does
    if (PIE4K_CFG.GameType == "pst") {
        text += std::to_string(object.faction) + " ";   // T1
        text += std::to_string(object.team) + " ";      // T2
    }

    text += std::to_string(object.general) + " ";   // T3
    text += std::to_string(object.race) + " ";      // T4
    text += std::to_string(object.class_) + " ";    // T5
    text += std::to_string(object.specific) + " ";  // T6
    text += std::to_string(object.gender) + " ";    // T7
    text += std::to_string(object.alignment) + " "; // T8

    // Write identifiers (I0-I4)
    for (int i = 0; i < 5; ++i) {
        if (object.identifiers[i] != 0) {
            Log(DEBUG, "BCS", "Writing identifier[{}] = {}", i, object.identifiers[i]);
        }
        text += std::to_string(object.identifiers[i]) + " ";
    }

    // Write name (S0) - no area coordinates for BG
    Log(DEBUG, "BCS", "Writing object name: '{}' (empty={})", object.name, object.name.empty());
    if (!object.name.empty()) {
        text += "\"" + object.name + "\" ";
    } else {
        text += "\"\" ";
    }

    text += "OB\n";
    return text;
}

int BCSCompiler::getParseCode(const std::string& paramType) {
    // Near Infinity parse codes
    if (paramType == "I") return 1; // Integer
    if (paramType == "P") return 2; // Point
    if (paramType == "O") return 3; // Object
    if (paramType == "S") return 4; // String
    return 0;
}

bool BCSCompiler::parseNegation(const std::string& text, bool& negated) {
    negated = (text.find("!") == 0);
    return true;
}

bool BCSCompiler::parseOverride(const std::string& text, int& overrideType) {
    // TODO: Implement override parsing
    overrideType = 0;
    return true;
}

bool BCSCompiler::parseResponseProbability(const std::string& text, int& weight) {
    if (text.find("RESPONSE #") == 0) {
        try {
            weight = std::stoi(text.substr(10));
            return true;
        } catch (...) {
            return false;
        }
    }
    weight = 100; // Default weight
    return true;
}

} // namespace ProjectIE4k
