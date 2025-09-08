#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <algorithm>

#include "core/Logging/Logging.h"
#include "core/CFG.h"

namespace ProjectIE4k {

#pragma pack(push, 1)

// Reference: https://burner1024.github.io/iesdp/file_formats/ie_formats/gam_v1.1.htm
struct GAMEV11Header {
    char signature[4];      // 'GAME'
    char version[4];        // 'V1.1'
    uint32_t gameTime;      // 300 units == 1 hour
    uint16_t selectedFormation;
    uint16_t formationButton1;
    uint16_t formationButton2;
    uint16_t formationButton3;
    uint16_t formationButton4;
    uint16_t formationButton5;
    uint32_t partyGold;
    uint16_t partyNpcCountExcludingProtagonist;
    uint16_t weatherBitfield;
    uint32_t partyMembersOffset;                 // Offset to NPC structs for party members
    uint32_t partyMembersCountIncludingProtagonist;
    uint32_t partyInventoryOffset;               // Offset to party inventory (not expanded here)
    uint32_t partyInventoryCount;                // Count of party inventory
    uint32_t nonPartyMembersOffset;              // Offset to NPC structs for non-party members
    uint32_t nonPartyMembersCount;               // Count of NPC structs for non-party members
    uint32_t variablesOffset;                    // Offset to GLOBAL variables         (0x0038)
    uint32_t variablesCount;                     // Count of variables                 (0x003C)
    char      mainArea[8];                       // Main area resref                   (0x0040)
    uint32_t  unknown_0048;                      // Unknown                            (0x0048)
    uint32_t journalCount;                       // Count of journal entries           (0x004C)
    uint32_t journalOffset;                      // Offset to journal entries          (0x0050)
    // Game-specific tail (keep memory layout explicit and variant-safe)
    union {
        struct {
            // BG1/IWD tail per IESDP
            // 0x0054  Party Reputation (*10)
            // 0x0058  Current area (resref)
            // 0x0060  GUI flags
            // 0x0064  Save version (BG1 only)
            // 0x0068  Unknown (76 bytes)
            uint32_t partyReputation;            // 0x0054
            char      currentArea[8];            // 0x0058 (resref)
            uint32_t guiFlags;                   // 0x0060
            uint32_t saveVersion;                // 0x0064 (BG1 only)
            uint8_t  unknown_0068[76];           // 0x0068
        } bg_iwd;
        struct {
            // PST tail per IESDP
            // 0x0054  Offset to Modron Maze struct
            // 0x0058  Party Reputation (*10)
            // 0x005C  Current area (resref)
            // 0x0064  Offset to Kill variables
            // 0x0068  Count of Kill variables
            // 0x006C  Offset to Bestiary structure
            // 0x0070  Current area 2 (resref)
            // 0x0078  Unused (64 bytes)
            uint32_t modronMazeOffset;           // 0x0054
            uint32_t partyReputation;            // 0x0058
            char      currentArea[8];            // 0x005C (resref)
            uint32_t killVariablesOffset;        // 0x0064
            uint32_t killVariablesCount;         // 0x0068
            uint32_t bestiaryOffset;             // 0x006C
            char      currentArea2[8];           // 0x0070 (resref)
            uint8_t  unused_0078[64];            // 0x0078
        } pst;
    } gameSpecific;
};

// GAME V1.1 Character stats
struct GAMEV11CharacterStats {
    uint32_t mostPowerfulVanquishedNameStrRef;  // 0x0000
    uint32_t mostPowerfulVanquishedXP;          // 0x0004
    uint32_t timeInParty;                       // 0x0008 (1/15 sec)
    uint32_t timeJoined;                        // 0x000c (1/15 sec)
    uint8_t inParty;                            // 0x0010 (0/1)
    uint16_t unused;                            // 0x0011
    uint8_t creFirstLetter;                     // 0x0013 (changed to '*')
    uint32_t killsXPChapter;                    // 0x0014
    uint32_t killsNumberChapter;                // 0x0018
    uint32_t killsXPGame;                       // 0x001c
    uint32_t killsNumberGame;                   // 0x0020
    char     favouriteSpells[4][8];             // 0x0024 (4*8)
    uint16_t favouriteSpellCount[4];            // 0x0044 (4*2)
    char     favouriteWeapons[4][8];            // 0x004c (4*8)
    uint16_t favouriteWeaponTime[4];            // 0x006c (4*2)
}; // Size: 0x0074

// GAME V1.1 NPC (both in-party and out-of-party NPCs)
struct GAMEV11NPC {
    uint16_t characterSelection;                // 0x0000 (0=not selected,1=selected,0x8000=dead)
    uint16_t partyOrder;                        // 0x0002 (0x0-0x5 PlayerXFill, 0xFFFF not in party)
    uint32_t creOffset;                         // 0x0004 Offset to CRE data (from start of file)
    uint32_t creSize;                           // 0x0008 Size of CRE data
    char     characterName8[8];                 // 0x000C Character Name (8 bytes)
    uint32_t orientation;                       // 0x0014 Character orientation
    char     currentArea[8];                    // 0x0018 Current area (resref)
    uint16_t x;                                 // 0x0020 Character X coordinate
    uint16_t y;                                 // 0x0022 Character Y coordinate
    uint16_t viewX;                             // 0x0024 Viewing rectangle X
    uint16_t viewY;                             // 0x0026 Viewing rectangle Y
    uint16_t modalAction;                       // 0x0028 Modal action
    uint16_t happiness;                         // 0x002A Happiness
    uint32_t numTimesInteracted[23];            // 0x002C..0x0088 NPC count (unused)
    uint16_t quickWeaponSlot1;                  // 0x008C Index into slots.ids (0xFFFF none)
    uint16_t quickWeaponSlot2;                  // 0x008E
    uint16_t quickWeaponSlot3;                  // 0x0090
    uint16_t quickWeaponSlot4;                  // 0x0092
    int16_t  quickWeaponAbility1;               // 0x0094 (0/1/2 or -1 disabled)
    int16_t  quickWeaponAbility2;               // 0x0096
    int16_t  quickWeaponAbility3;               // 0x0098
    int16_t  quickWeaponAbility4;               // 0x009A
    char     quickSpell1[8];                    // 0x009C Quick Spell 1 resref
    char     quickSpell2[8];                    // 0x00A4 Quick Spell 2 resref
    char     quickSpell3[8];                    // 0x00AC Quick Spell 3 resref
    uint16_t quickItemSlot1;                    // 0x00B4 Index into slots.ids (0xFFFF none)
    uint16_t quickItemSlot2;                    // 0x00B6
    uint16_t quickItemSlot3;                    // 0x00B8
    int16_t  quickItemAbility1;                 // 0x00BA (0/1/2 or -1 disabled)
    int16_t  quickItemAbility2;                 // 0x00BC
    int16_t  quickItemAbility3;                 // 0x00BE
    char     name[32];                          // 0x00C0 Name (32 bytes)
    uint32_t talkCount;                         // 0x00E0 Talkcount
    GAMEV11CharacterStats stats;                // 0x00E4 Character Stats (116 bytes)
    char     voiceSet[8];                       // 0x0158 Voice Set (filename prefix)
    char     voiceSetPath[32];                  // 0x0160 Path to Voice Set folder (IWD only)
};

// GAME V1.1 Game variable (structure mirrors PST kill variables table)
struct GAMEV11Variable {
    char name[32];           // Variable name
    uint16_t type;           // bit0: int, bit1: float, bit2: script, bit3: resref, bit4: strref, bit5: dword
    uint16_t refValue;       // Ref value
    uint32_t dwordValue;     // Dword value
    uint32_t intValue;       // Int value
    double doubleValue;      // Double value
    char scriptNameValue[32];// Script name value
};

// GAME V1.1 Journal entry
struct GAMEV11JournalEntry {
    uint32_t textStrRef;     // Journal text
    uint32_t timeSeconds;    // Time (seconds)
    uint8_t chapterNumber;   // Current chapter number
    uint8_t readByFlags;     // Read by character x
    uint8_t sectionFlags;    // bit0 Quests, bit1 Completed, bit2 Info (0 -> user-note)
    uint8_t locationFlag;    // 0x1F = external TOT/TOH, 0xFF = internal TLK
};

#pragma pack(pop)

// Ensure PST-only structs are packed as well
#pragma pack(push, 1)

// PST-only: Kill variables
struct GAMEV11KillVariable {
    char name[32];
    uint16_t type;
    uint16_t refValue;
    uint32_t dwordValue;
    uint32_t intValue;
    double doubleValue;
    char scriptNameValue[32];
};

// PST-only: Modron Maze Entry (26 bytes)
struct GAMEV11ModronMazeEntry {
    uint32_t used;           // 0: No, 1: Yes
    uint32_t accessible;     // 0: No, 1: Yes
    uint32_t isValid;        // 0: No, 1: Yes
    uint32_t isTrapped;      // 0: No, 1: Yes
    uint32_t trapType;       // 0: Trap A, 1: Trap B, 2: Trap C
    uint16_t exits;          // bit0 East, bit1 West, bit2 North, bit3 South
    uint32_t populated;      // 0: No, 1: Yes
};

// PST-only: Modron Maze (1720 bytes)
struct GAMEV11ModronMaze {
    GAMEV11ModronMazeEntry entries[64]; // 8x8 grid
    uint32_t rows;          // always 8
    uint32_t cols;          // always 8
    uint32_t wizardX;
    uint32_t wizardY;
    uint32_t nordomX;
    uint32_t nordomY;
    uint32_t foyerX;
    uint32_t foyerY;
    uint32_t engineX;
    uint32_t engineY;
    uint32_t totalTraps;
    uint32_t mazeInitialized;         // 0/1
    uint32_t foyerMazeBlockerMade;    // 0/1
    uint32_t foyerEngineBlockerMade;  // 0/1
};

// PST-only: Bestiary (260 bytes)
struct GAMEV11Bestiary {
    uint8_t available[260]; // 1 if creature for specific byte value is available
};

#pragma pack(pop)

// High-level container for GAME V1.1 with safe, section-oriented serialization
enum class GAMEV11Variant { BG1, IWD, PST, UNKNOWN };

inline GAMEV11Variant GAMEV11VariantFromString(const std::string& gameType) {
    std::string t = gameType; std::transform(t.begin(), t.end(), t.begin(), ::tolower);
    if (t.find("pst") != std::string::npos) return GAMEV11Variant::PST;
    if (t.find("bg1") != std::string::npos || t == "bg") return GAMEV11Variant::BG1;
    if (t.find("iwd") != std::string::npos) return GAMEV11Variant::IWD;
    return GAMEV11Variant::UNKNOWN;
}

struct GAMV11File {
    // Convenience overload: infers variant from PIE4K_CFG.GameType
    bool deserialize(const std::vector<uint8_t>& data) {
        GAMEV11Variant variant = GAMEV11VariantFromString(PIE4K_CFG.GameType);
        return deserialize(data, variant);
    }
    bool deserialize(const std::vector<uint8_t>& data, GAMEV11Variant variant) {
        if (data.size() < sizeof(GAMEV11Header)) {
            Log(ERROR, "GAM", "Data too small for GAME V1.1 header");
            return false;
        }

        std::memcpy(&header, data.data(), sizeof(GAMEV11Header));
        if (std::string(header.signature, 4) != "GAME" || std::string(header.version, 4) != "V1.1") {
            Log(ERROR, "GAM", "Invalid signature/version for GAME V1.1");
            return false;
        }

        auto nextOffsetAfter = [&](uint32_t base) -> uint32_t {
            std::vector<uint32_t> candidates;
            auto push = [&](uint32_t off){ if (off > base && off < data.size()) candidates.push_back(off); };
            push(header.partyInventoryOffset);
            push(header.nonPartyMembersOffset);
            push(header.variablesOffset);
            push(header.journalOffset);
            if (variant == GAMEV11Variant::PST) {
                push(header.gameSpecific.pst.killVariablesOffset);
                push(header.gameSpecific.pst.modronMazeOffset);
                push(header.gameSpecific.pst.bestiaryOffset);
            }
            std::sort(candidates.begin(), candidates.end());
            return candidates.empty() ? static_cast<uint32_t>(data.size()) : candidates.front();
        };

        // Party members (structured NPCs)
        partyNPCs.clear();
        if (header.partyMembersOffset > 0 && header.partyMembersCountIncludingProtagonist > 0) {
            uint32_t count = header.partyMembersCountIncludingProtagonist;
            uint64_t need = static_cast<uint64_t>(header.partyMembersOffset) + static_cast<uint64_t>(count) * sizeof(GAMEV11NPC);
            if (need > data.size()) {
                Log(ERROR, "GAM", "V1.1 party NPC block too small for expected count");
                return false;
            }
            partyNPCs.resize(count);
            std::memcpy(partyNPCs.data(), data.data() + header.partyMembersOffset, count * sizeof(GAMEV11NPC));
        }

        // Non-party members (structured NPCs)
        nonPartyNPCs.clear();
        if (header.nonPartyMembersOffset > 0 && header.nonPartyMembersCount > 0) {
            uint32_t count = header.nonPartyMembersCount;
            uint64_t need = static_cast<uint64_t>(header.nonPartyMembersOffset) + static_cast<uint64_t>(count) * sizeof(GAMEV11NPC);
            if (need > data.size()) {
                Log(ERROR, "GAM", "V1.1 non-party NPC block too small for expected count");
                return false;
            }
            nonPartyNPCs.resize(count);
            std::memcpy(nonPartyNPCs.data(), data.data() + header.nonPartyMembersOffset, count * sizeof(GAMEV11NPC));
        }

        // Variables (keep as one raw blob range)
        variablesBlob.clear();
        if (header.variablesOffset > 0 && header.variablesCount > 0) {
            uint32_t end = nextOffsetAfter(header.variablesOffset);
            if (end <= header.variablesOffset || end > data.size()) return false;
            variablesBlob.assign(data.begin() + header.variablesOffset, data.begin() + end);
        }

        // Journal entries (fixed-size)
        journalEntries.clear();
        if (header.journalOffset > 0 && header.journalCount > 0) {
            uint64_t needed = static_cast<uint64_t>(header.journalOffset) + static_cast<uint64_t>(header.journalCount) * sizeof(GAMEV11JournalEntry);
            if (needed > data.size()) {
                Log(ERROR, "GAM", "Journal section exceeds file size");
                return false;
            }
            journalEntries.resize(header.journalCount);
            std::memcpy(journalEntries.data(), data.data() + header.journalOffset, header.journalCount * sizeof(GAMEV11JournalEntry));
        }

        // PST-only sections
        if (variant == GAMEV11Variant::PST) {
            // Kill variables block (optional)
            if (header.gameSpecific.pst.killVariablesOffset > 0 && header.gameSpecific.pst.killVariablesCount > 0) {
                uint32_t end = nextOffsetAfter(header.gameSpecific.pst.killVariablesOffset);
                if (end <= header.gameSpecific.pst.killVariablesOffset || end > data.size()) {
                    Log(ERROR, "GAM", "Kill vars out of bounds");
                    return false;
                }
                killVariablesBlob.assign(data.begin() + header.gameSpecific.pst.killVariablesOffset, data.begin() + end);
            }
            // Modron maze (fixed length 0x6B8) if present
            if (header.gameSpecific.pst.modronMazeOffset > 0) {
                constexpr uint32_t MODRON_SIZE = 0x6B8;
                if (header.gameSpecific.pst.modronMazeOffset + MODRON_SIZE <= data.size()) {
                    modronMazeData.assign(data.begin() + header.gameSpecific.pst.modronMazeOffset,
                                          data.begin() + header.gameSpecific.pst.modronMazeOffset + MODRON_SIZE);
                }
            }
            // Bestiary (260 bytes) if present
            if (header.gameSpecific.pst.bestiaryOffset > 0) {
                constexpr uint32_t BESTIARY_SIZE = 260;
                if (header.gameSpecific.pst.bestiaryOffset + BESTIARY_SIZE <= data.size()) {
                    bestiaryData.assign(data.begin() + header.gameSpecific.pst.bestiaryOffset,
                                        data.begin() + header.gameSpecific.pst.bestiaryOffset + BESTIARY_SIZE);
                }
            }
        }

        return true;
    }

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> out;
        out.resize(sizeof(GAMEV11Header));
        GAMMutableHeader h = header; // local mutable copy
        uint32_t cursor = sizeof(GAMEV11Header);

        auto write_blob = [&](const std::vector<uint8_t>& blob) {
            out.insert(out.end(), blob.begin(), blob.end());
            cursor += blob.size();
        };

        auto write_party = [&]() {
            if (!partyNPCs.empty()) {
                h.partyMembersOffset = cursor;
                h.partyMembersCountIncludingProtagonist = partyNPCs.size();
                const uint8_t *start = reinterpret_cast<const uint8_t*>(partyNPCs.data());
                out.insert(out.end(), start, start + partyNPCs.size() * sizeof(GAMEV11NPC));
                cursor += static_cast<uint32_t>(partyNPCs.size() * sizeof(GAMEV11NPC));
            } else { h.partyMembersOffset = 0; h.partyMembersCountIncludingProtagonist = 0; }
        };

        auto write_non_party = [&]() {
            if (!nonPartyNPCs.empty()) {
                h.nonPartyMembersOffset = cursor;
                h.nonPartyMembersCount = nonPartyNPCs.size();
                const uint8_t *start = reinterpret_cast<const uint8_t*>(nonPartyNPCs.data());
                out.insert(out.end(), start, start + nonPartyNPCs.size() * sizeof(GAMEV11NPC));
                cursor += static_cast<uint32_t>(nonPartyNPCs.size() * sizeof(GAMEV11NPC));
            } else { h.nonPartyMembersOffset = 0; h.nonPartyMembersCount = 0; }
        };

        auto write_variables = [&]() {
            if (!variablesBlob.empty()) {
                h.variablesOffset = cursor;
                // Keep original count; cannot infer safely from raw blob size
                write_blob(variablesBlob);
            } else {
                h.variablesOffset = 0; /* keep h.variablesCount */
            }
        };

        auto write_journal = [&]() {
            if (!journalEntries.empty()) {
                h.journalOffset = cursor;
                h.journalCount = journalEntries.size();
                const uint8_t* start = reinterpret_cast<const uint8_t*>(journalEntries.data());
                const uint8_t* end = start + journalEntries.size() * sizeof(GAMEV11JournalEntry);
                out.insert(out.end(), start, end);
                cursor += static_cast<uint32_t>(end - start);
            } else {
                h.journalOffset = 0; h.journalCount = 0;
            }
        };

        // Write sections in safe order preserving monotonic offsets
        write_party();
        write_non_party();
        write_variables();
        write_journal();

        // PST-only: write PST sections if present
        if (!killVariablesBlob.empty()) {
            h.gameSpecific.pst.killVariablesOffset = cursor;
            out.insert(out.end(), killVariablesBlob.begin(), killVariablesBlob.end());
            cursor += killVariablesBlob.size();
        } else { h.gameSpecific.pst.killVariablesOffset = 0; }

        if (!modronMazeData.empty()) {
            h.gameSpecific.pst.modronMazeOffset = cursor;
            out.insert(out.end(), modronMazeData.begin(), modronMazeData.end());
            cursor += modronMazeData.size();
        } else { h.gameSpecific.pst.modronMazeOffset = 0; }

        if (!bestiaryData.empty()) {
            h.gameSpecific.pst.bestiaryOffset = cursor;
            out.insert(out.end(), bestiaryData.begin(), bestiaryData.end());
            cursor += bestiaryData.size();
        } else { h.gameSpecific.pst.bestiaryOffset = 0; }

        // Copy updated header back
        std::memcpy(out.data(), &h, sizeof(GAMEV11Header));
        return out;
    }

    // Public data
    struct GAMMutableHeader : public GAMEV11Header {};
    GAMMutableHeader header{};
    std::vector<GAMEV11NPC> partyNPCs;
    std::vector<GAMEV11NPC> nonPartyNPCs;
    std::vector<uint8_t> variablesBlob;
    std::vector<GAMEV11JournalEntry> journalEntries;
    // PST-only raw sections to preserve
    std::vector<uint8_t> killVariablesBlob;
    std::vector<uint8_t> modronMazeData;
    std::vector<uint8_t> bestiaryData;
};

} // namespace ProjectIE4k


