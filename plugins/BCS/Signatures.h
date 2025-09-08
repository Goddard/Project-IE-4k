#pragma once

#include <map>
#include <vector>
#include <string>
#include <memory>

namespace ProjectIE4k {

/**
 * Near Infinity-style signatures system for BCS functions.
 * Manages action and trigger function signatures.
 */
class Signatures {
public:
    /** Function parameter types */
    enum ParameterType {
        TYPE_ACTION  = 'A',  // Action parameter
        TYPE_TRIGGER = 'T',  // Trigger parameter  
        TYPE_INTEGER = 'I',  // Numeric value
        TYPE_OBJECT  = 'O',  // Object specifier
        TYPE_POINT   = 'P',  // Point structure
        TYPE_STRING  = 'S'   // Literal string
    };

    /** Provides information about a single function parameter */
    class Parameter {
    public:
        /** Special resource type constants */
        static const std::string RESTYPE_SCRIPT;
        static const std::string RESTYPE_SPELL_LIST;

        Parameter() : type('\0'), combinedString(false), colonSeparated(false) {}
        Parameter(char type, const std::string& name, const std::string& idsRef) 
            : type(type), name(name), idsRef(idsRef), combinedString(false), colonSeparated(false) {}

        /** Returns parameter type letter */
        char getType() const { return type; }
        void setType(char t) { type = t; }

        /** Returns parameter name (without trailing asterisk) */
        const std::string& getName() const { return name; }
        void setName(const std::string& n);

        /** Returns lowercased IDS reference */
        const std::string& getIdsRef() const { return idsRef; }
        void setIdsRef(const std::string& ref);

        /** Returns list of resource types referenced by parameter */
        std::vector<std::string> getResourceType() const;
        void setResourceType(const std::string& resType) { this->resType = resType; }

        /** Returns whether parameter is part of combined string */
        bool isCombinedString() const { return combinedString; }
        void setCombinedString(bool combined) { combinedString = combined; }

        /** Returns whether parameter is colon-separated */
        bool isColonSeparatedString() const { return colonSeparated; }
        void setColonSeparatedString(bool colon) { colonSeparated = colon; }
        
        /** Check if parameter is combined for specific function context */
        bool isCombinedString(int functionId, int position, int numParameters) const {
            return Signatures::isCombinedString(functionId, position, numParameters);
        }
        
        /** Check if parameter is colon-separated for specific function context */
        bool isColonSeparatedString(int functionId, int position, int numParameters) const {
            return Signatures::isColonSeparatedString(functionId, position, numParameters);
        }

    private:
        char type;
        std::string name;
        std::string idsRef;
        std::string resType;
        bool combinedString;
        bool colonSeparated;
    };

    /** Function definition with parameters */
    class Function {
    public:
        enum FunctionType { TRIGGER, ACTION };

        static const std::string TRIGGER_OVERRIDE_NAME;

        Function() : id(0), functionType(TRIGGER) {}

        int getId() const { return id; }
        void setId(int funcId) { id = funcId; }

        const std::string& getName() const { return name; }
        void setName(const std::string& funcName) { name = funcName; }

        FunctionType getFunctionType() const { return functionType; }
        void setFunctionType(FunctionType type) { functionType = type; }

        size_t getNumParameters() const { return parameters.size(); }
        const Parameter& getParameter(size_t index) const { return parameters[index]; }
        void addParameter(const Parameter& param) { parameters.push_back(param); }

        /** Parse function definition from IDS line */
        static std::unique_ptr<Function> parse(const std::string& line, bool isTrigger);

    private:
        int id;
        std::string name;
        FunctionType functionType;
        std::vector<Parameter> parameters;

        /** Parse parameter list from signature string */
        static std::vector<Parameter> parseParameters(const std::string& paramStr, FunctionType funcType, int id);
    };

    /** Constructor for specific IDS resource */
    Signatures(const std::string& resource) : resource(resource) { initializeConcatenationMap(); }

    /** Returns IDS resource name */
    const std::string& getResource() const { return resource; }
    
    /** Check if function has concatenated strings */
    static bool isCombinedString(int functionId, int position, int numParameters);
    
    /** Check if function has colon-separated strings */
    static bool isColonSeparatedString(int functionId, int position, int numParameters);

    /** Returns function signatures for given ID */
    std::vector<Function*> getFunction(int id);

    /** Returns function by name (case-sensitive) */
    Function* getFunction(const std::string& name);

    /** Add function to signatures */
    void addFunction(std::unique_ptr<Function> function);

    /** Static factory methods */
    static std::shared_ptr<Signatures> getTriggers();
    static std::shared_ptr<Signatures> getActions();
    static std::shared_ptr<Signatures> get(const std::string& resource, bool isTrigger);

private:
    std::string resource;
    std::map<int, std::vector<std::unique_ptr<Function>>> functions;
    std::map<std::string, Function*> functionsByName;

    static std::map<std::string, std::shared_ptr<Signatures>> instances;
    static std::string normalizedName(const std::string& resource);
    
    /** Near Infinity concatenation map (function ID -> concatenation flags) */
    static std::map<int, int> functionConcatMap;
    
    /** Initialize the concatenation map with Near Infinity data */
    static void initializeConcatenationMap();
};

} // namespace ProjectIE4k
