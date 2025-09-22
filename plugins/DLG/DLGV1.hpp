#ifndef DLGV1_HPP
#define DLGV1_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>

#include "core/Logging/Logging.h"
#include "core/CFG.h"

namespace ProjectIE4k {

#pragma pack(push, 1)

struct DLGHeader {
    char signature[4];              // 0x0000 Signature ('DLG ')
    char version[4];                // 0x0004 Version ('V1.0')
    uint32_t statesCount;           // 0x0008 Number of states
    uint32_t statesOffset;          // 0x000c Offset of state table from start of file
    uint32_t transitionsCount;      // 0x0010 Number of transitions
    uint32_t transitionsOffset;     // 0x0014 Offset of transition table from start of file
    uint32_t stateTriggersOffset;   // 0x0018 Offset of state trigger table from start of file
    uint32_t stateTriggersCount;    // 0x001c Number of state triggers
    uint32_t transitionTriggersOffset; // 0x0020 Offset of transition trigger table from start of file
    uint32_t transitionTriggersCount;  // 0x0024 Number of transition triggers
    uint32_t actionsOffset;         // 0x0028 Offset of action table from start of file
    uint32_t actionsCount;          // 0x002c Number of actions
    // Game-specific tail (keep memory layout explicit and variant-safe)
    union {
        struct {
            // BG1 format (48 bytes total) - no flags field
            char unused[0];          // No additional fields in BG1
        } bg1;
        struct {
            // BG2+ format (52 bytes total) - includes flags field
            uint32_t flags;          // 0x0030 Flags specifying what the creature does when the dialog is interrupted by a hostile action
                                    // bit 0: Enemy()
                                    // bit 1: EscapeArea()
                                    // bit 2: nothing (but since the action was hostile, it behaves similar to bit 0)
        } bg2plus;
    } gameSpecific;
};

struct DLGState {
    uint32_t actorText;             // 0x0000 Actor response text (strref - what the non-player character says to the party)
    uint32_t firstTransitionIndex;  // 0x0004 Index of the first transition corresponding to this state
    uint32_t transitionsCount;      // 0x0008 Number of transitions corresponding to this state
    uint32_t triggerIndex;          // 0x000c Trigger for this state (as index into the state trigger table), or 0xFFFFFFFF if no trigger is used
};

struct DLGTransition {
    uint32_t flags;                 // 0x0000 Flags:
                                    // bit 0: 1=Associated text, 0=no associated text
                                    // bit 1: 1=Trigger, 0=no trigger
                                    // bit 2: 1=Action, 0=no action
                                    // bit 3: 1=Terminates dialog, 0=has "next node" information
                                    // bit 4: 1=Journal entry, 0=no journal entry
                                    // bit 5: Interrupt
                                    // bit 6: Add Unsolved Quest Journal entry (BG2)
                                    // bit 7: Add Journal note (BG2)
                                    // bit 8: Add Solved Quest Journal entry (BG2)
                                    // bit 9: 1=Immediate execution of script actions, 0=Delayed execution of script actions (BGEE)
                                    // bit 10: Clear actions (BGEE)
    uint32_t playerText;            // 0x0004 If flags bit 0 was set, this is the text associated with the transition (strref - what the player character says)
    uint32_t journalText;           // 0x0008 If flags bit 4 was set, this is the text that goes into your journal after you have spoken (strref)
    uint32_t triggerIndex;          // 0x000c If flags bit 1 was set, this is the index of this transition's trigger within the transition trigger table
    uint32_t actionIndex;           // 0x0010 If flags bit 2 was set, this is the index of this transition's action within the action table
    char nextDialogResource[8];     // 0x0014 If flags bit 3 was not set, this is the resource name of the DLG resource which contains the next state
    uint32_t nextStateIndex;        // 0x001c If flags bit 3 was not set, this is the index of the next state within the DLG resource specified by the previous field
};

struct DLGStateTrigger {
    uint32_t offset;                // 0x0000 Offset from start of file to state trigger string
    uint32_t length;                // 0x0004 Length in bytes of the state trigger string (not zero terminated)
};

struct DLGTransitionTrigger {
    uint32_t offset;                // 0x0000 Offset from start of file to transition trigger string
    uint32_t length;                // 0x0004 Length in bytes of the transition trigger string (not zero terminated)
};

struct DLGAction {
    uint32_t offset;                // 0x0000 Offset from start of file to the action string
    uint32_t length;                // 0x0004 Length in bytes of the action string (not zero terminated)
};

#pragma pack(pop)

struct DLGFile {
    bool deserialize(const std::vector<uint8_t>& data) {
        // Check for minimum header size (BG1 = 48 bytes, BG2+ = 52 bytes)
        if (data.size() < 48) {
            Log(ERROR, "DLG", "Data size is too small for a DLG header (minimum 48 bytes for BG1 format).");
            return false;
        }

        // Determine format based on file size and game type
        bool isBG1Format = (data.size() < 52) || (PIE4K_CFG.GameType == "bg1");
        
        if (isBG1Format) {
            Log(DEBUG, "DLG", "Detected BG1 DLG format (48-byte header)");
            // Copy the first 48 bytes (common fields only)
            std::memcpy(&header, data.data(), 48);
            // Initialize game-specific fields
            header.gameSpecific.bg1.unused[0] = 0; // No additional fields in BG1
        } else {
            Log(DEBUG, "DLG", "Detected BG2+ DLG format (52-byte header)");
            // Copy full header including game-specific fields
            std::memcpy(&header, data.data(), sizeof(DLGHeader));
        }

        Log(DEBUG, "DLG", "DLG deserialization header values:");
        Log(DEBUG, "DLG", "  Header size: {}", sizeof(DLGHeader));
        Log(DEBUG, "DLG", "  File size: {}", data.size());
        Log(DEBUG, "DLG", "  Signature: {:.4s}", header.signature);
        Log(DEBUG, "DLG", "  Version: {:.4s}", header.version);
        Log(DEBUG, "DLG", "  States count: {}", header.statesCount);
        Log(DEBUG, "DLG", "  States offset: {}", header.statesOffset);
        Log(DEBUG, "DLG", "  Transitions count: {}", header.transitionsCount);
        Log(DEBUG, "DLG", "  Transitions offset: {}", header.transitionsOffset);
        if (!isBG1Format) {
            Log(DEBUG, "DLG", "  Flags: 0x{:08x}", header.gameSpecific.bg2plus.flags);
        }

        // Validate signature and version
        if (std::string(header.signature, 4) != "DLG " || std::string(header.version, 4) != "V1.0") {
            Log(ERROR, "DLG", "Invalid DLG file signature or version.");
            return false;
        }

        auto read_section = [&](auto& vec, uint32_t offset, uint32_t count) {
            if (count > 0 && offset > 0 && (offset + count * sizeof(typename std::remove_reference_t<decltype(vec)>::value_type)) <= data.size()) {
                vec.resize(count);
                std::memcpy(vec.data(), data.data() + offset, count * sizeof(typename std::remove_reference_t<decltype(vec)>::value_type));
            }
        };

        // Read all sections based on header offsets and counts
        read_section(states, header.statesOffset, header.statesCount);
        read_section(transitions, header.transitionsOffset, header.transitionsCount);
        read_section(stateTriggers, header.stateTriggersOffset, header.stateTriggersCount);
        read_section(transitionTriggers, header.transitionTriggersOffset, header.transitionTriggersCount);
        read_section(actions, header.actionsOffset, header.actionsCount);

        // Read the actual trigger and action strings
        // These are stored as raw string data at the end of the file
        for (const auto& trigger : stateTriggers) {
            if (trigger.offset > 0 && trigger.length > 0 && 
                (trigger.offset + trigger.length) <= data.size()) {
                stateTriggerStrings.emplace_back(
                    reinterpret_cast<const char*>(data.data() + trigger.offset), 
                    trigger.length
                );
            } else {
                stateTriggerStrings.emplace_back();
            }
        }

        for (const auto& trigger : transitionTriggers) {
            if (trigger.offset > 0 && trigger.length > 0 && 
                (trigger.offset + trigger.length) <= data.size()) {
                transitionTriggerStrings.emplace_back(
                    reinterpret_cast<const char*>(data.data() + trigger.offset), 
                    trigger.length
                );
            } else {
                transitionTriggerStrings.emplace_back();
            }
        }

        for (const auto& action : actions) {
            if (action.offset > 0 && action.length > 0 && 
                (action.offset + action.length) <= data.size()) {
                actionStrings.emplace_back(
                    reinterpret_cast<const char*>(data.data() + action.offset), 
                    action.length
                );
            } else {
                actionStrings.emplace_back();
            }
        }

        return true;
    }

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        DLGHeader updatedHeader = header; // Create a mutable copy
        
        // Determine if this should be BG1 format (48 bytes) or BG2+ format (52 bytes)
        // Use game type and data characteristics to determine format
        bool isBG1Format = (PIE4K_CFG.GameType == "bg1") || 
                           (updatedHeader.statesCount == 0 && updatedHeader.transitionsCount == 0);
        
        uint32_t headerSize = isBG1Format ? 48 : sizeof(DLGHeader);
        uint32_t currentOffset = headerSize;

        Log(DEBUG, "DLG", "Starting DLG serialization with {} format ({} byte header), currentOffset: {}", 
            isBG1Format ? "BG1" : "BG2+", headerSize, currentOffset);

        auto write_section = [&](const auto& vec, uint32_t& offset, auto& count, const char* sectionName) {
            if (!vec.empty()) {
                offset = currentOffset;
                count = vec.size();
                size_t sectionSize = vec.size() * sizeof(typename std::remove_reference<decltype(vec)>::type::value_type);
                Log(DEBUG, "DLG", "Writing {} section: offset={}, count={}, size={}",
                    sectionName, offset, (size_t)count, sectionSize);
                const uint8_t* start = reinterpret_cast<const uint8_t*>(vec.data());
                const uint8_t* end = start + sectionSize;
                data.insert(data.end(), start, end);
                currentOffset += sectionSize;
            } else {
                offset = 0;
                count = 0;
                Log(DEBUG, "DLG", "Skipping empty {} section", sectionName);
            }
        };
        
        // Reserve space for header
        data.resize(headerSize);

        // Write sections and update header offsets/counts
        write_section(states, updatedHeader.statesOffset, updatedHeader.statesCount, "states");
        write_section(transitions, updatedHeader.transitionsOffset, updatedHeader.transitionsCount, "transitions");
        write_section(stateTriggers, updatedHeader.stateTriggersOffset, updatedHeader.stateTriggersCount, "stateTriggers");
        write_section(transitionTriggers, updatedHeader.transitionTriggersOffset, updatedHeader.transitionTriggersCount, "transitionTriggers");
        write_section(actions, updatedHeader.actionsOffset, updatedHeader.actionsCount, "actions");

        // Write the actual string data for triggers and actions
        // Update the offset fields in the trigger/action structs to point to the correct locations
        std::vector<DLGStateTrigger> updatedStateTriggers = stateTriggers;
        for (size_t i = 0; i < stateTriggerStrings.size() && i < updatedStateTriggers.size(); ++i) {
            if (!stateTriggerStrings[i].empty()) {
                updatedStateTriggers[i].offset = currentOffset;
                updatedStateTriggers[i].length = stateTriggerStrings[i].length();
                Log(DEBUG, "DLG", "Writing state trigger string {}: offset={}, length={}",
                    i, currentOffset, stateTriggerStrings[i].length());
                data.insert(data.end(), stateTriggerStrings[i].begin(), stateTriggerStrings[i].end());
                currentOffset += stateTriggerStrings[i].length();
            }
        }

        std::vector<DLGTransitionTrigger> updatedTransitionTriggers = transitionTriggers;
        for (size_t i = 0; i < transitionTriggerStrings.size() && i < updatedTransitionTriggers.size(); ++i) {
            if (!transitionTriggerStrings[i].empty()) {
                updatedTransitionTriggers[i].offset = currentOffset;
                updatedTransitionTriggers[i].length = transitionTriggerStrings[i].length();
                Log(DEBUG, "DLG", "Writing transition trigger string {}: offset={}, length={}",
                    i, currentOffset, transitionTriggerStrings[i].length());
                data.insert(data.end(), transitionTriggerStrings[i].begin(), transitionTriggerStrings[i].end());
                currentOffset += transitionTriggerStrings[i].length();
            }
        }

        std::vector<DLGAction> updatedActions = actions;
        for (size_t i = 0; i < actionStrings.size() && i < updatedActions.size(); ++i) {
            if (!actionStrings[i].empty()) {
                updatedActions[i].offset = currentOffset;
                updatedActions[i].length = actionStrings[i].length();
                Log(DEBUG, "DLG", "Writing action string {}: offset={}, length={}",
                    i, currentOffset, actionStrings[i].length());
                data.insert(data.end(), actionStrings[i].begin(), actionStrings[i].end());
                currentOffset += actionStrings[i].length();
            }
        }

        // Update the trigger/action sections with the corrected offsets
        if (!updatedStateTriggers.empty()) {
            std::memcpy(data.data() + updatedHeader.stateTriggersOffset, 
                       updatedStateTriggers.data(), 
                       updatedStateTriggers.size() * sizeof(DLGStateTrigger));
        }
        if (!updatedTransitionTriggers.empty()) {
            std::memcpy(data.data() + updatedHeader.transitionTriggersOffset, 
                       updatedTransitionTriggers.data(), 
                       updatedTransitionTriggers.size() * sizeof(DLGTransitionTrigger));
        }
        if (!updatedActions.empty()) {
            std::memcpy(data.data() + updatedHeader.actionsOffset, 
                       updatedActions.data(), 
                       updatedActions.size() * sizeof(DLGAction));
        }

        // Write the updated header at the beginning of the data vector
        std::memcpy(data.data(), &updatedHeader, headerSize);

        Log(DEBUG, "DLG", "DLG serialization complete - data size: {}, states: {}, transitions: {}",
            data.size(), updatedHeader.statesCount, updatedHeader.transitionsCount);

        return data;
    }

    DLGHeader header;
    std::vector<DLGState> states;
    std::vector<DLGTransition> transitions;
    std::vector<DLGStateTrigger> stateTriggers;
    std::vector<DLGTransitionTrigger> transitionTriggers;
    std::vector<DLGAction> actions;
    
    // String data for triggers and actions (not zero terminated)
    std::vector<std::string> stateTriggerStrings;
    std::vector<std::string> transitionTriggerStrings;
    std::vector<std::string> actionStrings;
};

} // namespace ProjectIE4k

#endif // DLGV1_HPP
