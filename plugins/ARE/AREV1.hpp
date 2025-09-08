#ifndef AREV1_HPP
#define AREV1_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>

#include "core/Logging/Logging.h"
#include "core/CFG.h"

namespace ProjectIE4k {

#pragma pack(push, 1)

struct AREHeader {
    char signature[4];              // 0x0000 Signature ('AREA')
    char version[4];                // 0x0004 Version ('V1.0')
    char areaWED[8];                // 0x0008 Area WED resref
    uint32_t lastSaved;             // 0x0010 Last saved (seconds, real time)
    uint32_t areaFlags;             // 0x0014 Area flags (AREAFLAG.IDS)
                                     // BG1/BG2/IWD: bit 0: Save not allowed
                                     //              bit 1: Tutorial area (not BG1)
                                     //              bit 2: Dead magic zone
                                     //              bit 3: Dream
                                     // BGEE:       bit 0: Save not allowed
                                     //              bit 1: Tutorial area
                                     //              bit 2: Dead magic zone
                                     //              bit 3: Dream
                                     //              bit 4: Player1 death does not end the game
                                     //              bit 5: Resting not allowed
                                     //              bit 6: Travel not allowed
                                     // PST:        bit 0: Save not allowed
                                     //              bit 1: "You cannot rest here."
                                     //              bit 2: "Too dangerous to rest."
                                     //              bit 1+2: "You must obtain permission to rest here."
                                     // PSTEE:      bit 0: Save not allowed
                                     //              bit 1: Unknown
                                     //              bit 2: Dead magic zone
                                     //              bit 3: Dream
                                     //              bit 4: Player1 death does not end the game
                                     //              bit 5: Resting not allowed
                                     //              bit 6: Travel not allowed
                                     //              bit 7: "You cannot rest here."
                                     //              bit 8: "Too dangerous to rest."
                                     //              bit 7+8: "You must obtain permission to rest here."
    char northArea[8];             // 0x0018 North area resref
    uint32_t northFlags;           // 0x0020 North area flags
                                     // bit 0: Party Required
                                     // bit 1: Party Enabled
    char eastArea[8];              // 0x0024 East area resref
    uint32_t eastFlags;            // 0x002c East area flags
                                     // bit 0: Party Required
                                     // bit 1: Party Enabled
    char southArea[8];             // 0x0030 South area resref
    uint32_t southFlags;           // 0x0038 South area flags
                                     // bit 0: Party Required
                                     // bit 1: Party Enabled
    char westArea[8];              // 0x003c West area resref
    uint32_t westFlags;            // 0x0044 West area flags
                                     // bit 0: Party Required
                                     // bit 1: Party Enabled
    uint16_t areaTypeFlags;        // 0x0048 Area type flags (AREATYPE.IDS)
                                     // BG1/BG2/IWD/BGEE: bit 0: Outdoor
                                     //                   bit 1: Day/night
                                     //                   bit 2: Weather
                                     //                   bit 3: City
                                     //                   bit 4: Forest
                                     //                   bit 5: Dungeon
                                     //                   bit 6: Extended night
                                     //                   bit 7: Can rest indoors
                                     // PST/PSTEE:        bit 0: Hive
                                     //                   bit 1: Hive night?
                                     //                   bit 2: Clerk's Ward
                                     //                   bit 3: Lower Ward
                                     //                   bit 4: Ravel's Maze
                                     //                   bit 5: Baator
                                     //                   bit 6: Rubikon
                                     //                   bit 7: Fortress of Regrets
                                     //                   bit 8: Curst
                                     //                   bit 9: Carceri
                                     //                   bit 10: Outdoors
    uint16_t rainProb;             // 0x004a Rain probability
    uint16_t snowProb;             // 0x004c Snow probability
    // Game-specific weather fields
    union {
        struct {
            // BG1, BG2, IWD, PST: No fog probability field
            uint16_t lightningProb;        // 0x004e Lightning probability
            uint16_t windSpeed;            // 0x0050 Wind speed (BG1/BG2/IWD) / unused (PST)
        } standard;
        struct {
            // BGEE: Has fog probability field
            uint16_t fogProb;              // 0x004e Fog probability (BGEE only)
            uint16_t lightningProb;        // 0x0050 Lightning probability
            uint8_t overlayTransparency;   // 0x0052 Overlay transparency (BGEE, first byte only)
            uint8_t unused_weather;        // 0x0053 Unused (second byte)
        } bgee;
    } weather;
    uint32_t actorsOffset;              // 0x0054 Offset to actors
    uint16_t actorsCount;               // 0x0058 Count of actors
    uint16_t regionsCount;              // 0x005a Count of regions
    uint32_t regionsOffset;             // 0x005c Offset to regions
    uint32_t spawnPointsOffset;         // 0x0060 Offset to spawn points
    uint32_t spawnPointsCount;          // 0x0064 Count of spawn points
    uint32_t entrancesOffset;           // 0x0068 Offset to entrances
    uint32_t entrancesCount;            // 0x006c Count of entrances
    uint32_t containersOffset;          // 0x0070 Offset to containers
    uint16_t containersCount;           // 0x0074 Count of containers
    uint16_t itemsCount;                // 0x0076 Count of items
    uint32_t itemsOffset;               // 0x0078 Offset to items
    uint32_t verticesOffset;            // 0x007c Offset to vertices
    uint16_t verticesCount;             // 0x0080 Count of vertices
    uint16_t ambientsCount;             // 0x0082 Count of ambients
    uint32_t ambientsOffset;            // 0x0084 Offset to ambients
    uint32_t variablesOffset;           // 0x0088 Offset to variables
    uint32_t variablesCount;            // 0x008c Count of variables
    uint16_t tiledObjectFlagsOffset;    // 0x0090 Offset to tiled object flags
    uint16_t tiledObjectFlagsCount;     // 0x0092 Count of tiled object flags
    char areaScript[8];                 // 0x0094 Area script resref
    uint32_t exploredBitmaskSize;       // 0x009c Size of explored bitmask
    uint32_t exploredBitmaskOffset;     // 0x00a0 Offset to explored bitmask
    uint32_t doorsCount;                // 0x00a4 Count of doors
    uint32_t doorsOffset;               // 0x00a8 Offset to doors
    uint32_t animationsCount;           // 0x00ac Count of animations
    uint32_t animationsOffset;          // 0x00b0 Offset to animations
    uint32_t tiledObjectsCount;         // 0x00b4 Count of tiled objects
    uint32_t tiledObjectsOffset;        // 0x00b8 Offset to tiled objects
    uint32_t songEntriesOffset;         // 0x00bc Offset to song entries
    uint32_t restInterruptionsOffset;   // 0x00c0 Offset to rest interruptions
    // Game-specific tail (keep memory layout explicit and variant-safe)
    union {
        struct {
            // Standard format (BG1, BG2, IWD, BGEE)
            uint32_t automapNoteOffset;        // 0x00c4 PST: 0xFFFFFFFF, Other: Offset of automap notes
            uint32_t automapNoteCount;         // 0x00c8 PST: Offset of automap notes, Other: Count of automap notes
            uint32_t projectileTrapsOffset;    // 0x00cc PST: Number of automap notes, Other: Offset to projectile traps
            uint32_t projectileTrapsCount;     // 0x00d0 PST: Offset to projectile traps, Other: Count of projectile traps
            char restMovieDay[8];              // 0x00d4 BG2:ToB, BGEE - Rest movie (day), Others - Unknown
            char restMovieNight[8];            // 0x00dc BG2:ToB, BGEE - Rest movie (night), Others - Unknown
            char unused[56];                   // 0x00e4 Unused
        } standard;
        struct {
            // PST-specific format
            uint32_t specialValue;             // 0x00c4 0xFFFFFFFF (special PST value)
            uint32_t automapNoteOffset;        // 0x00c8 Offset of automap notes
            uint32_t automapNoteCount;         // 0x00cc Number of automap notes
            uint32_t projectileTrapsOffset;    // 0x00d0 Offset to projectile traps
            uint32_t projectileTrapsCount;     // 0x00d4 Number of projectile traps
            char unused[64];                   // 0x00d8 Unused
        } pst;
    } gameSpecific;
};

struct AREActor {
    char name[32];                                    // 0x0000 Name
    uint16_t currentX;                                // 0x0020 Current X coordinate
    uint16_t currentY;                                // 0x0022 Current Y coordinate
    uint16_t destX;                                   // 0x0024 Destination X coordinate
    uint16_t destY;                                   // 0x0026 Destination Y coordinate
    uint32_t flags;                                   // 0x0028 Flags
                                                     // bit 0: CRE attached (0=yes, 1=no)
                                                     // bit 1: Has Seen Party
                                                     // bit 2: Invulnerable
                                                     // bit 3: Override script name
    uint16_t hasBeenSpawned;                         // 0x002c Has been spawned (0=no, 1=yes)
    uint8_t creResrefFirstLetter;                    // 0x002e First letter of CRE resref (changed to *)
    uint8_t unused1;                                 // 0x002f Unused
    uint32_t actorAnimation;                         // 0x0030 Actor animation
    uint16_t actorOrientation;                       // 0x0034 Actor orientation
    uint16_t unused2;                                // 0x0036 Unused
    uint32_t actorRemovalTimer;                      // 0x0038 Actor removal timer (seconds, usu. -1 to avoid removal)
    uint16_t movementRestrictionDistance;            // 0x003c Movement restriction distance
    uint16_t movementRestrictionDistanceMoveToObject; // 0x003e Movement restriction distance (move to object)
    uint32_t actorAppearanceSchedule;                // 0x0040 Actor appearance schedule (bits 0-23 = hours)
                                                     // Hours offset by 30 minutes:
                                                     // bit 23 = 23:30 to 00:29
                                                     // bit 22 = 22:30 to 23:29
                                                     // bit 21 = 21:30 to 22:29 (Night)
                                                     // bit 20 = 20:30 to 21:29 (Dusk)
                                                     // bit 19 = 19:30 to 20:29
                                                     // bit 18 = 18:30 to 19:29
                                                     // bit 17 = 17:30 to 18:29
                                                     // bit 16 = 16:30 to 17:29
                                                     // bit 15 = 15:30 to 16:29
                                                     // bit 14 = 14:30 to 15:29
                                                     // bit 13 = 13:30 to 14:29
                                                     // bit 12 = 12:30 to 13:29
                                                     // bit 11 = 11:30 to 12:29
                                                     // bit 10 = 10:30 to 11:29
                                                     // bit 9 = 09:30 to 10:29
                                                     // bit 8 = 08:30 to 09:29
                                                     // bit 7 = 07:30 to 08:29
                                                     // bit 6 = 06:30 to 07:29 (Day)
                                                     // bit 5 = 05:30 to 06:29 (Dawn)
                                                     // bit 4 = 04:30 to 05:29
                                                     // bit 3 = 03:30 to 04:29
                                                     // bit 2 = 02:30 to 03:29
                                                     // bit 1 = 01:30 to 02:29
                                                     // bit 0 = 00:30 to 01:29
    uint32_t numTimesTalkedTo;                       // 0x0044 NumTimesTalkedTo (in SAV files)
    char dialog[8];                                  // 0x0048 Dialog resref
    char scriptOverride[8];                          // 0x0050 Script (Override) resref
    char scriptGeneral[8];                           // 0x0058 Script (General) resref
    char scriptClass[8];                             // 0x0060 Script (Class) resref
    char scriptRace[8];                              // 0x0068 Script (Race) resref
    char scriptDefault[8];                           // 0x0070 Script (Default) resref
    char scriptSpecific[8];                          // 0x0078 Script (Specific) resref
    char creFile[8];                                 // 0x0080 CRE file resref
    uint32_t creOffset;                              // 0x0088 Offset to CRE structure (for embedded CRE files)
    uint32_t creSize;                                // 0x008c Size of stored CRE structure
    char unused3[128];                               // 0x0090 Unused (128 bytes)
};

struct ARERegion {
    char name[32];                        // 0x0000 Name
    uint16_t regionType;                  // 0x0020 Region type
                                          // 0: Proximity trigger
                                          // 1: Info point
                                          // 2: Travel region
    int16_t boundingBox[4];               // 0x0022 Minimum bounding box [left,top,right,bottom]
    uint16_t verticesCount;               // 0x002a Count of vertices composing the perimeter
    uint32_t verticesIndex;               // 0x002c Index of first vertex for this region
    uint32_t triggerValue;                // 0x0030 Trigger value
    uint32_t cursorIndex;                 // 0x0034 Cursor index (cursors.bam)
    char destArea[8];                     // 0x0038 Destination area (for travel regions)
    char entranceName[32];                // 0x0040 Entrance name in destination area (for travel regions)
    uint32_t flags;                       // 0x0060 Flags
                                          // bit 0: Key required
                                          // bit 1: Reset trap (for proximity triggers)
                                          // bit 2: Party Required flag (for travel triggers)
                                          // bit 3: Detectable
                                          // bit 4: NPC activates
                                          // bit 5: Active in tutorial area only
                                          // bit 6: Anyone activates
                                          // bit 7: No string
                                          // bit 8: Deactivated (for proximity triggers)
                                          // bit 9: Party only
                                          // bit 10: Alternative point
                                          // bit 11: Door closed
    uint32_t infoText;                    // 0x0064 Information text (strref, for info points)
    uint16_t trapDetectionDifficulty;     // 0x0068 Trap detection difficulty (%)
                                          // Note: 100 or higher prevents detection
    uint16_t trapRemovalDifficulty;       // 0x006a Trap removal difficulty (%)
    uint16_t isTrapped;                   // 0x006c Region is trapped (0=No, 1=Yes)
    uint16_t trapDetected;                // 0x006e Trap detected (0=No, 1=Yes)
    uint16_t trapLaunchX;                 // 0x0070 Trap launch location X coordinate
    uint16_t trapLaunchY;                 // 0x0072 Trap launch location Y coordinate
    char keyItem[8];                      // 0x0074 Key item (resref)
    char regionScript[8];                 // 0x007c Region script (resref)
    uint16_t altUsePointX;                // 0x0084 Alternative use point X coordinate
    uint16_t altUsePointY;                // 0x0086 Alternative use point Y coordinate
    uint32_t unknown1;                    // 0x0088 Unknown
    char unknown2[32];                    // 0x008c Unknown
    char sound[8];                        // 0x00ac Sound (PST, PSTEE)
    uint16_t talkLocationPointX;          // 0x00b4 Talk location point X coordinate (PST, PSTEE)
    uint16_t talkLocationPointY;          // 0x00b6 Talk location point Y coordinate (PST, PSTEE)
    uint32_t speakerName;                 // 0x00b8 Speaker name (strref, PST, PSTEE)
    char dialogFile[8];                   // 0x00bc Dialog file (resref, PST, PSTEE)
};

struct ARESpawnPoint {
    char name[32];                                    // 0x0000 Name
    uint16_t x;                                        // 0x0020 X coordinate
    uint16_t y;                                        // 0x0022 Y coordinate
    char creatureToSpawn[10][8];                       // 0x0024-0x006c Creature resrefs (10 slots)
    uint16_t countOfSpawnCreatures;                    // 0x0074 Count of spawn creatures
    uint16_t baseCreatureNumberToSpawn;                // 0x0076 Base creature number to spawn
                                                       // Actual: (Frequency * AvgPartyLevel) / CreaturePower
    uint16_t frequency;                                // 0x0078 Frequency (seconds between spawning)
    uint16_t spawnMethod;                              // 0x007a Spawn method
                                                       // bit 0: If bit 2 set, don't spawn
                                                       // bit 1: One-time spawnpoint
                                                       // bit 2: Temporarily disabled
    uint32_t actorRemovalTimer;                        // 0x007c Actor removal timer (seconds, -1 = permanent)
    uint16_t movementRestrictionDistance;              // 0x0080 Movement restriction distance
    uint16_t movementRestrictionDistanceMoveToObject;  // 0x0082 Movement restriction distance (move to object)
    uint16_t maxCreaturesToSpawn;                      // 0x0084 Maximum creatures to spawn
    uint16_t spawnPointEnabled;                        // 0x0086 Spawn point enabled (0=Inactive, 1=Active)
    uint32_t spawnPointAppearanceSchedule;             // 0x0088 Appearance schedule (bits 0-23 = hours)
                                                       // Hours offset by 30 minutes:
                                                       // bit 23 = 23:30 to 00:29
                                                       // bit 22 = 22:30 to 23:29
                                                       // bit 21 = 21:30 to 22:29 (Night)
                                                       // bit 20 = 20:30 to 21:29 (Dusk)
                                                       // ... (same hour mapping as actors)
    uint16_t probabilityDay;                           // 0x008c Probability (day)
    uint16_t probabilityNight;                         // 0x008e Probability (night)
    union {
        struct {
            char unused[56];                           // 0x0090 Unused (BG1, BG2, PST, IWD)
        } bg;
        struct {
            uint32_t spawnFrequency;                   // 0x0090 Spawn frequency (BGEE)
            uint32_t countdown;                        // 0x0094 Countdown (BGEE)
            uint8_t spawnWeight[10];                   // 0x0098-0x00a1 Spawn weights for 10 creature slots (BGEE)
            char unused[38];                           // 0x00a2 Unused (BGEE)
        } bgee;
    };
};

struct AREEntrance {
    char name[32];
    uint16_t x;
    uint16_t y;
    uint16_t orientation;
    char unused[66];
};

struct AREContainer {
    char name[32];                        // 0x0000 Name
                                           // Note: If starts with T2, loads 2da file with same name
    uint16_t x;                           // 0x0020 X coordinate
    uint16_t y;                           // 0x0022 Y coordinate
    uint16_t containerType;               // 0x0024 Container type
                                          // 0x00: <n/a>
                                          // 0x01: Bag
                                          // 0x02: Chest
                                          // 0x03: Drawer
                                          // 0x04: Pile
                                          // 0x05: Table
                                          // 0x06: Shelf
                                          // 0x07: Altar
                                          // 0x08: Nonvisible
                                          // 0x09: Spellbook
                                          // 0x0a: Body
                                          // 0x0b: Barrel
                                          // 0x0c: Crate
    uint16_t lockDifficulty;              // 0x0026 Lock difficulty (0-100)
    uint32_t flags;                       // 0x0028 Flags
                                          // bit 0: Locked
                                          // bit 1: Disable if no owner
                                          // bit 2: Magically locked
                                          // bit 3: Trap resets
                                          // bit 4: Remove only
                                          // bit 5: Disabled
    uint16_t trapDetectionDifficulty;     // 0x002c Trap detection difficulty (0-100)
    uint16_t trapRemovalDifficulty;       // 0x002e Trap removal difficulty (0-100)
    uint16_t isTrapped;                   // 0x0030 Container is trapped (0=No, 1=Yes)
    uint16_t trapDetected;                // 0x0032 Trap detected (0=No, 1=Yes)
    uint16_t trapLaunchX;                 // 0x0034 Trap launch X coordinate
    uint16_t trapLaunchY;                 // 0x0036 Trap launch Y coordinate
    int16_t boundingBox[4];               // 0x0038 Bounding box [left, top, right, bottom]
    uint32_t itemsIndex;                  // 0x0040 Index of first item in this container
    uint32_t itemsCount;                  // 0x0044 Count of items in this container
    char trapScript[8];                   // 0x0048 Trap script (resref)
    uint32_t verticesIndex;               // 0x0050 Index of first vertex for container outline
    uint16_t verticesCount;               // 0x0054 Count of vertices for container outline
    uint16_t triggerRange;                // 0x0056 Trigger range
    char owner[32];                       // 0x0058 Owner (script name)
    char keyItem[8];                      // 0x0078 Key item (resref)
    uint32_t breakDifficulty;             // 0x0080 Break difficulty
    uint32_t lockpickString;              // 0x0084 Lockpick string (strref)
    char unused[56];                      // 0x0088 Unused
};

struct AREItem {
    char resref[8];            // 0x0000 Item resref
    uint16_t expirationTime;   // 0x0008 Item expiration time (replace with drained item)
    uint16_t charges1;         // 0x000a Quantity/Charges 1
    uint16_t charges2;         // 0x000c Quantity/Charges 2
    uint16_t charges3;         // 0x000e Quantity/Charges 3
    uint32_t flags;            // 0x0010 Flags
                               // bit 0: Identified
                               // bit 1: Unstealable
                               // bit 2: Stolen
                               // bit 3: Undroppable
};

struct AREVertex {
    int16_t x;
    int16_t y;
};

struct AREAmbient {
    char name[32];                       // 0x0000 Name
    uint16_t x;                          // 0x0020 X coordinate
    uint16_t y;                          // 0x0022 Y coordinate
    uint16_t radius;                     // 0x0024 Radius
    uint16_t height;                     // 0x0026 Height
    uint32_t pitchVariance;              // 0x0028 Pitch variance
    uint16_t volumeVariance;             // 0x002c Volume variance
    uint16_t volume;                     // 0x002e Volume (%)
    char sounds[10][8];                  // 0x0030-0x0078 Sound resrefs (up to 10)
    uint16_t countOfSounds;              // 0x0080 Count of sounds
    uint16_t unused1;                    // 0x0082 Unused
    uint32_t baseTime;                   // 0x0084 Base time (seconds) between sounds
    uint32_t baseTimeDeviation;          // 0x0088 Base time deviation
                                         // Time uses uniform distribution over (baseTime - deviation) to (baseTime + deviation)
    uint32_t ambientAppearanceSchedule;  // 0x008c Appearance schedule (bits 0-23 = hours)
                                         // Hours offset by 30 minutes:
                                         // bit 23 = 23:30 to 00:29
                                         // bit 22 = 22:30 to 23:29
                                         // bit 21 = 21:30 to 22:29 (Night)
                                         // bit 20 = 20:30 to 21:29 (Dusk)
                                         // bit 19 = 19:30 to 20:29
                                         // bit 18 = 18:30 to 19:29
                                         // bit 17 = 17:30 to 18:29
                                         // bit 16 = 16:30 to 17:29
                                         // bit 15 = 15:30 to 16:29
                                         // bit 14 = 14:30 to 15:29
                                         // bit 13 = 13:30 to 14:29
                                         // bit 12 = 12:30 to 13:29
                                         // bit 11 = 11:30 to 12:29
                                         // bit 10 = 10:30 to 11:29
                                         // bit 9 = 09:30 to 10:29
                                         // bit 8 = 08:30 to 09:29
                                         // bit 7 = 07:30 to 08:29
                                         // bit 6 = 06:30 to 07:29 (Day)
                                         // bit 5 = 05:30 to 06:29 (Dawn)
                                         // bit 4 = 04:30 to 05:29
                                         // bit 3 = 03:30 to 04:29
                                         // bit 2 = 02:30 to 03:29
                                         // bit 1 = 01:30 to 02:29
                                         // bit 0 = 00:30 to 01:29
    uint32_t flags;                      // 0x0090 Flags
                                         // bit 0: (0) Ambient disabled / (1) Ambient enabled
                                         // bit 1: (0) Enable environmental effects / (1) Disable environmental effects
                                         // bit 2: (0) Local (uses radius) / (1) Global (ignores radius)
                                         // bit 3: (0) Sequential ambient selection / (1) Random ambient selection
                                         // bit 4: Affected by 'Low Mem Sounds 1'
    char unused2[64];                    // 0x0094 Unused
};

struct AREVariable {
    char name[32];                    // 0x0000 Variable name
    uint16_t type;                    // 0x0020 Variable type (bitfield)
                                       // bit 0: int
                                       // bit 1: float
                                       // bit 2: script name
                                       // bit 3: resref
                                       // bit 4: strref
                                       // bit 5: dword
    uint16_t resourceType;            // 0x0022 Resource type
    uint32_t dwordValue;              // 0x0024 Dword value
    uint32_t intValue;                // 0x0028 Int value (most commonly used)
    double doubleValue;               // 0x002c Double value
    char scriptNameValue[32];         // 0x0030 Script name value
};

struct AREDoor {
    char name[32];                           // 0x0000 Name
    char doorId[8];                          // 0x0020 Door ID (links to WED)
    uint32_t flags;                          // 0x0028 Flags
                                               // bit 0: Door open
                                               // bit 1: Door locked
                                               // bit 2: Reset trap
                                               // bit 3: Trap detectable
                                               // bit 4: Broken
                                               // bit 5: Can't close
                                               // bit 6: Linked
                                               // bit 7: Door Hidden
                                               // bit 8: Door Found (if hidden)
                                               // bit 9: Don't block line of sight
                                               // bit 10: Remove Key (BG2 only)
                                               // bit 11: Ignore obstacles when closing
    uint32_t openVerticesIndex;              // 0x002c Index of first vertex (open)
    uint16_t openVerticesCount;              // 0x0030 Count of vertices (open)
    uint16_t closedVerticesCount;            // 0x0032 Count of vertices (closed)
    uint32_t closedVerticesIndex;            // 0x0034 Index of first vertex (closed)
    int16_t openBoundingBox[4];              // 0x0038 Bounding box [left,top,right,bottom] (open)
    int16_t closedBoundingBox[4];            // 0x0040 Bounding box [left,top,right,bottom] (closed)
    uint32_t openImpededCellBlockIndex;      // 0x0048 Index of impeded cells (open)
    uint16_t openImpededCellBlockCount;      // 0x004c Count of impeded cells (open)
    uint16_t closedImpededCellBlockCount;    // 0x004e Count of impeded cells (closed)
    uint32_t closedImpededCellBlockIndex;    // 0x0050 Index of impeded cells (closed)
    uint16_t hitPoints;                      // 0x0054 Hit points
    uint16_t armorClass;                     // 0x0056 Armor class
    char openSound[8];                       // 0x0058 Door open sound (resref)
    char closeSound[8];                      // 0x0060 Door close sound (resref)
    uint32_t cursorIndex;                    // 0x0068 Cursor index (cursors.bam)
    uint16_t trapDetectionDifficulty;        // 0x006c Trap detection difficulty
    uint16_t trapRemovalDifficulty;          // 0x006e Trap removal difficulty
    uint16_t isTrapped;                      // 0x0070 Door is trapped (0=No, 1=Yes)
    uint16_t trapDetected;                   // 0x0072 Trap detected (0=No, 1=Yes)
    uint16_t trapLaunchTargetX;              // 0x0074 Trap launch target X coordinate
    uint16_t trapLaunchTargetY;              // 0x0076 Trap launch target Y coordinate
    char keyItem[8];                         // 0x0078 Key item (resref)
    char doorScript[8];                      // 0x0080 Door script (resref)
    uint32_t detectionDifficulty;            // 0x0088 Detection difficulty (secret doors)
    uint32_t lockDifficulty;                 // 0x008c Lock difficulty (0-100)
    int16_t usePoints[2][2];                 // 0x0090 Use points [x1,y1,x2,y2]
    uint32_t lockpickString;                 // 0x0098 Lockpick string (strref)
    char travelTriggerName[24];              // 0x009c Travel trigger name
    uint32_t dialogSpeakerName;              // 0x00b4 Dialog speaker name (strref)
    char dialogResref[8];                    // 0x00b8 Dialog resref
    char unknown[8];                         // 0x00c0 Unknown
};

struct AREAnimation {
    char name[32];                    // 0x0000
    uint16_t x;                       // 0x0020
    uint16_t y;                       // 0x0022
    uint32_t appearanceSchedule;      // 0x0024 (bits 0-23 represent hours)
    char animationResref[8];          // 0x0028 (BGEE: BAM/WBM/PVRZ, Others: BAM)
    uint16_t bamSequenceNumber;       // 0x0030
    uint16_t bamFrameNumber;          // 0x0032
    uint32_t flags;                   // 0x0034
                                       // bit 13: Use WBM resref (BGEE)
                                       // bit 14: Underground? (BGEE)
                                       // bit 15: Use PVRZ resref (BGEE)
    uint16_t height;                  // 0x0038
    uint16_t transparency;            // 0x003a
    uint16_t startingFrame;           // 0x003c
    uint8_t chanceOfLooping;          // 0x003e
    uint8_t skipCycles;               // 0x003f
    char palette[8];                  // 0x0040
    uint16_t animationWidth;          // 0x0048 (BGEE only - for WBM/PVRZ)
    uint16_t animationHeight;         // 0x004a (BGEE only - for WBM/PVRZ)
};

struct AREAutomapNote {
    union {
        struct {
            // Standard format (BG1, BG2, IWD) - 52 bytes
            uint16_t x;                 // 0x0000
            uint16_t y;                 // 0x0002
            uint32_t noteText;          // 0x0004 (strref)
            uint16_t strrefLocation;    // 0x0008
            uint16_t color;             // 0x000a (BG2) Color of automap marker
            uint32_t noteCount;         // 0x000c
            char unused[36];            // 0x0010
        } standard;
        struct {
            // PST format - 532 bytes
            uint32_t x;                 // 0x0000
            uint32_t y;                 // 0x0004
            char text[500];             // 0x0008 (ASCII text)
            uint32_t color;             // 0x01fc (0=Blue user note, 1=Red game note)
            uint32_t unknown[5];        // 0x0200
        } pst;
    };
};

struct ARETiledObject {
    char name[32];                      // 0x0000 Name
    char tileId[8];                     // 0x0020 Tile Id (resref)
    uint32_t flags;                     // 0x0028 Flags
                                        // bit 0: Currently in secondary state
                                        // bit 1: Can be seen through
    uint32_t openSearchSquaresOffset;   // 0x002c Offset to open search squares
    uint16_t openSearchSquaresCount;    // 0x0030 Count of open search squares
    uint16_t closedSearchSquaresCount;  // 0x0034 Count of closed search squares
    uint32_t closedSearchSquaresOffset; // 0x0038 Offset to closed search squares
    char unused[48];                    // 0x003c Unused
};

struct AREProjectileTrap {
    char projectileResref[8];           // 0x0000 Projectile resref
    uint32_t effectBlockOffset;         // 0x0008 Effect block offset
    uint16_t effectBlockSize;           // 0x000c Effect block size
    uint16_t missileIdsReference;       // 0x000e Missile.ids reference (projectl.ids - 1)
    uint16_t ticksUntilNextTriggerCheck; // 0x0010 Ticks until next trigger check
    uint16_t triggersRemaining;         // 0x0012 Triggers remaining (explosion count)
    uint16_t x;                         // 0x0014 X coordinate
    uint16_t y;                         // 0x0016 Y coordinate
    uint16_t z;                         // 0x0018 Z coordinate
    uint8_t enemyAllyTargeting;         // 0x001a Enemy-ally targeting
    uint8_t partyMemberIndex;           // 0x001b Party member index that created this projectile (0-5)
                                        // Note: BG2-specific feature
};

struct ARESongEntry {
    uint32_t daySong;                // 0x0000 Day song reference number
    uint32_t nightSong;              // 0x0004 Night song reference number
    uint32_t winSong;                // 0x0008 Win song reference number
    uint32_t battleSong;             // 0x000c Battle song reference number
    uint32_t loseSong;               // 0x0010 Lose song reference number
    uint32_t altMusic1;              // 0x0014 Alt music 1
    uint32_t altMusic2;              // 0x0018 Alt music 2
    uint32_t altMusic3;              // 0x001c Alt music 3
    uint32_t altMusic4;              // 0x0020 Alt music 4
    uint32_t altMusic5;              // 0x0024 Alt music 5
    char mainDayAmbient1[8];         // 0x0028 Main day ambient 1 (WAV resref)
    char mainDayAmbient2[8];         // 0x0030 Main day ambient 2 (WAV resref)
    uint32_t mainDayAmbientVolume;   // 0x0038 Main day ambient volume %
    char mainNightAmbient1[8];       // 0x003c Main night ambient 1 (WAV resref)
    char mainNightAmbient2[8];       // 0x0044 Main night ambient 2 (WAV resref)
    uint32_t mainNightAmbientVolume; // 0x004c Main night ambient volume %
    uint32_t reverb;                 // 0x0050 Reverb from REVERB.IDS/REVERB.2DA, or unused
    char unused[60];                 // 0x0054 Unused
};

struct ARERestInterrupt {
    char name[32];                                    // 0x0000 Name
    uint32_t interruptionExplanationText[10];         // 0x0020 Interruption explanation text (10 entries)
    char creatureToSpawn[10][8];                       // 0x0048 Creature resrefs to spawn (10 slots)
    uint16_t countOfCreaturesInSpawnTable;             // 0x0098 Count of creatures in spawn table
    uint16_t difficulty;                               // 0x009a Difficulty
                                                     // If Party Level * Difficulty > Total Creature Power then spawn
    uint32_t removalTime;                             // 0x009c Removal time (seconds)
    uint16_t movementRestrictionDistance;             // 0x00a0 Movement restriction distance
    uint16_t movementRestrictionDistanceMoveToObject; // 0x00a2 Movement restriction distance (move to object)
    uint16_t maxCreaturesToSpawn;                     // 0x00a4 Maximum number of creatures to spawn
    uint16_t interruptionPointEnabled;                // 0x00a6 Interruption point enabled (0=Inactive, 1=Active)
    uint16_t probabilityDay;                          // 0x00a8 Probability (day) per hour
    uint16_t probabilityNight;                        // 0x00aa Probability (night) per hour
    char unused[56];                                  // 0x00ac Unused
};

#pragma pack(pop)

struct AREFile {
    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(AREHeader)) {
            Log(ERROR, "ARE", "Data size is too small for an ARE header.");
            return false;
        }

        // Copy header
        std::memcpy(&header, data.data(), sizeof(AREHeader));


        Log(DEBUG, "ARE", "ARE deserialization header values:");
        Log(DEBUG, "ARE", "  Header size: {}", sizeof(AREHeader));
        Log(DEBUG, "ARE", "  File size: {}", data.size());
        Log(DEBUG, "ARE", "  Signature: {:.4s}", header.signature);
        Log(DEBUG, "ARE", "  Version: {:.4s}", header.version);
        Log(DEBUG, "ARE", "  Vertices offset: {}", header.verticesOffset);
        Log(DEBUG, "ARE", "  Vertices count: {}", header.verticesCount);
        Log(DEBUG, "ARE", "  Tiled objects count: {}", header.tiledObjectsCount);
        Log(DEBUG, "ARE", "  Tiled objects offset: {}", header.tiledObjectsOffset);
        Log(DEBUG, "ARE", "  Explored size: {}", header.exploredBitmaskSize);
        Log(DEBUG, "ARE", "  Explored offset: {}", header.exploredBitmaskOffset);

        // Validate signature and version
        if (std::string(header.signature, 4) != "AREA" || std::string(header.version, 4) != "V1.0") {
            Log(ERROR, "ARE", "Invalid ARE file signature or version.");
            return false;
        }

        auto read_section = [&](auto& vec, uint32_t offset, uint32_t count) {
            if (count > 0 && offset > 0 && (offset + count * sizeof(typename std::remove_reference_t<decltype(vec)>::value_type)) <= data.size()) {
                vec.resize(count);
                std::memcpy(vec.data(), data.data() + offset, count * sizeof(typename std::remove_reference_t<decltype(vec)>::value_type));
            }
        };

        // Read all sections based on header offsets and counts
        read_section(actors, header.actorsOffset, header.actorsCount);
        read_section(regions, header.regionsOffset, header.regionsCount);
        read_section(spawnPoints, header.spawnPointsOffset, header.spawnPointsCount);
        read_section(entrances, header.entrancesOffset, header.entrancesCount);
        read_section(containers, header.containersOffset, header.containersCount);
        read_section(items, header.itemsOffset, header.itemsCount);
        read_section(vertices, header.verticesOffset, header.verticesCount);
        read_section(ambients, header.ambientsOffset, header.ambientsCount);
        read_section(variables, header.variablesOffset, header.variablesCount);
        read_section(doors, header.doorsOffset, header.doorsCount);
        read_section(animations, header.animationsOffset, header.animationsCount);
        read_section(tiledObjects, header.tiledObjectsOffset, header.tiledObjectsCount);
        // Game-specific field access through union
        if (PIE4K_CFG.GameType == "pst") {
            read_section(automapNotes, header.gameSpecific.pst.automapNoteOffset, header.gameSpecific.pst.automapNoteCount);
            read_section(projectileTraps, header.gameSpecific.pst.projectileTrapsOffset, header.gameSpecific.pst.projectileTrapsCount);
        } else {
            read_section(automapNotes, header.gameSpecific.standard.automapNoteOffset, header.gameSpecific.standard.automapNoteCount);
            // Projectile traps are BG2-specific, but we read them for all games for compatibility
            read_section(projectileTraps, header.gameSpecific.standard.projectileTrapsOffset, header.gameSpecific.standard.projectileTrapsCount);
        }
        
        // Read tiled object flags (array of uint16_t)
        if (header.tiledObjectFlagsCount > 0 && header.tiledObjectFlagsOffset > 0) {
            tiledObjectFlags.resize(header.tiledObjectFlagsCount);
            std::memcpy(tiledObjectFlags.data(), data.data() + header.tiledObjectFlagsOffset, 
                       header.tiledObjectFlagsCount * sizeof(uint16_t));
        }

        // Read song entries (array, not just one)
        if (header.songEntriesOffset > 0) {
            // For now, read one song entry, but this should be an array
            songEntries.resize(1);
            std::memcpy(&songEntries[0], data.data() + header.songEntriesOffset, sizeof(ARESongEntry));
        }

        // Read rest interruptions (array, not just one)
        if (header.restInterruptionsOffset > 0) {
            // For now, read one rest interruption, but this should be an array
            restInterrupts.resize(1);
            std::memcpy(&restInterrupts[0], data.data() + header.restInterruptionsOffset, sizeof(ARERestInterrupt));
        }

        // Read explored bitmask
        if (header.exploredBitmaskSize > 0 && header.exploredBitmaskOffset > 0 &&
            (header.exploredBitmaskOffset + header.exploredBitmaskSize) <= data.size()) {
            exploredBitmask.resize(header.exploredBitmaskSize);
            std::memcpy(exploredBitmask.data(), data.data() + header.exploredBitmaskOffset, header.exploredBitmaskSize);
        } else if (header.exploredBitmaskSize > 0) {
            // Try to find explored bitmask at the end of the file
            size_t potentialOffset = data.size() - header.exploredBitmaskSize;
            if (potentialOffset >= sizeof(AREHeader) && potentialOffset + header.exploredBitmaskSize <= data.size()) {
                Log(DEBUG, "ARE", "Attempting to read explored bitmask from potential offset: {}", potentialOffset);
                exploredBitmask.resize(header.exploredBitmaskSize);
                std::memcpy(exploredBitmask.data(), data.data() + potentialOffset, header.exploredBitmaskSize);
            }
        }

        return true;
    }

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        AREHeader updatedHeader = header; // Create a mutable copy
        uint32_t currentOffset = sizeof(AREHeader);

        Log(DEBUG, "ARE", "AREFile serialize - original header vertices: offset={}, count={}",
            header.verticesOffset, header.verticesCount);
        Log(DEBUG, "ARE", "Starting ARE serialization with currentOffset: {}", currentOffset);

        auto write_section = [&](const auto& vec, uint32_t& offset, auto& count, const char* sectionName) {
            if (!vec.empty()) {
                offset = currentOffset;
                count = vec.size();
                size_t sectionSize = vec.size() * sizeof(typename std::remove_reference<decltype(vec)>::type::value_type);
                Log(DEBUG, "ARE", "Writing {} section: offset={}, count={}, size={}",
                    sectionName, offset, (size_t)count, sectionSize);
                const uint8_t* start = reinterpret_cast<const uint8_t*>(vec.data());
                const uint8_t* end = start + sectionSize;
                data.insert(data.end(), start, end);
                currentOffset += sectionSize;
            } else {
                // For empty sections, set offset to 0 to avoid conflicts
                offset = 0;
                count = 0;
                Log(DEBUG, "ARE", "Skipping empty {} section", sectionName);
            }
        };
        
        // Reserve space for header
        data.resize(sizeof(AREHeader));

        // Write sections and update header offsets/counts
        // Order must match the original file layout to preserve correct offsets
        write_section(actors, updatedHeader.actorsOffset, updatedHeader.actorsCount, "actors");
        write_section(regions, updatedHeader.regionsOffset, updatedHeader.regionsCount, "regions");
        write_section(spawnPoints, updatedHeader.spawnPointsOffset, updatedHeader.spawnPointsCount, "spawnPoints");
        write_section(entrances, updatedHeader.entrancesOffset, updatedHeader.entrancesCount, "entrances");
        write_section(containers, updatedHeader.containersOffset, updatedHeader.containersCount, "containers");
        write_section(items, updatedHeader.itemsOffset, updatedHeader.itemsCount, "items");
        write_section(vertices, updatedHeader.verticesOffset, updatedHeader.verticesCount, "vertices");
        Log(DEBUG, "ARE", "After write_section vertices: offset={}, count={}",
            updatedHeader.verticesOffset, updatedHeader.verticesCount);
        write_section(ambients, updatedHeader.ambientsOffset, updatedHeader.ambientsCount, "ambients");
        write_section(variables, updatedHeader.variablesOffset, updatedHeader.variablesCount, "variables");
        write_section(doors, updatedHeader.doorsOffset, updatedHeader.doorsCount, "doors");
        write_section(animations, updatedHeader.animationsOffset, updatedHeader.animationsCount, "animations");
        write_section(tiledObjects, updatedHeader.tiledObjectsOffset, updatedHeader.tiledObjectsCount, "tiledObjects");
        // Game-specific field access through union
        if (PIE4K_CFG.GameType == "pst") {
            write_section(automapNotes, updatedHeader.gameSpecific.pst.automapNoteOffset, updatedHeader.gameSpecific.pst.automapNoteCount, "automapNotes");
            write_section(projectileTraps, updatedHeader.gameSpecific.pst.projectileTrapsOffset, updatedHeader.gameSpecific.pst.projectileTrapsCount, "projectileTraps");
        } else {
            write_section(automapNotes, updatedHeader.gameSpecific.standard.automapNoteOffset, updatedHeader.gameSpecific.standard.automapNoteCount, "automapNotes");
            // Projectile traps are BG2-specific, but we write them for all games for compatibility
            write_section(projectileTraps, updatedHeader.gameSpecific.standard.projectileTrapsOffset, updatedHeader.gameSpecific.standard.projectileTrapsCount, "projectileTraps");
        }

        // Write tiled object flags
        if (!tiledObjectFlags.empty()) {
            updatedHeader.tiledObjectFlagsOffset = currentOffset;
            updatedHeader.tiledObjectFlagsCount = tiledObjectFlags.size();
            size_t sectionSize = tiledObjectFlags.size() * sizeof(uint16_t);
            Log(DEBUG, "ARE", "Writing tiledObjectFlags section: offset={}, count={}, size={}",
                currentOffset, tiledObjectFlags.size(), sectionSize);
            const uint8_t* start = reinterpret_cast<const uint8_t*>(tiledObjectFlags.data());
            data.insert(data.end(), start, start + sectionSize);
            currentOffset += sectionSize;
        } else {
            updatedHeader.tiledObjectFlagsCount = 0;
            Log(DEBUG, "ARE", "Skipping empty tiledObjectFlags section");
            // Preserve original offset
        }

        // Write song entries
        if (!songEntries.empty()) {
            updatedHeader.songEntriesOffset = currentOffset;
            Log(DEBUG, "ARE", "Writing songEntries section: offset={}, count={}, size={}",
                currentOffset, songEntries.size(), sizeof(ARESongEntry));
            const uint8_t* start = reinterpret_cast<const uint8_t*>(&songEntries[0]);
            data.insert(data.end(), start, start + sizeof(ARESongEntry));
            currentOffset += sizeof(ARESongEntry);
        } else {
            Log(DEBUG, "ARE", "Skipping empty songEntries section");
            // Preserve original offset if empty
        }

        // Write rest interruptions
        if (!restInterrupts.empty()) {
            updatedHeader.restInterruptionsOffset = currentOffset;
            Log(DEBUG, "ARE", "Writing restInterrupts section: offset={}, count={}, size={}",
                currentOffset, restInterrupts.size(), sizeof(ARERestInterrupt));
            const uint8_t* start = reinterpret_cast<const uint8_t*>(&restInterrupts[0]);
            data.insert(data.end(), start, start + sizeof(ARERestInterrupt));
            currentOffset += sizeof(ARERestInterrupt);
        } else {
            Log(DEBUG, "ARE", "Skipping empty restInterrupts section");
            // Preserve original offset if empty
        }

        // Write explored bitmask
        if (!exploredBitmask.empty()) {
            updatedHeader.exploredBitmaskOffset = currentOffset;
            updatedHeader.exploredBitmaskSize = exploredBitmask.size();
            Log(DEBUG, "ARE", "Writing exploredBitmask section: offset={}, size={}",
                currentOffset, exploredBitmask.size());
            data.insert(data.end(), exploredBitmask.begin(), exploredBitmask.end());
            currentOffset += exploredBitmask.size();
        } else {
            updatedHeader.exploredBitmaskSize = 0;
            updatedHeader.exploredBitmaskOffset = 0;
            Log(DEBUG, "ARE", "Skipping empty exploredBitmask section");
        }

        // Zero out the unused field in the header to prevent "unused bytes" issues
        if (PIE4K_CFG.GameType != "pst") {
            std::memset(updatedHeader.gameSpecific.standard.unused, 0, 56);
        } else {
            std::memset(updatedHeader.gameSpecific.pst.unused, 0, 64);
        }

        // Write the updated header at the beginning of the data vector
        std::memcpy(data.data(), &updatedHeader, sizeof(AREHeader));

        // Debug: Check if header was written correctly
        AREHeader checkHeader;
        std::memcpy(&checkHeader, data.data(), sizeof(AREHeader));
        Log(DEBUG, "ARE", "Header written to data - vertices: offset={}, count={}",
            checkHeader.verticesOffset, checkHeader.verticesCount);

        // Also check the raw bytes
        Log(DEBUG, "ARE", "Raw header bytes in data: {:02x} {:02x} {:02x} {:02x} (vertices offset)",
            data[0x7c], data[0x7d], data[0x7e], data[0x7f]);
        Log(DEBUG, "ARE", "Raw header bytes in data: {:02x} {:02x} {:02x} {:02x} (vertices count)",
            data[0x80], data[0x81], data[0x82], data[0x83]);

        Log(DEBUG, "ARE", "ARE serialization complete - data size: {}, vertices: offset={}, count={}",
            data.size(), updatedHeader.verticesOffset, updatedHeader.verticesCount);
        Log(DEBUG, "ARE", "Original header vertices: offset={}, count={}",
            header.verticesOffset, header.verticesCount);

        return data;
    }

    AREHeader header;
    std::vector<AREActor> actors;
    std::vector<ARERegion> regions;
    std::vector<ARESpawnPoint> spawnPoints;
    std::vector<AREEntrance> entrances;
    std::vector<AREContainer> containers;
    std::vector<AREItem> items;
    std::vector<AREVertex> vertices;
    std::vector<AREAmbient> ambients;
    std::vector<AREVariable> variables;
    std::vector<uint8_t> exploredBitmask;  // Bit array: 1 bit per 16x16 pixel cell (set=explored)
    std::vector<AREDoor> doors;
    std::vector<AREAnimation> animations;
    std::vector<AREAutomapNote> automapNotes;
    std::vector<ARETiledObject> tiledObjects;
    std::vector<AREProjectileTrap> projectileTraps;
    std::vector<ARESongEntry> songEntries;
    std::vector<ARERestInterrupt> restInterrupts;
    std::vector<uint16_t> tiledObjectFlags;
};

} // namespace ProjectIE4k

#endif // AREV1_HPP
