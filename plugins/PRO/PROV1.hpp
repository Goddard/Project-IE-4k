#ifndef PROV1_HPP
#define PROV1_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

namespace ProjectIE4k {

#pragma pack(push, 1)

// 8-byte resource reference (RESREF), zero-padded
struct ResRef8 {
    char name[8];

    ResRef8() { std::memset(name, 0, sizeof(name)); }
    explicit ResRef8(const std::string& s) { set(s); }

    void set(const std::string& s) {
        std::memset(name, 0, sizeof(name));
        std::memcpy(name, s.data(), std::min<size_t>(s.size(), 8));
    }

    std::string get() const {
        // Trim at first null
        size_t len = 0;
        while (len < 8 && name[len] != '\0') ++len;
        return std::string(name, name + len);
    }
};

// Projectile type
enum class ProjectileType : uint16_t {
    NoBam       = 1, // 256 bytes total
    SingleTarget= 2, // 512 bytes total (adds BAM section)
    AreaOfEffect= 3  // 768 bytes total (adds BAM + AoE sections)
};

// Target type semantics when ExtendedFlags bit 30 is unset (IDS selection)
enum class TargetTypeIDS : uint16_t {
    EA        = 2,
    GENERAL   = 3,
    RACE      = 4,
    CLASS     = 5,
    SPECIFIC  = 6,
    GENDER    = 7,
    ALIGNMEN  = 8,
    KIT       = 9,
};

// Base header flags (0x000C)
namespace SparkingFlags {
    static constexpr uint32_t ShowSparkle             = 1u << 0;
    static constexpr uint32_t UseZCoordinate          = 1u << 1;
    static constexpr uint32_t LoopSound10             = 1u << 2; // "0x10"
    static constexpr uint32_t LoopSound18             = 1u << 3; // "0x18"
    static constexpr uint32_t NoAffectDirectTarget    = 1u << 4;
    static constexpr uint32_t DrawBelowAnimate        = 1u << 5;
    static constexpr uint32_t AllowSavingGame_EE      = 1u << 6; // EE only
}

// Extended flags (0x002C, EE)
namespace ExtendedFlags {
    static constexpr uint32_t BounceFromWalls         = 1u << 0;
    static constexpr uint32_t PassTarget              = 1u << 1;
    static constexpr uint32_t DrawCentreVvcOnce       = 1u << 2;
    static constexpr uint32_t HitImmediately          = 1u << 3;
    static constexpr uint32_t FaceTarget              = 1u << 4;
    static constexpr uint32_t CurvedPath              = 1u << 5;
    static constexpr uint32_t StartRandomFrame        = 1u << 6;
    static constexpr uint32_t Pillar                  = 1u << 7;
    static constexpr uint32_t SemiTransparentTrailVef = 1u << 8;
    static constexpr uint32_t TintedTrailVef          = 1u << 9;
    static constexpr uint32_t MultipleProjectiles     = 1u << 10;
    static constexpr uint32_t DefaultSpellOnMissed    = 1u << 11;
    static constexpr uint32_t FallingPath             = 1u << 12;
    static constexpr uint32_t Comet                   = 1u << 13;
    static constexpr uint32_t LinedUpAoE              = 1u << 14;
    static constexpr uint32_t RectangularAoE          = 1u << 15;
    static constexpr uint32_t DrawBehindTarget        = 1u << 16;
    static constexpr uint32_t CastingGlowEffect       = 1u << 17;
    static constexpr uint32_t TravelDoor              = 1u << 18;
    static constexpr uint32_t StopFadeAfterHit        = 1u << 19;
    static constexpr uint32_t DisplayMessage          = 1u << 20;
    static constexpr uint32_t RandomPath              = 1u << 21;
    static constexpr uint32_t StartRandomSequence     = 1u << 22;
    static constexpr uint32_t ColourPulseOnHit        = 1u << 23;
    static constexpr uint32_t TouchProjectile         = 1u << 24;
    static constexpr uint32_t NegateFirstCreature     = 1u << 25;
    static constexpr uint32_t NegateSecondCreature    = 1u << 26;
    static constexpr uint32_t UseEitherIDS            = 1u << 27;
    static constexpr uint32_t DelayedPayload          = 1u << 28;
    static constexpr uint32_t LimitedPathCount        = 1u << 29;
    static constexpr uint32_t IWDStyleCheck           = 1u << 30; // use splprot.2da for first/second targets
    static constexpr uint32_t CasterAffected          = 1u << 31;
}

// BAM section flags (0x0100)
namespace BamFlags {
    static constexpr uint32_t EnableBamColouring      = 1u << 0; // palette at 0x11C
    static constexpr uint32_t EnableSmoke             = 1u << 1;
    // bit 2 unused
    static constexpr uint32_t EnableAreaLightingUsage = 1u << 3;
    static constexpr uint32_t EnableAreaHeightUsage   = 1u << 4;
    static constexpr uint32_t EnableShadow            = 1u << 5;
    static constexpr uint32_t EnableLightSpot         = 1u << 6;
    static constexpr uint32_t EnableBrightenFlags     = 1u << 7;
    static constexpr uint32_t LowLevelBrighten        = 1u << 8;
    static constexpr uint32_t HighLevelBrighten       = 1u << 9;
}

// Area flags (0x0200)
namespace AreaFlags {
    static constexpr uint32_t RemainsVisibleAtDest    = 1u << 0;
    static constexpr uint32_t TriggeredByInanimate    = 1u << 1;
    static constexpr uint32_t TriggeredOnCondition    = 1u << 2;
    static constexpr uint32_t TriggerDuringDelay      = 1u << 3;
    static constexpr uint32_t UseSecondaryProjectile  = 1u << 4;
    static constexpr uint32_t UseFragmentGraphics     = 1u << 5;
    static constexpr uint32_t TargetSelectionBit6     = 1u << 6; // 2-bit selection relative to caster
    static constexpr uint32_t TargetSelectionBit7     = 1u << 7;
    static constexpr uint32_t TriggersEqCastMageLvl   = 1u << 8;
    static constexpr uint32_t TriggersEqCastClericLvl = 1u << 9;
    static constexpr uint32_t UseVVC                  = 1u << 10;
    static constexpr uint32_t ConeShape               = 1u << 11;
    static constexpr uint32_t AffectThroughObstacles  = 1u << 12;
    static constexpr uint32_t CheckTriggersFromFrame30= 1u << 13; // requires bit 0
    static constexpr uint32_t DelayedExplosion        = 1u << 14;
    static constexpr uint32_t AffectOnlyOneTarget     = 1u << 15;
}

// Area extended flags (0x0240, EE)
namespace AreaExtendedFlags {
    static constexpr uint32_t PalettedRing            = 1u << 0;
    static constexpr uint32_t RandomSpeed             = 1u << 1;
    static constexpr uint32_t StartScattered          = 1u << 2;
    static constexpr uint32_t PalettedCentre          = 1u << 3;
    static constexpr uint32_t RepeatScattering        = 1u << 4;
    static constexpr uint32_t PalettedAnimation       = 1u << 5;
    // 6..8 unused
    static constexpr uint32_t OrientedFireballPuffs   = 1u << 9;
    static constexpr uint32_t UseHitDiceLookup        = 1u << 10;
    // 11..12 unused
    static constexpr uint32_t BlendedAreaRingAnim     = 1u << 13;
    static constexpr uint32_t GlowingAreaRingAnim     = 1u << 14;
    static constexpr uint32_t HitPointLimit           = 1u << 15;
}

/*
    Base PRO V1.0 header (0x0000..0x00FF) — 256 bytes
*/
struct PROBaseV1 {
    char signature[4];       // 0x0000 'PRO '
    char version[4];         // 0x0004 'V1.0'
    uint16_t projectileType; // 0x0008 1=no BAM, 2=single target, 3=area of effect
    uint16_t projectileSpeed;// 0x000A 0x01..0xFF
    uint32_t sparkingFlags;  // 0x000C See SparkingFlags

    ResRef8 wavcTravel;      // 0x0010 WAVC travelling
    ResRef8 wavcExplosion;   // 0x0018 WAVC explosion/reach destination
    ResRef8 vvcTravel;       // 0x0020 Travelling VEF/VVC/BAM

    uint16_t sparkColor;     // 0x0028 from sprkclr.2da (0..12; 0 crashes)

    // Enhanced Editions (present in layout; typically zero for classics)
    uint16_t projectileWidth;   // 0x002A
    uint32_t extendedFlags;     // 0x002C See ExtendedFlags
    uint32_t displayMessage;    // 0x0030 strref
    uint32_t pulseColor;        // 0x0034 byte order: blue, green, red, unused
    uint16_t colorSpeed;        // 0x0038
    uint16_t screenShake;       // 0x003A

    // First creature target (value/type)
    uint16_t firstTargetValue;  // 0x003C value (IDS or splprot.2da depending on bit 30)
    uint16_t firstTargetType;   // 0x003E type (IDS selector) unless bit 30 set

    // Second creature target (value/type)
    uint16_t secondTargetValue; // 0x0040
    uint16_t secondTargetType;  // 0x0042

    ResRef8  defaultSpell;      // 0x0044 SPL
    ResRef8  successSpell;      // 0x004C SPL

    // PSTEE-only fields (present in layout; zero for others)
    uint16_t angleIncreaseMin;      // 0x0054
    uint16_t angleIncreaseMax;      // 0x0056
    uint16_t curveMin;              // 0x0058
    uint16_t curveMax;              // 0x005A
    uint16_t thac0Bonus;            // 0x005C
    uint16_t thac0BonusNonActor;    // 0x005E
    uint16_t radiusMin;             // 0x0060
    uint16_t radiusMax;             // 0x0062

    uint8_t  unused[0x0100 - 0x0064]; // 0x0064..0x00FF (156 bytes)

    PROBaseV1() { std::memset(this, 0, sizeof(*this)); }
};

/*
    BAM section (0x0100..0x01FF) — 256 bytes (present if projectileType >= 2)
*/
struct PROBamV1 {
    uint32_t bamFlags;          // 0x0100 See BamFlags
    ResRef8  projectileBAM;     // 0x0104
    ResRef8  shadowBAM;         // 0x010C
    uint8_t  projectileSeq;     // 0x0114 BAM sequence
    uint8_t  shadowSeq;         // 0x0115 BAM sequence
    uint16_t lightSpotIntensity;// 0x0116
    uint16_t lightSpotWidth;    // 0x0118
    uint16_t lightSpotHeight;   // 0x011A
    ResRef8  paletteBMP;        // 0x011C (BMP)

    uint8_t  projectileColors[7]; // 0x0124
    uint8_t  smokePuffPeriod;     // 0x012B
    uint8_t  smokeColors[7];      // 0x012C
    uint8_t  faceTargetGranularity;//0x0133 (1, 5, 9, 16)

    int16_t  smokeAnimationID;  // 0x0134 animate.ids

    ResRef8  trailing1;         // 0x0136 BAM
    ResRef8  trailing2;         // 0x013E BAM
    ResRef8  trailing3;         // 0x0146 BAM
    uint16_t trailingSeq1;      // 0x014E
    uint16_t trailingSeq2;      // 0x0150
    uint16_t trailingSeq3;      // 0x0152

    uint32_t puffFlags;         // 0x0154 (bit0 Puff at target, bit1 Puff at source)
    uint8_t  unused[0x0200 - 0x0158]; // 0x0158..0x01FF (168 bytes)

    PROBamV1() { std::memset(this, 0, sizeof(*this)); }
};

/*
    Area section (0x0200..0x02FF) — 256 bytes (present if projectileType == 3)
*/
struct PROAreaV1 {
    uint32_t areaFlags;         // 0x0200 See AreaFlags
    uint16_t triggerRadius;     // 0x0204 (÷~8.5 => feet diameter)
    uint16_t areaOfEffect;      // 0x0206 (÷~8.5 => feet diameter)
    ResRef8  triggerSound;      // 0x0208 WAV
    uint16_t explosionDelay;    // 0x0210
    int16_t  fragmentAnimationID;//0x0212 animate.ids
    uint16_t secondaryProjectile;//0x0214 projectl.ids-1
    uint8_t  triggerCount;      // 0x0216 used if bits 8/9 at 0x200 not set
    uint8_t  explosionAnimation;// 0x0217 fireball.ids
    uint8_t  explosionColor;    // 0x0218
    uint8_t  unused1;           // 0x0219
    uint16_t explosionProjectile;//0x021A projectl.ids (played on affected creatures)
    ResRef8  explosionVVC;      // 0x021C VVC
    uint16_t coneWidth;         // 0x0224 (1..359)

    // EE-only additions
    uint16_t unusedEE;          // 0x0226
    ResRef8  spreadAnim;        // 0x0228 VEF/VVC/BAM
    ResRef8  ringAnim;          // 0x0230 VEF/VVC/BAM
    ResRef8  areaSound;         // 0x0238 WAV
    uint32_t areaExtendedFlags; // 0x0240 See AreaExtendedFlags
    uint16_t diceThrown;        // 0x0244
    uint16_t diceSides;         // 0x0246
    uint16_t animGranularity;   // 0x0248
    uint16_t animGranularityDiv;// 0x024A
    uint8_t  unused2[0x0300 - 0x024C]; // 0x024C..0x02FF (180 bytes)

    PROAreaV1() { std::memset(this, 0, sizeof(*this)); }
};

// Composite container for a PRO V1.0 file
struct PROV1File {
    PROBaseV1 base;
    PROBamV1  bam;   // meaningful only if base.projectileType >= 2
    PROAreaV1 area;  // meaningful only if base.projectileType == 3

    static constexpr size_t kSizeBase = 0x0100;
    static constexpr size_t kSizeBam  = 0x0100;
    static constexpr size_t kSizeArea = 0x0100;

    static size_t expectedSize(uint16_t projectileType) {
        switch (static_cast<ProjectileType>(projectileType)) {
            case ProjectileType::NoBam:        return 0x0100;
            case ProjectileType::SingleTarget:  return 0x0200;
            case ProjectileType::AreaOfEffect:  return 0x0300;
            default:                            return 0; // invalid
        }
    }

    // Deserialize from full file bytes
    static bool deserialize(const std::vector<uint8_t>& data, PROV1File& out, std::string& err) {
        err.clear();
        if (data.size() < kSizeBase) {
            err = "PRO: file too small for base header";
            return false;
        }

        // Copy base
        std::memcpy(&out.base, data.data(), kSizeBase);

        // Validate signature/version
        if (std::memcmp(out.base.signature, "PRO ", 4) != 0) {
            err = "PRO: bad signature";
            return false;
        }
        if (std::memcmp(out.base.version, "V1.0", 4) != 0) {
            err = "PRO: unsupported version";
            return false;
        }

        size_t need = expectedSize(out.base.projectileType);
        if (need == 0 || data.size() != need) {
            err = "PRO: file size does not match projectileType";
            return false;
        }

        // BAM section present?
        if (need >= 0x0200) {
            std::memcpy(&out.bam, data.data() + 0x0100, kSizeBam);
        } else {
            out.bam = PROBamV1{};
        }

        // Area section present?
        if (need >= 0x0300) {
            std::memcpy(&out.area, data.data() + 0x0200, kSizeArea);
        } else {
            out.area = PROAreaV1{};
        }

        return true;
    }

    // Serialize to file bytes
    std::vector<uint8_t> serialize(std::string& err) const {
        err.clear();
        size_t need = expectedSize(base.projectileType);
        if (need == 0) {
            err = "PRO: invalid projectileType for serialization";
            return {};
        }

        std::vector<uint8_t> out;
        out.resize(need, 0);

        // Base (0x0000..0x00FF)
        std::memcpy(out.data(), &base, kSizeBase);

        // BAM (0x0100..0x01FF) if present
        if (need >= 0x0200) {
            std::memcpy(out.data() + 0x0100, &bam, kSizeBam);
        }

        // Area (0x0200..0x02FF) if present
        if (need >= 0x0300) {
            std::memcpy(out.data() + 0x0200, &area, kSizeArea);
        }

        return out;
    }
};

#pragma pack(pop)

} // namespace ProjectIE4k

#endif // PROV1_HPP