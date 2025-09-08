#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <cstddef>
#include <algorithm>

#include "core/Logging/Logging.h"

namespace ProjectIE4k {

#pragma pack(push, 1)

// Reference: https://burner1024.github.io/iesdp/file_formats/ie_formats/gam_v2.0.htm
struct GAMEV20Header {
    char     signature[4];                 // 0x0000 'GAME'
    char     version[4];                   // 0x0004 'V2.0'
    uint32_t gameTime;                     // 0x0008
    uint16_t selectedFormation;            // 0x000C
    uint16_t formationButton1;             // 0x000E
    uint16_t formationButton2;             // 0x0010
    uint16_t formationButton3;             // 0x0012
    uint16_t formationButton4;             // 0x0014
    uint16_t formationButton5;             // 0x0016
    uint32_t partyGold;                    // 0x0018
    uint16_t partyNpcCountExcludingProtagonist; // 0x001C
    uint16_t weatherBitfield;              // 0x001E
    uint32_t partyMembersOffset;           // 0x0020
    uint32_t partyMembersCountIncludingProtagonist; // 0x0024
    uint32_t partyInventoryOffset;         // 0x0028
    uint32_t partyInventoryCount;          // 0x002C
    uint32_t nonPartyMembersOffset;        // 0x0030
    uint32_t nonPartyMembersCount;         // 0x0034
    uint32_t variablesOffset;              // 0x0038
    uint32_t variablesCount;               // 0x003C
    char     mainArea[8];                  // 0x0040
    uint32_t familiarExtraOffset;          // 0x0048 (Offset to Familiar Extra)
    uint32_t journalCount;                 // 0x004C
    uint32_t journalOffset;                // 0x0050
    uint32_t partyReputation;              // 0x0054 (*10)
    char     currentArea[8];               // 0x0058
    uint32_t guiFlags;                     // 0x0060
    uint32_t loadingProgress;              // 0x0064
    uint32_t familiarInfoOffset;           // 0x0068
    uint32_t storedLocationsOffset;        // 0x006C
    uint32_t storedLocationsCount;         // 0x0070
    uint32_t gameTimeRealSeconds;          // 0x0074
    uint32_t pocketPlaneLocationsOffset;   // 0x0078
    uint32_t pocketPlaneLocationsCount;    // 0x007C
    uint8_t  unused_0080[52];              // 0x0080
};

struct GAMEV20Variable {
    char name[32];
    uint16_t type;
    uint16_t refValue;
    uint32_t dwordValue;
    uint32_t intValue;
    double doubleValue;
    char scriptNameValue[32];
};

struct GAMEV20JournalEntry {
    uint32_t textStrRef;
    uint32_t timeSeconds;
    uint8_t chapterNumber;
    uint8_t readByFlags;
    uint8_t sectionFlags;
    uint8_t locationFlag;
};

#pragma pack(pop)

// GAME V2.0 Character stats
#pragma pack(push, 1)
struct GAMEV20CharacterStats {
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

// GAME V2.0 NPCs (both in-party and out-of-party NPCs)
struct GAMEV20NPC {
    uint16_t characterSelection;          // 0x0000 Character selection (0=Not selected, 1=Selected, 0x8000=Dead)
    uint16_t partyOrder;                  // 0x0002 Party order (0x0-0x5 = PlayerXFill, 0xFFFF = not in party)
    uint32_t creOffset;                   // 0x0004 Offset (from start of file) to CRE resource data for this character
    uint32_t creSize;                     // 0x0008 Size of CRE resource data for this character
    char     characterName8[8];           // 0x000C Character Name (8 bytes)
    uint32_t orientation;                 // 0x0014 Character orientation
    char     currentArea[8];              // 0x0018 Characters current area
    uint16_t x;                           // 0x0020 Character X coordinate
    uint16_t y;                           // 0x0022 Character Y coordinate
    uint16_t viewX;                       // 0x0024 Viewing rectangle X coordinate
    uint16_t viewY;                       // 0x0026 Viewing rectangle Y coordinate
    uint16_t modalAction;                 // 0x0028 Modal action
    uint16_t happiness;                   // 0x002A Happiness
    uint32_t numTimesInteracted[23];      // 0x002C..0x0088 NumTimesInteracted NPC count (unused)
    uint16_t quickWeapon1;                // 0x008C Index into slots.ids for Quick Weapon 1 (0xFFFF = none)
    uint16_t quickWeapon2;                // 0x008E Index into slots.ids for Quick Weapon 2 (0xFFFF = none)
    uint16_t quickWeapon3;                // 0x0090 Index into slots.ids for Quick Weapon 3 (0xFFFF = none)
    uint16_t quickWeapon4;                // 0x0092 Index into slots.ids for Quick Weapon 4 (0xFFFF = none)
    uint16_t quickWeapon1Ability;         // 0x0094 Quick weapon slot 1 ability (0/1/2 or -1 disabled)
    uint16_t quickWeapon2Ability;         // 0x0096 Quick weapon slot 2 ability (0/1/2 or -1 disabled)
    uint16_t quickWeapon3Ability;         // 0x0098 Quick weapon slot 3 ability (0/1/2 or -1 disabled)
    uint16_t quickWeapon4Ability;         // 0x009A Quick weapon slot 4 ability (0/1/2 or -1 disabled)
    char     quickSpell1[8];              // 0x009C Quick Spell 1 resource
    char     quickSpell2[8];              // 0x00A4 Quick Spell 2 resource
    char     quickSpell3[8];              // 0x00AC Quick Spell 3 resource
    uint16_t quickItem1;                  // 0x00B4 Index into slots.ids for Quick Item 1 (0xFFFF = none)
    uint16_t quickItem2;                  // 0x00B6 Index into slots.ids for Quick Item 2 (0xFFFF = none)
    uint16_t quickItem3;                  // 0x00B8 Index into slots.ids for Quick Item 3 (0xFFFF = none)
    uint16_t quickItem1Ability;           // 0x00BA Quick Item 1 slot ability (0/1/2 or -1 disabled)
    uint16_t quickItem2Ability;           // 0x00BC Quick Item 2 slot ability (0/1/2 or -1 disabled)
    uint16_t quickItem3Ability;           // 0x00BE Quick Item 3 slot ability (0/1/2 or -1 disabled)
    char     name[32];                    // 0x00C0 Name (32 bytes)
    uint32_t talkCount;                   // 0x00E0 Talkcount
    char     characterStats[116];         // 0x00E4 Character stats for this character (116 bytes)
    char     voiceSet[8];                 // 0x0158 Voice Set (8 bytes)
    char     padding[4];                  // 0x0160 Padding to align struct size to 352 bytes
};

#pragma pack(pop)

// GAME V2.0 Familiar info
// Note: This structure maps caster alignment to CRE resrefs and per-level counts.
// The spec also references an offset to a follow-up familiar resources table.
#pragma pack(push, 1)
struct GAMEV20FamiliarInfo {
    // Alignment → Familiar resref map
    char lawfulGood[8];            // 0x0000
    char lawfulNeutral[8];         // 0x0008
    char lawfulEvil[8];            // 0x0010
    char neutralGood[8];           // 0x0018
    char neutralNeutral[8];        // 0x0020
    char neutralEvil[8];           // 0x0028
    char chaoticGood[8];           // 0x0030
    char chaoticNeutral[8];        // 0x0038
    char chaoticEvil[8];           // 0x0040
    uint32_t familiarResourcesOffset; // 0x0048 Offset to familiar resources

    // Per-alignment counts for levels 1..9
    uint32_t lgCounts[9];          // 0x004C..
    uint32_t lnCounts[9];          // ...
    uint32_t cgCounts[9];
    uint32_t ngCounts[9];
    uint32_t tnCounts[9];
    uint32_t neCounts[9];
    uint32_t leCounts[9];
    uint32_t cnCounts[9];
    uint32_t ceCounts[9];
};
#pragma pack(pop)

// GAME V2.0 Stored Locations info
#pragma pack(push, 1)
struct GAMEV20StoredLocation {
    char     area[8];     // 0x0000 Area resref
    uint16_t x;           // 0x0008 X coordinate
    uint16_t y;           // 0x000A Y coordinate
};
#pragma pack(pop)

// GAME V2.0 Pocket Plane info
#pragma pack(push, 1)
struct GAMEV20PocketPlaneLocation {
    char     area[8];     // 0x0000 Area resref
    uint16_t x;           // 0x0008 X coordinate
    uint16_t y;           // 0x000A Y coordinate
};
#pragma pack(pop)

// GAME V2.0 Familiar Extra table
#pragma pack(push, 1)
struct GAMEV20FamiliarExtraEntry {
    char resref[8];       // Familiar resref
};
#pragma pack(pop)

struct GAMV20File {
    // Deserialize binary into structured fields
    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(GAMEV20Header)) {
            Log(ERROR, "GAM", "Data too small for GAME V2.0 header");
            return false;
        }
        std::memcpy(&header, data.data(), sizeof(GAMEV20Header));
        if (std::string(header.signature, 4) != "GAME") {
            Log(ERROR, "GAM", "Invalid signature for GAME V2.x: got '{}'", std::string(header.signature, 4));
            return false;
        }
        const std::string ver(header.version, 4);
        if (!(ver == "V2.0" || ver == "V2.1")) {
            Log(ERROR, "GAM", "Invalid version for GAME V2.0: got '{}'", ver);
            return false;
        }
        Log(DEBUG, "GAM", "Successfully parsed GAM header - Version: {}, Party count: {}, Data size: {}", 
            ver, header.partyMembersCountIncludingProtagonist, data.size());
        Log(DEBUG, "GAM", "Section offsets - Party: {}, PartyInv: {}, NonParty: {} (count: {}), Variables: {} (count: {})", 
            header.partyMembersOffset, header.partyInventoryOffset, 
            header.nonPartyMembersOffset, header.nonPartyMembersCount,
            header.variablesOffset, header.variablesCount);

        auto nextOffsetAfter = [&](uint32_t base) -> uint32_t {
            std::vector<uint32_t> candidates;
            auto push = [&](uint32_t off){ if (off > base && off > 0 && off != 0xFFFFFFFF && off < data.size()) candidates.push_back(off); };
            push(header.nonPartyMembersOffset);
            push(header.variablesOffset);
            push(header.journalOffset);
            push(header.familiarInfoOffset);
            push(header.familiarExtraOffset);
            push(header.storedLocationsOffset);
            push(header.pocketPlaneLocationsOffset);
            std::sort(candidates.begin(), candidates.end());
            return candidates.empty() ? static_cast<uint32_t>(data.size()) : candidates.front();
        };

        partyNPCs.clear();
        partyCreBlobs.clear();
        if (header.partyMembersOffset > 0 && header.partyMembersCountIncludingProtagonist > 0) {
            uint32_t count = header.partyMembersCountIncludingProtagonist;
            uint64_t need = static_cast<uint64_t>(header.partyMembersOffset) + static_cast<uint64_t>(count) * sizeof(GAMEV20NPC);
            if (need > data.size()) {
                Log(ERROR, "GAM", "V2.0 party NPC block too small for expected count");
                return false;
            }
            partyNPCs.resize(count);
            std::memcpy(partyNPCs.data(), data.data() + header.partyMembersOffset, count * sizeof(GAMEV20NPC));
            // Capture party CRE blobs
            partyCreBlobs.resize(count);
            for (uint32_t i = 0; i < count; i++) {
                const auto &npc = partyNPCs[i];
                uint64_t start = npc.creOffset;
                uint64_t end = start + npc.creSize;
                if (npc.creSize > 0 && end <= data.size()) {
                    partyCreBlobs[i].assign(data.begin() + start, data.begin() + end);
                } else {
                    partyCreBlobs[i].clear();
                }
            }
        }

        nonPartyNPCs.clear();
        nonPartyCreBlobs.clear();
        if (header.nonPartyMembersOffset > 0 && header.nonPartyMembersCount > 0) {
            Log(DEBUG, "GAM", "Loading {} non-party NPCs from offset {}", header.nonPartyMembersCount, header.nonPartyMembersOffset);
            uint32_t count = header.nonPartyMembersCount;
            uint64_t need = static_cast<uint64_t>(header.nonPartyMembersOffset) + static_cast<uint64_t>(count) * sizeof(GAMEV20NPC);
            if (need > data.size()) {
                Log(ERROR, "GAM", "V2.0 non-party NPC block too small for expected count");
                return false;
            }
            nonPartyNPCs.resize(count);
            std::memcpy(nonPartyNPCs.data(), data.data() + header.nonPartyMembersOffset, count * sizeof(GAMEV20NPC));
            Log(DEBUG, "GAM", "Loaded {} non-party NPC headers", nonPartyNPCs.size());
            Log(DEBUG, "GAM", "GAMEV20NPC struct size: {} bytes", sizeof(GAMEV20NPC));
            // Capture non-party CRE blobs
            nonPartyCreBlobs.resize(count);
            for (uint32_t i = 0; i < count; i++) {
                const auto &npc = nonPartyNPCs[i];
                uint64_t start = npc.creOffset;
                uint64_t end = start + npc.creSize;
                if (npc.creSize > 0 && end <= data.size()) {
                    nonPartyCreBlobs[i].assign(data.begin() + start, data.begin() + end);
                    std::string charName(npc.characterName8, 8);
                    Log(DEBUG, "GAM", "Non-party NPC {} '{}': CRE blob {} bytes at offset {}", 
                        i, charName, npc.creSize, npc.creOffset);
                } else {
                    nonPartyCreBlobs[i].clear();
                    std::string charName(npc.characterName8, 8);
                    Log(DEBUG, "GAM", "Non-party NPC {} '{}': no CRE data (size={}, offset={})", 
                        i, charName, npc.creSize, npc.creOffset);
                }
            }
        }

        // Party Inventory
        partyInventoryBlob.clear();
        if (header.partyInventoryOffset > 0 && header.partyInventoryCount > 0) {
            uint32_t end = nextOffsetAfter(header.partyInventoryOffset);
            if (end <= header.partyInventoryOffset || end > data.size()) {
                Log(ERROR, "GAM", "Invalid party inventory section: offset={}, end={}, data_size={}", 
                    header.partyInventoryOffset, end, data.size());
                return false;
            }
            partyInventoryBlob.assign(data.begin() + header.partyInventoryOffset, data.begin() + end);
            Log(DEBUG, "GAM", "Loaded party inventory blob: {} bytes", partyInventoryBlob.size());
        }

        variablesBlob.clear();
        if (header.variablesOffset > 0 && header.variablesCount > 0) {
            uint32_t end = nextOffsetAfter(header.variablesOffset);
            if (end <= header.variablesOffset || end > data.size()) {
                Log(ERROR, "GAM", "Invalid variables section: offset={}, end={}, data_size={}", 
                    header.variablesOffset, end, data.size());
                return false;
            }
            variablesBlob.assign(data.begin() + header.variablesOffset, data.begin() + end);
            Log(DEBUG, "GAM", "Loaded variables blob: {} bytes", variablesBlob.size());
        }

        // Familiar Info
        familiarInfoBlob.clear();
        if (header.familiarInfoOffset > 0 && header.familiarInfoOffset != 0xFFFFFFFF) {
            uint32_t end = nextOffsetAfter(header.familiarInfoOffset);
            if (end <= header.familiarInfoOffset || end > data.size()) {
                Log(ERROR, "GAM", "Invalid familiar info section: offset={}, end={}, data_size={}", 
                    header.familiarInfoOffset, end, data.size());
                return false;
            }
            familiarInfoBlob.assign(data.begin() + header.familiarInfoOffset, data.begin() + end);
            Log(DEBUG, "GAM", "Loaded familiar info blob: {} bytes", familiarInfoBlob.size());
        }

        // Familiar Extra
        familiarExtraBlob.clear();
        if (header.familiarExtraOffset > 0 && header.familiarExtraOffset != 0xFFFFFFFF) {
            uint32_t end = nextOffsetAfter(header.familiarExtraOffset);
            if (end <= header.familiarExtraOffset || end > data.size()) {
                Log(ERROR, "GAM", "Invalid familiar extra section: offset={}, end={}, data_size={}", 
                    header.familiarExtraOffset, end, data.size());
                return false;
            }
            familiarExtraBlob.assign(data.begin() + header.familiarExtraOffset, data.begin() + end);
            Log(DEBUG, "GAM", "Loaded familiar extra blob: {} bytes", familiarExtraBlob.size());
        }

        journalEntries.clear();
        if (header.journalOffset > 0 && header.journalCount > 0) {
            uint64_t needed = static_cast<uint64_t>(header.journalOffset) + static_cast<uint64_t>(header.journalCount) * sizeof(GAMEV20JournalEntry);
            if (needed > data.size()) {
                Log(ERROR, "GAM", "Journal section exceeds file size (V2.0)");
                return false;
            }
            journalEntries.resize(header.journalCount);
            std::memcpy(journalEntries.data(), data.data() + header.journalOffset, header.journalCount * sizeof(GAMEV20JournalEntry));
        }

        // Stored Locations
        storedLocations.clear();
        if (header.storedLocationsOffset && header.storedLocationsCount) {
            uint64_t need = static_cast<uint64_t>(header.storedLocationsOffset) + static_cast<uint64_t>(header.storedLocationsCount) * sizeof(GAMEV20StoredLocation);
            if (need <= data.size()) {
                storedLocations.resize(header.storedLocationsCount);
                std::memcpy(storedLocations.data(), data.data() + header.storedLocationsOffset,
                            header.storedLocationsCount * sizeof(GAMEV20StoredLocation));
            }
        }

        // Pocket Plane Locations
        pocketPlaneLocations.clear();
        if (header.pocketPlaneLocationsOffset && header.pocketPlaneLocationsCount) {
            uint64_t need = static_cast<uint64_t>(header.pocketPlaneLocationsOffset) + static_cast<uint64_t>(header.pocketPlaneLocationsCount) * sizeof(GAMEV20PocketPlaneLocation);
            if (need <= data.size()) {
                pocketPlaneLocations.resize(header.pocketPlaneLocationsCount);
                std::memcpy(pocketPlaneLocations.data(), data.data() + header.pocketPlaneLocationsOffset,
                            header.pocketPlaneLocationsCount * sizeof(GAMEV20PocketPlaneLocation));
            }
        }

        Log(DEBUG, "GAM", "Successfully deserialized GAM V2.0 file");
        return true;
    }

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> out;
        out.resize(sizeof(GAMEV20Header));
        GAMMutableHeader h = header;
        uint32_t cursor = sizeof(GAMEV20Header);

        auto write_blob = [&](const std::vector<uint8_t>& blob) {
            out.insert(out.end(), blob.begin(), blob.end());
            cursor += blob.size();
        };

        auto write_party = [&]() {
            if (!partyNPCs.empty()) {
                h.partyMembersOffset = cursor;
                h.partyMembersCountIncludingProtagonist = static_cast<uint32_t>(partyNPCs.size());
                // Compute where CRE blobs will start
                uint32_t creStart = cursor + static_cast<uint32_t>(partyNPCs.size() * sizeof(GAMEV20NPC));
                uint32_t running = 0;
                std::vector<GAMEV20NPC> tmp = partyNPCs;
                for (size_t i = 0; i < tmp.size(); i++) {
                    uint32_t blobSize = (i < partyCreBlobs.size()) ? static_cast<uint32_t>(partyCreBlobs[i].size()) : 0;
                    tmp[i].creSize = blobSize;
                    tmp[i].creOffset = blobSize ? (creStart + running) : 0;
                    running += blobSize;
                }
                const uint8_t* start = reinterpret_cast<const uint8_t*>(tmp.data());
                out.insert(out.end(), start, start + tmp.size() * sizeof(GAMEV20NPC));
                cursor = creStart;
                for (size_t i = 0; i < partyCreBlobs.size(); i++) {
                    out.insert(out.end(), partyCreBlobs[i].begin(), partyCreBlobs[i].end());
                }
                cursor = static_cast<uint32_t>(out.size());
            } else { h.partyMembersOffset = 0; h.partyMembersCountIncludingProtagonist = 0; }
        };

        auto write_non_party = [&]() {
            if (!nonPartyNPCs.empty()) {
                h.nonPartyMembersOffset = cursor;
                h.nonPartyMembersCount = static_cast<uint32_t>(nonPartyNPCs.size());
                uint32_t creStart = cursor + static_cast<uint32_t>(nonPartyNPCs.size() * sizeof(GAMEV20NPC));
                uint32_t running = 0;
                std::vector<GAMEV20NPC> tmp = nonPartyNPCs;
                Log(DEBUG, "GAM", "Serialize: Writing {} non-party NPCs at offset {}", tmp.size(), cursor);
                if (!tmp.empty()) {
                    std::string charName0(tmp[0].characterName8, 8);
                    Log(DEBUG, "GAM", "Serialize: First NPC name: '{}' x={} y={}", charName0, tmp[0].x, tmp[0].y);
                }
                for (size_t i = 0; i < tmp.size(); i++) {
                    uint32_t blobSize = (i < nonPartyCreBlobs.size()) ? static_cast<uint32_t>(nonPartyCreBlobs[i].size()) : 0;
                    if (blobSize > 0) {
                        // This NPC has embedded CRE data, update offsets
                        tmp[i].creSize = blobSize;
                        tmp[i].creOffset = creStart + running;
                        running += blobSize;
                    }
                    // If blobSize == 0, preserve original creSize and creOffset (don't modify)
                }
                const uint8_t* start = reinterpret_cast<const uint8_t*>(tmp.data());
                out.insert(out.end(), start, start + tmp.size() * sizeof(GAMEV20NPC));
                cursor = creStart;
                for (size_t i = 0; i < nonPartyCreBlobs.size(); i++) {
                    out.insert(out.end(), nonPartyCreBlobs[i].begin(), nonPartyCreBlobs[i].end());
                }
                cursor = static_cast<uint32_t>(out.size());
            } else { h.nonPartyMembersOffset = 0; h.nonPartyMembersCount = 0; }
        };

        auto write_variables = [&]() {
            if (!variablesBlob.empty()) {
                h.variablesOffset = cursor;
                write_blob(variablesBlob);
            } else { h.variablesOffset = 0; }
        };

        auto write_journal = [&]() {
            if (!journalEntries.empty()) {
                h.journalOffset = cursor;
                h.journalCount = journalEntries.size();
                const uint8_t* start = reinterpret_cast<const uint8_t*>(journalEntries.data());
                const uint8_t* end = start + journalEntries.size() * sizeof(GAMEV20JournalEntry);
                out.insert(out.end(), start, end);
                cursor += static_cast<uint32_t>(end - start);
            } else { h.journalOffset = 0; h.journalCount = 0; }
        };

        auto write_party_inventory = [&]() {
            if (!partyInventoryBlob.empty() && !std::all_of(partyInventoryBlob.begin(), partyInventoryBlob.end(), [](uint8_t b) { return b == 0; })) {
                h.partyInventoryOffset = cursor;
                write_blob(partyInventoryBlob);
            } else { 
                h.partyInventoryOffset = 0; 
                h.partyInventoryCount = 0;
            }
        };

        auto write_familiar_info = [&]() {
            if (!familiarInfoBlob.empty()) {
                h.familiarInfoOffset = cursor;
                write_blob(familiarInfoBlob);
            } else { h.familiarInfoOffset = 0; }
        };

        auto write_familiar_extra = [&]() {
            if (!familiarExtraBlob.empty()) {
                h.familiarExtraOffset = cursor;
                write_blob(familiarExtraBlob);
            } else { h.familiarExtraOffset = 0; }
        };

        write_non_party();
        write_variables();
        write_journal();
        write_familiar_info();
        write_familiar_extra();
        write_party();
        write_party_inventory();

        // Stored Locations
        if (!storedLocations.empty()) {
            h.storedLocationsOffset = cursor;
            h.storedLocationsCount = storedLocations.size();
            const uint8_t *start = reinterpret_cast<const uint8_t*>(storedLocations.data());
            const uint8_t *end = start + storedLocations.size() * sizeof(GAMEV20StoredLocation);
            out.insert(out.end(), start, end);
            cursor += static_cast<uint32_t>(end - start);
        } else { h.storedLocationsOffset = 0; h.storedLocationsCount = 0; }

        // Pocket Plane Locations
        if (!pocketPlaneLocations.empty()) {
            h.pocketPlaneLocationsOffset = cursor;
            h.pocketPlaneLocationsCount = pocketPlaneLocations.size();
            const uint8_t *start = reinterpret_cast<const uint8_t*>(pocketPlaneLocations.data());
            const uint8_t *end = start + pocketPlaneLocations.size() * sizeof(GAMEV20PocketPlaneLocation);
            out.insert(out.end(), start, end);
            cursor += static_cast<uint32_t>(end - start);
        } else { h.pocketPlaneLocationsOffset = 0; h.pocketPlaneLocationsCount = 0; }

        std::memcpy(out.data(), &h, sizeof(GAMEV20Header));
        return out;
    }

    struct GAMMutableHeader : public GAMEV20Header {};
    GAMMutableHeader header{};
    std::vector<GAMEV20NPC> partyNPCs;
    std::vector<std::vector<uint8_t>> partyCreBlobs;
    std::vector<uint8_t> partyInventoryBlob;
    std::vector<GAMEV20NPC> nonPartyNPCs;
    std::vector<std::vector<uint8_t>> nonPartyCreBlobs;
    std::vector<uint8_t> variablesBlob;
    std::vector<GAMEV20JournalEntry> journalEntries;
    std::vector<uint8_t> familiarInfoBlob;
    std::vector<uint8_t> familiarExtraBlob;
    std::vector<GAMEV20StoredLocation> storedLocations;
    std::vector<GAMEV20PocketPlaneLocation> pocketPlaneLocations;
};

} // namespace ProjectIE4k
