

#pragma once

#include <cstdint>
#include <vector>
#include <cstring>
#include <string>

namespace ProjectIE4k {
#pragma pack(push, 1)
// CHU V1 Header structure (per IESDP)
struct CHUHeader {
    char signature[4];      // 'CHUI'
    char version[4];        // 'V1 '
    uint32_t windowCount;
    uint32_t controlTableOffset;
    uint32_t windowOffset;
};

// Window entry
struct CHUWindow {
    uint16_t windowID;
    uint16_t unknown;
    uint16_t x, y;
    uint16_t width, height;
    uint16_t backgroundFlag;
    uint16_t controlCount;
    char backgroundResRef[8];
    uint16_t firstControlIndex;
    uint16_t unknown2;
};

// Control table entry
struct CHUControlTableEntry {
    uint32_t controlOffset;
    uint32_t controlLength;
};

// Common control fields
struct CHUControlCommon {
    uint32_t controlID;
    uint16_t x, y;
    uint16_t width, height;
    uint8_t type;
    uint8_t unknown;
};

// Type 0: Button/Toggle/Pixmap
struct CHUControlButton {
    CHUControlCommon common;
    char bamResRef[8];
    uint8_t animCycle;
    uint8_t textJustifyFlags;
    uint8_t frameUnpressed;
    uint8_t anchorX1;
    uint8_t framePressed;
    uint8_t anchorX2;
    uint8_t frameSelected;
    uint8_t anchorY1;
    uint8_t frameDisabled;
    uint8_t anchorY2;
};

// Type 2: Slider
struct CHUControlSlider {
    CHUControlCommon common;
    char mosResRef[8];
    char bamResRef[8];
    uint16_t cycleNumber;
    uint16_t frameUngrabbed;
    uint16_t frameGrabbed;
    uint16_t knobXOffset;
    uint16_t knobYOffset;
    uint16_t knobJumpWidth;
    uint16_t knobJumpCount;
    uint16_t unknown1;
    uint16_t unknown2;
    uint16_t unknown3;
    uint16_t unknown4;
};

// Type 3: TextEdit
struct CHUControlTextEdit {
    CHUControlCommon common;
    char mosResRef1[8];
    char mosResRef2[8];
    char mosResRef3[8];
    char bamResRef[8];
    uint16_t carotAnimCycle;
    uint16_t carotAnimFrame;
    uint16_t xCoord;
    uint16_t yCoord;
    uint32_t scrollbarControlID;
    char bamFontResRef[8];
    uint16_t unknown;
    char initialText[32];
    uint16_t maxInputLength;
    uint32_t textCase;
};

// Type 5: TextArea
struct CHUControlTextArea {
    CHUControlCommon common;
    char bamFontInitials[8];
    char bamFontMain[8];
    uint32_t color1;
    uint32_t color2;
    uint32_t color3;
    uint32_t scrollbarControlID;
};

// Type 6: Label
struct CHUControlLabel {
    CHUControlCommon common;
    uint32_t strref;
    char bamFontResRef[8];
    uint32_t color1;
    uint32_t color2;
    uint16_t subtype;
};

// Type 7: Scrollbar
struct CHUControlScrollbar {
    CHUControlCommon common;
    char bamResRef[8];
    uint16_t cycleNumber;
    uint16_t frameUpUnpressed;
    uint16_t frameUpPressed;
    uint16_t frameDownUnpressed;
    uint16_t frameDownPressed;
    uint16_t frameTrough;
    uint16_t frameSlider;
    uint32_t textAreaControlID;
};

// Control variant (for storage)
struct CHUControl {
    CHUControlCommon common;
    // Use a union or std::variant in implementation for type-specific data
};
#pragma pack(pop)

// High-level container for CHU V1 files
struct CHUV1File {
    CHUHeader header{};
    std::vector<CHUWindow> windows;
    std::vector<CHUControlTableEntry> controlTable;
    std::vector<std::vector<uint8_t>> controls; // raw control blobs aligned with controlTable

    bool deserialize(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(CHUHeader)) return false;
        std::memcpy(&header, data.data(), sizeof(CHUHeader));
        if (std::string(header.signature, 4) != std::string("CHUI", 4)) return false;
        // Some engines use "V1 " with trailing space
        if (!(std::string(header.version, 4) == std::string("V1 ", 3) || std::string(header.version, 4) == std::string("V1 ", 4))) {
            // still accept as long as layout matches
        }

        // Read windows
        if (header.windowOffset + header.windowCount * sizeof(CHUWindow) > data.size()) return false;
        windows.resize(header.windowCount);
        std::memcpy(windows.data(), data.data() + header.windowOffset, header.windowCount * sizeof(CHUWindow));

        // Compute total controls from windows
        uint32_t totalControls = 0;
        for (const auto &w : windows) totalControls += static_cast<uint32_t>(w.controlCount);

        // Read control table
        if (header.controlTableOffset + totalControls * sizeof(CHUControlTableEntry) > data.size()) return false;
        controlTable.resize(totalControls);
        std::memcpy(controlTable.data(), data.data() + header.controlTableOffset, totalControls * sizeof(CHUControlTableEntry));

        // Read control blobs
        controls.resize(totalControls);
        for (uint32_t i = 0; i < totalControls; ++i) {
            const auto &cte = controlTable[i];
            uint64_t off = cte.controlOffset;
            uint64_t len = cte.controlLength;
            if (off + len > data.size()) return false;
            controls[i] = std::vector<uint8_t>(data.begin() + off, data.begin() + off + len);
        }
        return true;
    }

    std::vector<uint8_t> serialize() const {
        // Layout: [Header][Windows][ControlTable][Controls...]
        std::vector<uint8_t> out;
        out.resize(sizeof(CHUHeader));
        CHUHeader h = header;

        // Windows
        uint32_t winOffset = static_cast<uint32_t>(out.size());
        if (!windows.empty()) {
            const uint8_t* start = reinterpret_cast<const uint8_t*>(windows.data());
            out.insert(out.end(), start, start + windows.size() * sizeof(CHUWindow));
        }

        // Control table
        uint32_t ctOffset = static_cast<uint32_t>(out.size());
        std::vector<CHUControlTableEntry> ct = controlTable; // will adjust offsets
        if (!ct.empty()) {
            const uint8_t* start = reinterpret_cast<const uint8_t*>(ct.data());
            out.insert(out.end(), start, start + ct.size() * sizeof(CHUControlTableEntry));
        }

        // Controls data, update offsets/lengths
        for (size_t i = 0; i < controls.size(); ++i) {
            ct[i].controlOffset = static_cast<uint32_t>(out.size());
            ct[i].controlLength = static_cast<uint32_t>(controls[i].size());
            out.insert(out.end(), controls[i].begin(), controls[i].end());
        }

        // Rewrite adjusted control table
        if (!ct.empty()) {
            std::memcpy(out.data() + ctOffset, ct.data(), ct.size() * sizeof(CHUControlTableEntry));
        }

        // Finalize header
        h.windowOffset = winOffset;
        h.controlTableOffset = ctOffset;
        h.windowCount = static_cast<uint32_t>(windows.size());
        std::memcpy(out.data(), &h, sizeof(CHUHeader));
        return out;
    }
};

} // namespace ProjectIE4k
