
namespace ProjectIE4k {
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

} // namespace ProjectIE4k
