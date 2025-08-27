#ifndef AREV1_HPP
#define AREV1_HPP

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>

#include "core/Logging/Logging.h"

namespace ProjectIE4k {

#pragma pack(push, 1)

struct AREHeader {
    char signature[4];
    char version[4];
    char areaWED[8];
    uint32_t lastSaved;
    uint32_t areaFlags;
    char northArea[8];
    uint32_t northFlags;
    char eastArea[8];
    uint32_t eastFlags;
    char southArea[8];
    uint32_t southFlags;
    char westArea[8];
    uint32_t westFlags;
    uint16_t areaTypeFlags;
    uint16_t rainProb;
    uint16_t snowProb;
    uint16_t fogProb;
    uint16_t lightningProb;
    uint16_t windSpeed;
    uint32_t actorsOffset;
    uint16_t actorsCount;
    uint16_t regionsCount;
    uint32_t regionsOffset;
    uint32_t spawnPointsOffset;
    uint32_t spawnPointsCount;
    uint32_t entrancesOffset;
    uint32_t entrancesCount;
    uint32_t containersOffset;
    uint16_t containersCount;
    uint16_t itemsCount;
    uint32_t itemsOffset;
    uint32_t verticesOffset;
    uint16_t verticesCount;
    uint16_t ambientsCount;
    uint32_t ambientsOffset;
    uint32_t variablesOffset;
    uint32_t variablesCount;
    uint16_t tiledObjectFlagsOffset;
    uint16_t tiledObjectFlagsCount;
    char areaScript[8];
    uint32_t exploredBitmaskSize;
    uint32_t exploredBitmaskOffset;
    uint32_t doorsCount;
    uint32_t doorsOffset;
    uint32_t animationsCount;
    uint32_t animationsOffset;
    uint32_t tiledObjectsCount;
    uint32_t tiledObjectsOffset;
    uint32_t songEntriesOffset;
    uint32_t restInterruptionsOffset;
    uint32_t automapNoteOffset;
    uint32_t automapNoteCount;
    uint32_t projectileTrapsOffset;
    uint32_t projectileTrapsCount;
    char restMovieDay[8];
    char restMovieNight[8];
    char unused[56];
};

struct AREActor {
    char name[32];
    uint16_t currentX;
    uint16_t currentY;
    uint16_t destX;
    uint16_t destY;
    uint32_t flags;
    uint16_t hasBeenSpawned;
    uint8_t creResrefFirstLetter;
    uint8_t unused1;
    uint32_t actorAnimation;
    uint16_t actorOrientation;
    uint16_t unused2;
    uint32_t actorRemovalTimer;
    uint16_t movementRestrictionDistance;
    uint16_t movementRestrictionDistanceMoveToObject;
    uint32_t actorAppearanceSchedule;
    uint32_t numTimesTalkedTo;
    char dialog[8];
    char scriptOverride[8];
    char scriptGeneral[8];
    char scriptClass[8];
    char scriptRace[8];
    char scriptDefault[8];
    char scriptSpecific[8];
    char creFile[8];
    uint32_t creOffset;
    uint32_t creSize;
    char unused3[128];
};

struct ARERegion {
    char name[32];
    uint16_t regionType;
    int16_t boundingBox[4];
    uint16_t verticesCount;
    uint32_t verticesIndex;
    uint32_t triggerValue;
    uint32_t cursorIndex;
    char destArea[8];
    char entranceName[32];
    uint32_t flags;
    uint32_t infoText;
    uint16_t trapDetectionDifficulty;
    uint16_t trapRemovalDifficulty;
    uint16_t isTrapped;
    uint16_t trapDetected;
    uint32_t trapLaunchLocation;
    char keyItem[8];
    char regionScript[8];
    uint16_t altUsePointX;
    uint16_t altUsePointY;
    uint32_t unknown1;
    char unknown2[32];
    char sound[8];
    uint16_t talkLocationPointX;
    uint16_t talkLocationPointY;
    uint32_t speakerName;
    char dialogFile[8];
};

struct ARESpawnPoint {
    char name[32];
    uint16_t x;
    uint16_t y;
    char creatureToSpawn[10][8];
    uint16_t countOfSpawnCreatures;
    uint16_t baseCreatureNumberToSpawn;
    uint16_t frequency;
    uint16_t spawnMethod;
    uint32_t actorRemovalTimer;
    uint16_t movementRestrictionDistance;
    uint16_t movementRestrictionDistanceMoveToObject;
    uint16_t maxCreaturesToSpawn;
    uint16_t spawnPointEnabled;
    uint32_t spawnPointAppearanceSchedule;
    uint16_t probabilityDay;
    uint16_t probabilityNight;
    union {
        struct {
            char unused[56];
        } bg;
        struct {
            uint32_t spawnFrequency;
            uint32_t countdown;
            uint8_t spawnWeight[10];
            char unused[38];
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
    char name[32];
    uint16_t x;
    uint16_t y;
    uint16_t containerType;
    uint16_t lockDifficulty;
    uint32_t flags;
    uint16_t trapDetectionDifficulty;
    uint16_t trapRemovalDifficulty;
    uint16_t isTrapped;
    uint16_t trapDetected;
    uint16_t trapLaunchX;
    uint16_t trapLaunchY;
    int16_t boundingBox[4];
    uint32_t itemsIndex;
    uint32_t itemsCount;
    char trapScript[8];
    uint32_t verticesIndex;
    uint16_t verticesCount;
    uint16_t triggerRange;
    char owner[32];
    char keyItem[8];
    uint32_t breakDifficulty;
    uint32_t lockpickString;
    char unused[56];
};

struct AREItem {
    char resref[8];
    uint16_t expirationTime;
    uint16_t charges1;
    uint16_t charges2;
    uint16_t charges3;
    uint32_t flags;
};

struct AREVertex {
    int16_t x;
    int16_t y;
};

struct AREAmbient {
    char name[32];
    uint16_t x;
    uint16_t y;
    uint16_t radius;
    uint16_t height;
    uint32_t pitchVariance;
    uint16_t volumeVariance;
    uint16_t volume;
    char sounds[10][8];
    uint16_t countOfSounds;
    uint16_t unused1;
    uint32_t baseTime;
    uint32_t baseTimeDeviation;
    uint32_t ambientAppearanceSchedule;
    uint32_t flags;
    char unused2[64];
};

struct AREVariable {
    char name[32];
    uint16_t type;
    uint16_t resourceType;
    uint32_t dwordValue;
    uint32_t intValue;
    double doubleValue;
    char scriptNameValue[32];
};

struct AREDoor {
    char name[32];
    char doorId[8];
    uint32_t flags;
    uint32_t openVerticesIndex;
    uint16_t openVerticesCount;
    uint16_t closedVerticesCount;
    uint32_t closedVerticesIndex;
    int16_t openBoundingBox[4];
    int16_t closedBoundingBox[4];
    uint32_t openImpededCellBlockIndex;
    uint16_t openImpededCellBlockCount;
    uint16_t closedImpededCellBlockCount;
    uint32_t closedImpededCellBlockIndex;
    uint16_t hitPoints;
    uint16_t armorClass;
    char openSound[8];
    char closeSound[8];
    uint32_t cursorIndex;
    uint16_t trapDetectionDifficulty;
    uint16_t trapRemovalDifficulty;
    uint16_t isTrapped;
    uint16_t trapDetected;
    uint16_t trapLaunchTargetX;
    uint16_t trapLaunchTargetY;
    char keyItem[8];
    char doorScript[8];
    uint32_t detectionDifficulty;
    uint32_t lockDifficulty;
    int16_t usePoints[2][2];
    uint32_t lockpickString;
    char travelTriggerName[24];
    uint32_t dialogSpeakerName;
    char dialogResref[8];
    char unknown[8];
};

struct AREAnimation {
    char name[32];
    uint16_t x;
    uint16_t y;
    uint32_t appearanceSchedule;
    char animationResref[8];
    uint16_t bamSequenceNumber;
    uint16_t bamFrameNumber;
    uint32_t flags;
    uint16_t height;
    uint16_t transparency;
    uint16_t startingFrame;
    uint8_t chanceOfLooping;
    uint8_t skipCycles;
    char palette[8];
    uint16_t animationWidth;
    uint16_t animationHeight;
};

struct AREAutomapNote {
    uint16_t x;
    uint16_t y;
    uint32_t noteText;
    uint16_t strrefLocation;
    uint16_t color;
    uint32_t noteCount;
    char unused[36];
};

struct ARETiledObject {
    char name[32];
    char tileId[8];
    uint32_t flags;
    uint32_t openSearchSquaresOffset;
    uint16_t openSearchSquaresCount;
    uint16_t closedSearchSquaresCount;
    uint32_t closedSearchSquaresOffset;
    char unused[48];
};

struct AREProjectileTrap {
    char projectileResref[8];
    uint32_t effectBlockOffset;
    uint16_t effectBlockSize;
    uint16_t missileIdsReference;
    uint16_t ticksUntilNextTriggerCheck;
    uint16_t triggersRemaining;
    uint16_t x;
    uint16_t y;
    uint16_t z;
    uint8_t enemyAllyTargeting;
    uint8_t partyMemberIndex;
};

struct ARESongEntry {
    uint32_t daySong;
    uint32_t nightSong;
    uint32_t winSong;
    uint32_t battleSong;
    uint32_t loseSong;
    uint32_t altMusic1;
    uint32_t altMusic2;
    uint32_t altMusic3;
    uint32_t altMusic4;
    uint32_t altMusic5;
    char mainDayAmbient1[8];
    char mainDayAmbient2[8];
    uint32_t mainDayAmbientVolume;
    char mainNightAmbient1[8];
    char mainNightAmbient2[8];
    uint32_t mainNightAmbientVolume;
    uint32_t reverb;
    char unused[60];
};

struct ARERestInterrupt {
    char name[32];
    uint32_t interruptionExplanationText[10];
    char creatureToSpawn[10][8];
    uint16_t countOfCreaturesInSpawnTable;
    uint16_t difficulty;
    uint32_t removalTime;
    uint16_t movementRestrictionDistance;
    uint16_t movementRestrictionDistanceMoveToObject;
    uint16_t maxCreaturesToSpawn;
    uint16_t interruptionPointEnabled;
    uint16_t probabilityDay;
    uint16_t probabilityNight;
    char unused[56];
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
        read_section(automapNotes, header.automapNoteOffset, header.automapNoteCount);
        read_section(projectileTraps, header.projectileTrapsOffset, header.projectileTrapsCount);
        
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
        if (header.exploredBitmaskSize > 0 && header.exploredBitmaskOffset > 0) {
            exploredBitmask.resize(header.exploredBitmaskSize);
            std::memcpy(exploredBitmask.data(), data.data() + header.exploredBitmaskOffset, header.exploredBitmaskSize);
        }

        return true;
    }

    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        AREHeader updatedHeader = header; // Create a mutable copy
        uint32_t currentOffset = sizeof(AREHeader);

        auto write_section = [&](const auto& vec, uint32_t& offset, auto& count) {
            if (!vec.empty()) {
                offset = currentOffset;
                count = vec.size();
                const uint8_t* start = reinterpret_cast<const uint8_t*>(vec.data());
                const uint8_t* end = start + vec.size() * sizeof(typename std::remove_reference<decltype(vec)>::type::value_type);
                data.insert(data.end(), start, end);
                currentOffset += (end - start);
            } else {
                // For empty sections, preserve the original offset from the header
                // Don't change the offset at all, only set count to 0
                count = 0;
                // Keep the original offset value from the header (don't modify offset)
            }
        };
        
        // Reserve space for header
        data.resize(sizeof(AREHeader));

        // Write sections and update header offsets/counts
        // Order must match the original file layout to preserve correct offsets
        write_section(actors, updatedHeader.actorsOffset, updatedHeader.actorsCount);
        write_section(regions, updatedHeader.regionsOffset, updatedHeader.regionsCount);
        write_section(spawnPoints, updatedHeader.spawnPointsOffset, updatedHeader.spawnPointsCount);
        write_section(entrances, updatedHeader.entrancesOffset, updatedHeader.entrancesCount);
        write_section(containers, updatedHeader.containersOffset, updatedHeader.containersCount);
        write_section(items, updatedHeader.itemsOffset, updatedHeader.itemsCount);
        write_section(ambients, updatedHeader.ambientsOffset, updatedHeader.ambientsCount);
        write_section(variables, updatedHeader.variablesOffset, updatedHeader.variablesCount);
        write_section(doors, updatedHeader.doorsOffset, updatedHeader.doorsCount);
        write_section(vertices, updatedHeader.verticesOffset, updatedHeader.verticesCount);
        write_section(animations, updatedHeader.animationsOffset, updatedHeader.animationsCount);
        write_section(tiledObjects, updatedHeader.tiledObjectsOffset, updatedHeader.tiledObjectsCount);
        write_section(automapNotes, updatedHeader.automapNoteOffset, updatedHeader.automapNoteCount);
        write_section(projectileTraps, updatedHeader.projectileTrapsOffset, updatedHeader.projectileTrapsCount);

        // Write tiled object flags
        if (!tiledObjectFlags.empty()) {
            updatedHeader.tiledObjectFlagsOffset = currentOffset;
            updatedHeader.tiledObjectFlagsCount = tiledObjectFlags.size();
            const uint8_t* start = reinterpret_cast<const uint8_t*>(tiledObjectFlags.data());
            data.insert(data.end(), start, start + tiledObjectFlags.size() * sizeof(uint16_t));
            currentOffset += tiledObjectFlags.size() * sizeof(uint16_t);
        } else {
            updatedHeader.tiledObjectFlagsCount = 0;
            // Preserve original offset
        }

        // Write song entries
        if (!songEntries.empty()) {
            updatedHeader.songEntriesOffset = currentOffset;
            const uint8_t* start = reinterpret_cast<const uint8_t*>(&songEntries[0]);
            data.insert(data.end(), start, start + sizeof(ARESongEntry));
            currentOffset += sizeof(ARESongEntry);
        }
        // Preserve original offset if empty

        // Write rest interruptions
        if (!restInterrupts.empty()) {
            updatedHeader.restInterruptionsOffset = currentOffset;
            const uint8_t* start = reinterpret_cast<const uint8_t*>(&restInterrupts[0]);
            data.insert(data.end(), start, start + sizeof(ARERestInterrupt));
            currentOffset += sizeof(ARERestInterrupt);
        }
        // Preserve original offset if empty

        // Write explored bitmask
        if (!exploredBitmask.empty()) {
            updatedHeader.exploredBitmaskOffset = currentOffset;
            updatedHeader.exploredBitmaskSize = exploredBitmask.size();
            data.insert(data.end(), exploredBitmask.begin(), exploredBitmask.end());
            currentOffset += exploredBitmask.size();
        } else {
            updatedHeader.exploredBitmaskSize = 0;
            // Preserve original offset
        }

        // Write the updated header at the beginning of the data vector
        std::memcpy(data.data(), &updatedHeader, sizeof(AREHeader));

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
    std::vector<uint8_t> exploredBitmask;
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
