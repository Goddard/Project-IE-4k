#include "Signatures.h"
#include "IdsMapCache.h"
#include "core/Logging/Logging.h"
#include <algorithm>
#include <sstream>
#include <regex>
#include <mutex>

namespace ProjectIE4k {

// Static member definitions
const std::string Signatures::Parameter::RESTYPE_SCRIPT = "script";
const std::string Signatures::Parameter::RESTYPE_SPELL_LIST = "spelllist";
const std::string Signatures::Function::TRIGGER_OVERRIDE_NAME = "TriggerOverride";

std::map<std::string, std::shared_ptr<Signatures>> Signatures::instances;
std::map<int, int> Signatures::functionConcatMap;
static std::mutex instancesMutex;

// Parameter methods
void Signatures::Parameter::setName(const std::string& n) {
    if (n.empty()) {
        name = "";
    } else {
        size_t idx = n.find('*');
        name = (idx == std::string::npos) ? n : n.substr(0, idx);
    }
}

void Signatures::Parameter::setIdsRef(const std::string& ref) {
    if (ref.empty()) {
        idsRef = "";
    } else {
        idsRef = ref;
        std::transform(idsRef.begin(), idsRef.end(), idsRef.begin(), ::tolower);
    }
}

std::vector<std::string> Signatures::Parameter::getResourceType() const {
    std::vector<std::string> result;
    if (!resType.empty()) {
        std::stringstream ss(resType);
        std::string item;
        while (std::getline(ss, item, ':')) {
            if (!item.empty()) {
                result.push_back(item);
            }
        }
    }
    return result;
}

// Function methods
std::unique_ptr<Signatures::Function> Signatures::Function::parse(const std::string& line, bool isTrigger) {
    // Parse IDS line: "123 FunctionName(I:Param1*IDS,O:Param2*)"
    static const std::regex pattern(R"(^\s*(\d+|0x[0-9a-fA-F]+)\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(([^)]*)\)\s*)");
    std::smatch match;
    
    if (!std::regex_match(line, match, pattern)) {
        return nullptr;
    }
    
    auto function = std::make_unique<Function>();
    
    // Parse ID (hex or decimal)
    std::string idStr = match[1].str();
    if (idStr.substr(0, 2) == "0x" || idStr.substr(0, 2) == "0X") {
        function->id = std::stoi(idStr, nullptr, 16);
    } else {
        function->id = std::stoi(idStr);
    }
    
    function->name = match[2].str();
    function->functionType = isTrigger ? TRIGGER : ACTION;
    
    // Parse parameters
    std::string paramStr = match[3].str();
    if (!paramStr.empty()) {
        function->parameters = parseParameters(paramStr, function->functionType, function->id);
    }
    
    return function;
}

std::vector<Signatures::Parameter> Signatures::Function::parseParameters(const std::string& paramStr, FunctionType funcType, int id) {
    std::vector<Parameter> result;

    // Split parameters by comma first (outer-level only)
    std::vector<std::string> paramTokens;
    std::istringstream ss(paramStr);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim token whitespace
        size_t start = token.find_first_not_of(" \t\r\n");
        size_t end = token.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) {
            continue;
        }
        std::string t = token.substr(start, end - start + 1);
        if (t.empty()) {
            continue;
        }

        // Expected formats:
        //  - T:Name*IDS
        //  - I:Value
        //  - O:Object*
        //  - S:String
        //  - P:Point
        // Where '*' introduces an optional IDS reference or qualifier

        // Type must be a single uppercase letter followed by ':'
        if (t.size() < 3 || t[1] != ':') {
            continue;
        }

        char type = t[0];
        // Normalize common typo
        if (type == '0') type = 'O';

        // Accept the standard NI set
        switch (type) {
            case 'A': case 'I': case 'O': case 'P': case 'S': case 'T':
                break;
            default:
                // Unknown type, skip
                continue;
        }

        // Extract name and optional idsRef
        std::string nameAndMaybeIds = t.substr(2);

        // Find first '*'
        std::string name;
        std::string idsRef;
        size_t starPos = nameAndMaybeIds.find('*');
        if (starPos == std::string::npos) {
            name = nameAndMaybeIds;
        } else {
            name = nameAndMaybeIds.substr(0, starPos);
            idsRef = nameAndMaybeIds.substr(starPos + 1);
        }

        // Trim both
        auto trim = [](std::string& s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            size_t b = s.find_last_not_of(" \t\r\n");
            if (a == std::string::npos) { s.clear(); return; }
            s = s.substr(a, b - a + 1);
        };
        trim(name);
        trim(idsRef);

        Parameter param(type, name, idsRef);
        result.push_back(param);
    }

    return result;
}

// Signatures methods
std::vector<Signatures::Function*> Signatures::getFunction(int id) {
    std::vector<Function*> result;
    auto it = functions.find(id);
    if (it != functions.end()) {
        for (auto& func : it->second) {
            result.push_back(func.get());
        }
    }
    return result;
}

Signatures::Function* Signatures::getFunction(const std::string& name) {
    auto it = functionsByName.find(name);
    return (it != functionsByName.end()) ? it->second : nullptr;
}

void Signatures::addFunction(std::unique_ptr<Function> function) {
    if (!function) return;
    
    int id = function->getId();
    Function* funcPtr = function.get();
    
    functions[id].push_back(std::move(function));
    functionsByName[funcPtr->getName()] = funcPtr;
}

std::shared_ptr<Signatures> Signatures::getTriggers() {
    return get("TRIGGER", true);
}

std::shared_ptr<Signatures> Signatures::getActions() {
    return get("ACTION", false);
}

std::shared_ptr<Signatures> Signatures::get(const std::string& resource, bool isTrigger) {
    std::string normalizedRes = normalizedName(resource);

    // Fast path under lock to avoid races between worker threads
    {
        std::lock_guard<std::mutex> lk(instancesMutex);
        auto it = instances.find(normalizedRes);
        if (it != instances.end()) {
            return it->second;
        }
    }

    // Load raw IDS content from IdsMapCache
    Log(DEBUG, "Signatures", "Loading game IDS: {}", resource);

    std::string idsContent = IdsMapCache::getRawContent(resource + ".IDS");
    if (idsContent.empty()) {
        Log(DEBUG, "Signatures", "Could not get raw content for: {}.IDS", resource);
        return nullptr;
    }

    Log(DEBUG, "Signatures", "Loaded {} bytes from {}.IDS", idsContent.length(), resource);

    auto signatures = std::make_shared<Signatures>(normalizedRes);
    std::istringstream stream(idsContent);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') { continue; }
        auto function = Function::parse(line, isTrigger);
        if (function) { signatures->addFunction(std::move(function)); }
    }

    {
        std::lock_guard<std::mutex> lk(instancesMutex);
        // Check again to avoid overwriting if another thread won the race
        auto it = instances.find(normalizedRes);
        if (it != instances.end()) {
            return it->second;
        }
        instances[normalizedRes] = signatures;
    }

    Log(DEBUG, "Signatures", "Loaded {} {} signatures from {}", 
        signatures->functions.size(), isTrigger ? "trigger" : "action", resource);
    return signatures;
}

std::string Signatures::normalizedName(const std::string& resource) {
    std::string result = resource;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    if (result.find('.') == std::string::npos) {
        result += ".ids";
    }
    return result;
}

void Signatures::initializeConcatenationMap() {
    static bool initialized = false;
    if (initialized) return;
    
    // Near Infinity's functionConcatMap from ScriptInfo.java
    // Format: functionConcatMap.put(functionId, flags)
    // Flags: Bit 0: first string concatenated, Bit 4: second string concatenated
    //        Bit 8: first string colon-separated, Bit 12: second string colon-separated
    
    // BG1/BG2 concatenation map (lines 163-244 in ScriptInfo.java)
    functionConcatMap[0x400F] = 0x0001; // Global
    functionConcatMap[0x4034] = 0x0001; // GlobalGT
    functionConcatMap[0x4035] = 0x0001; // GlobalLT
    functionConcatMap[30] = 0x0001;     // SetGlobal
    functionConcatMap[109] = 0x0001;    // IncrementGlobal
    functionConcatMap[115] = 0x0001;    // SetGlobalTimer
    functionConcatMap[268] = 0x0001;    // RealSetGlobalTimer
    functionConcatMap[308] = 0x0001;    // SetGlobalTimerOnce
    functionConcatMap[243] = 0x0011;    // IncrementGlobalOnce
    functionConcatMap[0x40A5] = 0x0101; // BitGlobal
    functionConcatMap[247] = 0x0101;    // BitGlobal
    functionConcatMap[306] = 0x0101;    // SetGlobalRandom
    functionConcatMap[307] = 0x0101;    // SetGlobalTimerRandom
    functionConcatMap[0x40A6] = 0x1111; // GlobalBitGlobal
    functionConcatMap[289] = 0x1010;    // SpellCastEffect
    functionConcatMap[248] = 0x1111;    // GlobalBitGlobal
    
    // PST additional concatenations (lines 279-318 in ScriptInfo.java)
    functionConcatMap[0x407F] = 0x0001; // BitCheck
    functionConcatMap[0x4080] = 0x0001; // GlobalBAND
    functionConcatMap[0x4081] = 0x0001; // BitCheckExact
    functionConcatMap[0x4095] = 0x0001; // Xor
    functionConcatMap[0x409C] = 0x0001; // StuffGlobalRandom
    // Removed incorrect mapping for 227 - CreateCreatureObjectEffect should have separate strings
    functionConcatMap[246] = 0x0001;    // CreateCreatureAtLocation - combines first two string parameters
    functionConcatMap[256] = 0x0001;    // CreateItemGlobal - combines first two string parameters // added by me
    functionConcatMap[228] = 0x0001;    // GlobalBOR
    functionConcatMap[229] = 0x0001;    // GlobalSHR
    functionConcatMap[230] = 0x0001;    // GlobalSHL
    functionConcatMap[231] = 0x0001;    // GlobalMAX
    functionConcatMap[232] = 0x0001;    // GlobalMIN
    functionConcatMap[244] = 0x0001;    // BitSet
    // Removed 245 (BitClear) - conflicts with SaveObjectLocation which shouldn't combine strings //functionConcatMap[245] = 0x0001;    // BitClear
    functionConcatMap[260] = 0x0001;    // GlobalXOR
    functionConcatMap[0x4082] = 0x0011; // GlobalEqualsGlobal
    functionConcatMap[0x4083] = 0x0011; // GlobalLTGlobal
    functionConcatMap[0x4084] = 0x0011; // GlobalGTGlobal
    functionConcatMap[0x4085] = 0x0011; // GlobalANDGlobal
    functionConcatMap[0x4086] = 0x0011; // GlobalORGlobal
    functionConcatMap[0x4087] = 0x0011; // GlobalBANDGlobal
    functionConcatMap[0x4088] = 0x0011; // GlobalBANDGlobalExact
    functionConcatMap[202] = 0x0011;    // IncrementGlobalOnce
    functionConcatMap[233] = 0x0011;    // GlobalSetGlobal
    functionConcatMap[234] = 0x0011;    // GlobalAddGlobal
    functionConcatMap[235] = 0x0011;    // GlobalSubGlobal
    functionConcatMap[236] = 0x0011;    // GlobalANDGlobal
    functionConcatMap[237] = 0x0011;    // GlobalORGlobal
    functionConcatMap[238] = 0x0011;    // GlobalBANDGlobal
    functionConcatMap[239] = 0x0011;    // GlobalBORGlobal
    functionConcatMap[240] = 0x0011;    // GlobalSHRGlobal
    functionConcatMap[241] = 0x0011;    // GlobalSHLGlobal
    functionConcatMap[242] = 0x0011;    // GlobalMAXGlobal
    functionConcatMap[243] = 0x0011;    // GlobalMINGlobal
    functionConcatMap[261] = 0x0011;    // GlobalXORGlobal
    
    initialized = true;
}

bool Signatures::isCombinedString(int functionId, int position, int numParameters) {
    // Near Infinity's isCombinedString logic (lines 651-671 in ScriptInfo.java)
    auto it = functionConcatMap.find(functionId);
    if (it == functionConcatMap.end()) return false;

    int v = it->second;
    int numParams = (v >> 16) & 0xffff;
    if (numParams != 0 && numParameters != 0 && numParams != numParameters) {
        return false;
    }

    int mask = v & 0xff;
    int pos = 0;
    while (pos < position) {
        int ofs = ((mask & 1) != 0) ? 2 : 1;
        if (position < pos + ofs) {
            break;
        }
        pos += ofs;
        mask >>= 4;
    }
    return (mask & 1) != 0;
}

bool Signatures::isColonSeparatedString(int functionId, int position, int numParameters) {
    // Near Infinity's isColonSeparatedString logic (lines 681-703 in ScriptInfo.java)
    auto it = functionConcatMap.find(functionId);
    if (it == functionConcatMap.end()) return false;
    
    int v = it->second;
    int numParams = (v >> 16) & 0xffff;
    if (numParams != 0 && numParameters != 0 && numParams != numParameters) {
        return false;
    }
    
    int mask1 = v & 0xff;
    int mask2 = (v >> 8) & 0xff;
    int pos = 0;
    while (pos < position) {
        int ofs = ((mask1 & 1) != 0) ? 2 : 1;
        if (position < pos + ofs) {
            break;
        }
        pos += ofs;
        mask1 >>= 4;
        mask2 >>= 4;
    }
    return (mask2 & 1) != 0;
}

} // namespace ProjectIE4k
