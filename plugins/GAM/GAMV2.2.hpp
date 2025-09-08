#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <algorithm>

#include "core/Logging/Logging.h"

namespace ProjectIE4k {

#pragma pack(push, 1)

// Reference: https://burner1024.github.io/iesdp/file_formats/ie_formats/gam_v2.2.htm
struct GAMEV22Header {
    char signature[4];      // 'GAME'
    char version[4];        // 'V2.2'
    uint32_t gameTime;      // 300 units == 1 hour
    uint16_t selectedFormation;
    uint16_t formationButton1;
    uint16_t formationButton2;
    uint16_t formationButton3;
    uint16_t formationButton4;
    uint16_t formationButton5;
    uint32_t partyGold;
    uint16_t unknown_001c;  // Unknown (per IESDP)
    uint16_t weatherBitfield;
    uint32_t partyMembersOffset;                 // Offset to NPC structs for party members
    uint32_t partyMembersCount;                  // Count of party member NPC structs
    uint32_t partyInventoryOffset;               // Offset to party inventory
    uint32_t partyInventoryCount;                // Count of party inventory
    uint32_t nonPartyMembersOffset;              // Offset to NPC structs for non-party members
    uint32_t nonPartyMembersCount;               // Count of NPC structs for non-party members
    uint32_t variablesOffset;                    // Offset to GLOBAL variables
    uint32_t variablesCount;                     // Count of GLOBAL variables
    char mainArea[8];                            // 0x0040 Main area resref
    uint32_t unknown_0048;                       // 0x0048 Unknown
    uint32_t journalCount;                       // 0x004C Count of journal entries
    uint32_t journalOffset;                      // 0x0050 Offset to journal entries
    uint32_t partyReputation;                    // 0x0054 Party Reputation (*10)
    char currentArea[8];                         // 0x0058 Current area
    uint32_t guiFlags;                           // 0x0060 GUI flags
    uint32_t unknown_0064;                       // 0x0064 Unknown
    uint32_t familiarInfoOffset;                 // 0x0068 Offset to familiar info
    uint32_t heartOfFuryToggle;                  // 0x006C Heart of Fury mode toggle (bit 0)
    uint32_t unknown_0070;                       // 0x0070 Unknown
    uint32_t realGameTime;                       // 0x0074 Real game time
    uint32_t pocketPlaneLocationsOffset;         // 0x0078 Offset to Pocket Plane locations
    uint32_t pocketPlaneLocationsCount;          // 0x007C Count of Pocket Plane locations
    uint8_t  unused_0080[52];                    // 0x0080 Unused
};

// Variables and journals share same per-entry format as V2.0 (engine only reads/writes INT variables)
struct GAMEV22Variable {
    char name[32];
    uint16_t type;
    uint16_t refValue;
    uint32_t dwordValue;
    uint32_t intValue;
    double doubleValue;
    char scriptNameValue[32];
};

#pragma pack(pop)

// GAME V2.2 Familiar info
#pragma pack(push, 1)
struct GAMEV22FamiliarInfo {
    char lawfulGood[8];            // 0x0000
    char lawfulNeutral[8];         // 0x0008
    char lawfulEvil[8];            // 0x0010
    char neutralGood[8];           // 0x0018
    char neutralNeutral[8];        // 0x0020
    char neutralEvil[8];           // 0x0028
    char chaoticGood[8];           // 0x0030
    char chaoticNeutral[8];        // 0x0038
    char chaoticEvil[8];           // 0x0040
    uint8_t unknown_0048[328];     // 0x0048 Unknown bytes
};
#pragma pack(pop)

// GAME V2.2 Journal entry
#pragma pack(push, 1)
struct GAMEV22JournalEntry {
    uint32_t textStrRef;     // 0x0000 Journal text
    uint32_t timeSeconds;    // 0x0004 Time (seconds)
    uint8_t  chapterNumber;  // 0x0008 Current chapter number
    uint8_t  readByFlags;    // 0x0009 Read by character x
    uint8_t  sectionFlags;   // 0x000A bit0 Quests, bit1 Completed, bit2 Info (0 -> user-note)
    uint8_t  locationFlag;   // 0x000B 0x1F external TOT/TOH, 0xFF internal TLK
};
#pragma pack(pop)

// GAME V2.2 Character stats (same layout as V2.0)
#pragma pack(push, 1)
struct GAMEV22CharacterStats {
    uint32_t mostPowerfulVanquishedNameStrRef;  // 0x0000
    uint32_t mostPowerfulVanquishedXP;          // 0x0004
    uint32_t timeInParty;                       // 0x0008 (1/15 sec)
    uint32_t timeJoined;                        // 0x000C (1/15 sec)
    uint8_t  inParty;                           // 0x0010
    uint16_t unused;                            // 0x0011
    uint8_t  creFirstLetter;                    // 0x0013
    uint32_t killsXPChapter;                    // 0x0014
    uint32_t killsNumberChapter;                // 0x0018
    uint32_t killsXPGame;                       // 0x001C
    uint32_t killsNumberGame;                   // 0x0020
    char     favouriteSpells[4][8];             // 0x0024
    uint16_t favouriteSpellCount[4];            // 0x0044
    char     favouriteWeapons[4][8];            // 0x004C
    uint16_t favouriteWeaponTime[4];            // 0x006C
};
#pragma pack(pop)

// GAME V2.2 NPCs (both in-party and out-of-party)
#pragma pack(push, 1)
struct GAMEV22NPC {
    uint16_t characterSelection;          // 0x0000 (0,1,0x8000)
    uint16_t partyOrder;                  // 0x0002 (0x0-0x5, 0xFFFF not in party)
    uint32_t creOffset;                   // 0x0004 Offset to CRE data
    uint32_t creSize;                     // 0x0008 Size of CRE data
    char     characterName8[8];           // 0x000C Character Name (8 bytes)
    uint32_t orientation;                 // 0x0014 Character orientation
    char     currentArea[8];              // 0x0018 Current area (resref)
    uint16_t x;                           // 0x0020 Character X
    uint16_t y;                           // 0x0022 Character Y
    uint16_t viewX;                       // 0x0024 Viewing rectangle X
    uint16_t viewY;                       // 0x0026 Viewing rectangle Y
    uint16_t modalAction;                 // 0x0028 Modal action
    uint16_t happiness;                   // 0x002A Happiness
    uint32_t numTimesInteracted[23];      // 0x002C..0x0088 Unused counts
    // Quick weapons/shields
    uint16_t quickWeapon1;                // 0x008C
    uint16_t quickShield1;                // 0x008E
    uint16_t quickWeapon2;                // 0x0090
    uint16_t quickShield2;                // 0x0092
    uint16_t quickWeapon3;                // 0x0094
    uint16_t quickShield3;                // 0x0096
    uint16_t quickWeapon4;                // 0x0098
    uint16_t quickShield4;                // 0x009A
    // Slot usable flags (8 words)
    uint16_t slotUsable[8];               // 0x009C..0x00AA
    // Quick Spells (9 resrefs)
    char     quickSpell1[8];              // 0x00AC
    char     quickSpell2[8];              // 0x00B4
    char     quickSpell3[8];              // 0x00BC
    char     quickSpell4[8];              // 0x00C4
    char     quickSpell5[8];              // 0x00CC
    char     quickSpell6[8];              // 0x00D4
    char     quickSpell7[8];              // 0x00DC
    char     quickSpell8[8];              // 0x00E4
    char     quickSpell9[8];              // 0x00EC
    // Quick spell classes (9 bytes) + unknown
    uint8_t  quickSpellClass[9];          // 0x00F4..0x00FC
    uint8_t  unknown_00FD;                // 0x00FD
    // Quick items
    uint16_t quickItemSlot1;              // 0x00FE
    uint16_t quickItemSlot2;              // 0x0100
    uint16_t quickItemSlot3;              // 0x0102
    uint16_t slotUsableItems[3];          // 0x0104..0x0108
    // Quick Innates (9 resrefs)
    char     quickInnate1[8];             // 0x010A
    char     quickInnate2[8];             // 0x0112
    char     quickInnate3[8];             // 0x011A
    char     quickInnate4[8];             // 0x0122
    char     quickInnate5[8];             // 0x012A
    char     quickInnate6[8];             // 0x0132
    char     quickInnate7[8];             // 0x013A
    char     quickInnate8[8];             // 0x0142
    char     quickInnate9[8];             // 0x014A
    // Quick Songs (9 resrefs)
    char     quickSong1[8];               // 0x0152
    char     quickSong2[8];               // 0x015A
    char     quickSong3[8];               // 0x0162
    char     quickSong4[8];               // 0x016A
    char     quickSong5[8];               // 0x0172
    char     quickSong6[8];               // 0x017A
    char     quickSong7[8];               // 0x0182
    char     quickSong8[8];               // 0x018A
    char     quickSong9[8];               // 0x0192
    // Quick Slots (9 dwords)
    uint32_t quickSlots[9];               // 0x019A..0x01BA
    char     name[32];                    // 0x01BE Character Name
    uint32_t talkCount;                   // 0x01C2 Talkcount
    GAMEV22CharacterStats stats;          // 0x01C6..0x0239 (116 bytes)
    char     soundset[8];                 // 0x023A Soundset
    char     voiceSet[32];                // 0x0242 Voiceset
    uint32_t unknown_0262;                // 0x0262
    uint32_t unknown_0266;                // 0x0266
    uint32_t unknown_026A;                // 0x026A
    uint32_t expertise;                   // 0x026E (mutually exclusive with powerAttack)
    uint32_t powerAttack;                 // 0x0272
    uint32_t arterialStrike;              // 0x0276
    uint32_t hamstring;                   // 0x027A
    uint32_t rapidShot;                   // 0x027E
    uint32_t unknown_0282;                // 0x0282
    uint8_t  unknown_0286[3];             // 0x0286
    uint16_t selectedWeaponSlot;          // 0x0289
    uint8_t  unknownTail[153];            // 0x028B .. end (size per spec note)
};
#pragma pack(pop)

// High-level container for GAME V2.2 with safe, section-oriented serialization
struct GAMV22File {
    // Deserialize binary into structured fields
    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(GAMEV22Header)) {
            Log(ERROR, "GAM", "Data too small for GAME V2.2 header");
            return false;
        }
        std::memcpy(&header, data.data(), sizeof(GAMEV22Header));
        if (std::string(header.signature, 4) != "GAME" || std::string(header.version, 4) != "V2.2") {
            Log(ERROR, "GAM", "Invalid signature/version for GAME V2.2");
            return false;
        }

        auto nextOffsetAfter = [&](uint32_t base) -> uint32_t {
            std::vector<uint32_t> candidates;
            auto push = [&](uint32_t off){ if (off > base && off < data.size()) candidates.push_back(off); };
            push(header.partyInventoryOffset);
            push(header.nonPartyMembersOffset);
            push(header.variablesOffset);
            push(header.journalOffset);
            std::sort(candidates.begin(), candidates.end());
            return candidates.empty() ? static_cast<uint32_t>(data.size()) : candidates.front();
        };

        // Party members (structured NPCs)
        partyNPCs.clear();
        if (header.partyMembersOffset > 0 && header.partyMembersCount > 0) {
            uint32_t count = header.partyMembersCount;
            uint64_t need = static_cast<uint64_t>(header.partyMembersOffset) + static_cast<uint64_t>(count) * sizeof(GAMEV22NPC);
            if (need > data.size()) {
                Log(ERROR, "GAM", "V2.2 party NPC block too small for expected count");
                return false;
            }
            partyNPCs.resize(count);
            std::memcpy(partyNPCs.data(), data.data() + header.partyMembersOffset, count * sizeof(GAMEV22NPC));
        }

        // Non-party members (structured NPCs)
        nonPartyNPCs.clear();
        if (header.nonPartyMembersOffset > 0 && header.nonPartyMembersCount > 0) {
            uint32_t count = header.nonPartyMembersCount;
            uint64_t need = static_cast<uint64_t>(header.nonPartyMembersOffset) + static_cast<uint64_t>(count) * sizeof(GAMEV22NPC);
            if (need > data.size()) {
                Log(ERROR, "GAM", "V2.2 non-party NPC block too small for expected count");
                return false;
            }
            nonPartyNPCs.resize(count);
            std::memcpy(nonPartyNPCs.data(), data.data() + header.nonPartyMembersOffset, count * sizeof(GAMEV22NPC));
        }

        // Variables (raw blob)
        variablesBlob.clear();
        if (header.variablesOffset > 0 && header.variablesCount > 0) {
            uint32_t end = nextOffsetAfter(header.variablesOffset);
            if (end <= header.variablesOffset || end > data.size()) return false;
            variablesBlob.assign(data.begin() + header.variablesOffset, data.begin() + end);
        }

        return true;
    }

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> out;
        out.resize(sizeof(GAMEV22Header));
        GAMMutableHeader h = header;
        uint32_t cursor = sizeof(GAMEV22Header);

        auto write_blob = [&](const std::vector<uint8_t>& blob) {
            out.insert(out.end(), blob.begin(), blob.end());
            cursor += blob.size();
        };

        auto write_party = [&]() {
            if (!partyNPCs.empty()) {
                h.partyMembersOffset = cursor;
                h.partyMembersCount = partyNPCs.size();
                const uint8_t *start = reinterpret_cast<const uint8_t*>(partyNPCs.data());
                out.insert(out.end(), start, start + partyNPCs.size() * sizeof(GAMEV22NPC));
                cursor += static_cast<uint32_t>(partyNPCs.size() * sizeof(GAMEV22NPC));
            } else { h.partyMembersOffset = 0; h.partyMembersCount = 0; }
        };

        auto write_non_party = [&]() {
            if (!nonPartyNPCs.empty()) {
                h.nonPartyMembersOffset = cursor;
                h.nonPartyMembersCount = nonPartyNPCs.size();
                const uint8_t *start = reinterpret_cast<const uint8_t*>(nonPartyNPCs.data());
                out.insert(out.end(), start, start + nonPartyNPCs.size() * sizeof(GAMEV22NPC));
                cursor += static_cast<uint32_t>(nonPartyNPCs.size() * sizeof(GAMEV22NPC));
            } else { h.nonPartyMembersOffset = 0; h.nonPartyMembersCount = 0; }
        };

        auto write_variables = [&]() {
            if (!variablesBlob.empty()) {
                h.variablesOffset = cursor;
                write_blob(variablesBlob);
            } else { h.variablesOffset = 0; }
        };

        write_party();
        write_non_party();
        write_variables();
        // Write journal entries if present (fixed-size records)
        if (!journalEntries.empty()) {
            h.journalOffset = cursor;
            h.journalCount = journalEntries.size();
            const uint8_t* start = reinterpret_cast<const uint8_t*>(journalEntries.data());
            const uint8_t* end = start + journalEntries.size() * sizeof(GAMEV22JournalEntry);
            out.insert(out.end(), start, end);
            cursor += static_cast<uint32_t>(end - start);
        } else { h.journalOffset = 0; h.journalCount = 0; }

        std::memcpy(out.data(), &h, sizeof(GAMEV22Header));
        return out;
    }

    struct GAMMutableHeader : public GAMEV22Header {};
    GAMMutableHeader header{};
    std::vector<GAMEV22NPC> partyNPCs;
    std::vector<GAMEV22NPC> nonPartyNPCs;
    std::vector<uint8_t> variablesBlob;
    std::vector<GAMEV22JournalEntry> journalEntries;
};

} // namespace ProjectIE4k


