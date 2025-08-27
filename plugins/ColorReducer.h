#ifndef COLOR_REDUCER_H
#define COLOR_REDUCER_H

#include <vector>
#include <map>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <zlib.h>
#include <png.h>
#include <limits>
#include <unordered_set>
#include <unordered_map>

namespace ProjectIE4k {

// Simple color reduction using median cut algorithm
class ColorReducer {
    private:
        struct ColorBox {
            uint8_t minR, maxR, minG, maxG, minB, maxB;
            std::vector<uint32_t> colors;
            
            ColorBox() : minR(255), maxR(0), minG(255), maxG(0), minB(255), maxB(0) {}
            
            void addColor(uint32_t color) {
                uint8_t r = (color >> 16) & 0xFF;
                uint8_t g = (color >> 8) & 0xFF;
                uint8_t b = color & 0xFF;
                
                minR = std::min(minR, r);
                maxR = std::max(maxR, r);
                minG = std::min(minG, g);
                maxG = std::max(maxG, g);
                minB = std::min(minB, b);
                maxB = std::max(maxB, b);
                
                colors.push_back(color);
            }
            
            int getLongestAxis() const {
                int rangeR = maxR - minR;
                int rangeG = maxG - minG;
                int rangeB = maxB - minB;
                
                if (rangeR >= rangeG && rangeR >= rangeB) return 0; // R
                if (rangeG >= rangeB) return 1; // G
                return 2; // B
            }
            
            void split(ColorBox& box1, ColorBox& box2) {
                int axis = getLongestAxis();
                
                // Sort colors by the longest axis
                std::sort(colors.begin(), colors.end(), [axis](uint32_t a, uint32_t b) {
                    uint8_t valA, valB;
                    switch (axis) {
                        case 0: valA = (a >> 16) & 0xFF; valB = (b >> 16) & 0xFF; break;
                        case 1: valA = (a >> 8) & 0xFF; valB = (b >> 8) & 0xFF; break;
                        case 2: valA = a & 0xFF; valB = b & 0xFF; break;
                    }
                    return valA < valB;
                });
                
                // Split at median
                size_t mid = colors.size() / 2;
                box1.colors.assign(colors.begin(), colors.begin() + mid);
                box2.colors.assign(colors.begin() + mid, colors.end());
                
                // Recalculate bounds
                box1.recalculateBounds();
                box2.recalculateBounds();
            }
            
            void recalculateBounds() {
                if (colors.empty()) return;
                
                minR = maxR = (colors[0] >> 16) & 0xFF;
                minG = maxG = (colors[0] >> 8) & 0xFF;
                minB = maxB = colors[0] & 0xFF;
                
                for (uint32_t color : colors) {
                    uint8_t r = (color >> 16) & 0xFF;
                    uint8_t g = (color >> 8) & 0xFF;
                    uint8_t b = color & 0xFF;
                    
                    minR = std::min(minR, r);
                    maxR = std::max(maxR, r);
                    minG = std::min(minG, g);
                    maxG = std::max(maxG, g);
                    minB = std::min(minB, b);
                    maxB = std::max(maxB, b);
                }
            }
            
            uint32_t getAverageColor() const {
                if (colors.empty()) return 0;
                
                uint32_t sumR = 0, sumG = 0, sumB = 0;
                for (uint32_t color : colors) {
                    sumR += (color >> 16) & 0xFF;
                    sumG += (color >> 8) & 0xFF;
                    sumB += color & 0xFF;
                }
                
                uint8_t avgR = sumR / colors.size();
                uint8_t avgG = sumG / colors.size();
                uint8_t avgB = sumB / colors.size();
                
                return (0xFF << 24) | (avgR << 16) | (avgG << 8) | avgB; // Full alpha
            }
        };
        
    public:
        // got the idea from Near Infinity
        static bool medianCut(const std::vector<uint32_t>& pixels, size_t maxColors, 
                             std::vector<uint32_t>& palette, bool includeTransparent = true) {
            if (pixels.empty()) return false;
            
            // Collect unique colors (keep full ARGB)
            std::map<uint32_t, int> colorCount;
            for (uint32_t pixel : pixels) {
                if ((pixel >> 24) == 0) continue; // Skip transparent pixels
                colorCount[pixel]++; // Keep full ARGB color
            }
            
            // Handle completely transparent tiles
            if (colorCount.empty()) {
                palette.clear();
                if (includeTransparent) {
                    palette.push_back(0); // Transparent color
                }
                // Fill with black for remaining slots
                while (palette.size() < maxColors) {
                    palette.push_back(0);
                }
                return true;
            }
            
            // Create initial color box
            ColorBox initialBox;
            for (const auto& pair : colorCount) {
                initialBox.addColor(pair.first);
            }
            
            // Split boxes until we have enough colors
            std::vector<ColorBox> boxes = {initialBox};
            
            while (boxes.size() < maxColors && !boxes.empty()) {
                // Find the box with the most colors
                size_t largestBox = 0;
                for (size_t i = 1; i < boxes.size(); i++) {
                    if (boxes[i].colors.size() > boxes[largestBox].colors.size()) {
                        largestBox = i;
                    }
                }
                
                // Split the largest box
                if (boxes[largestBox].colors.size() <= 1) break;
                
                ColorBox box1, box2;
                boxes[largestBox].split(box1, box2);
                
                boxes[largestBox] = box1;
                boxes.push_back(box2);
            }
            
            // Create palette
            palette.clear();
            if (includeTransparent) {
                palette.push_back(0); // Transparent color
            }
            
            for (const ColorBox& box : boxes) {
                if (!box.colors.empty()) {
                    palette.push_back(box.getAverageColor());
                }
            }
            
            // Ensure we have exactly maxColors
            while (palette.size() < maxColors) {
                palette.push_back(0);
            }
            if (palette.size() > maxColors) {
                palette.resize(maxColors);
            }
            
            return true;
        }
        
        static int getNearestColor(uint32_t color, const std::vector<uint32_t>& palette, 
                                  int* exactMatch = nullptr) {
            if (palette.empty()) return 0;
            
            // Extract color components
            uint8_t a1 = (color >> 24) & 0xFF;
            uint8_t r1 = (color >> 16) & 0xFF;
            uint8_t g1 = (color >> 8) & 0xFF;
            uint8_t b1 = color & 0xFF;
            
            // Apply alpha blending (like Near Infinity does)
            if (a1 != 0xFF) {
                r1 = r1 * a1 / 255;
                g1 = g1 * a1 / 255;
                b1 = b1 * a1 / 255;
            }
            
            int bestIndex = 0;
            double bestDistance = std::numeric_limits<double>::max();
            
            for (size_t i = 0; i < palette.size(); i++) {
                uint32_t palColor = palette[i];
                uint8_t a2 = (palColor >> 24) & 0xFF;
                uint8_t r2 = (palColor >> 16) & 0xFF;
                uint8_t g2 = (palColor >> 8) & 0xFF;
                uint8_t b2 = palColor & 0xFF;
                
                // Apply alpha blending to palette color
                if (a2 != 0xFF) {
                    r2 = r2 * a2 / 255;
                    g2 = g2 * a2 / 255;
                    b2 = b2 * a2 / 255;
                }
                
                // Use weighted distance calculation (similar to Near Infinity's COLOR_DISTANCE_ARGB)
                // Note: alphaWeight = 0.0 means ignore alpha in distance calculation
                double dr = (r1 - r2) * 14.0;
                double dg = (g1 - g2) * 28.0;
                double db = (b1 - b2) * 6.0;
                double distance = dr * dr + dg * dg + db * db;
                
                if (distance < bestDistance) {
                    bestDistance = distance;
                    bestIndex = i;
                    
                    if (distance == 0) {
                        if (exactMatch) *exactMatch = i;
                        return i;
                    }
                }
            }
            
            if (exactMatch) *exactMatch = -1;
            return bestIndex;
        }
        
        // Magic Green transparency handling (Infinity Engine standard)
        // Magic Green = RGB(0, 255, 0) = 0x0000ff00
        
        /**
         * Check if a color is the magic green transparent color used in Infinity Engine
         * @param color ARGB color value
         * @return true if the color is magic green (0x0000ff00)
         */
        static bool isMagicGreen(uint32_t color) {
            return (color & 0x00FFFFFF) == 0x0000ff00; // RGB(0, 255, 0)
        }
        
        /**
         * Get the magic green transparent color
         * @return Magic green color (0x0000ff00)
         */
        static uint32_t getMagicGreen() {
            return 0x0000ff00; // RGB(0, 255, 0), alpha = 0
        }
        
        /**
         * Check if a BGRA palette entry is magic green
         * @param bgra 4-byte BGRA color array
         * @return true if the color is magic green
         */
        static bool isMagicGreenBGRA(const uint8_t bgra[4]) {
            return (bgra[0] == 0x00) && // Blue = 0
                   (bgra[1] == 0xFF) && // Green = 255
                   (bgra[2] == 0x00) && // Red = 0
                   (bgra[3] == 0x00);   // Alpha = 0
        }
        
        /**
         * Set a BGRA palette entry to magic green
         * @param bgra 4-byte BGRA color array to modify
         */
        static void setMagicGreenBGRA(uint8_t bgra[4]) {
            bgra[0] = 0x00; // Blue
            bgra[1] = 0xFF; // Green
            bgra[2] = 0x00; // Red
            bgra[3] = 0x00; // Alpha
        }
        
        /**
         * Convert BGRA palette entry to ARGB color with transparency handling
         * @param bgra 4-byte BGRA color array
         * @param index Palette index (0 is special for transparency)
         * @return ARGB color value
         */
        static uint32_t bgraToARGB(const uint8_t bgra[4], uint8_t index = 0) {
            uint32_t color = (bgra[2] << 16) | // R
                           (bgra[1] << 8)  | // G
                           bgra[0];           // B
            
            // Handle transparency like Near Infinity:
            // - If index 0 has magic green, make it transparent
            // - Otherwise, make all colors fully opaque
            if (index == 0 && isMagicGreenBGRA(bgra)) {
                return 0x00000000; // Fully transparent
            } else {
                return color | 0xFF000000; // Fully opaque
            }
        }
        
        /**
         * Convert ARGB color to BGRA palette entry
         * @param argb ARGB color value
         * @param bgra 4-byte BGRA color array to fill
         */
        static void argbToBGRA(uint32_t argb, uint8_t bgra[4]) {
            bgra[0] = (argb >> 0) & 0xFF;  // Blue
            bgra[1] = (argb >> 8) & 0xFF;  // Green
            bgra[2] = (argb >> 16) & 0xFF; // Red
            bgra[3] = (argb >> 24) & 0xFF; // Alpha
        }
        
        /**
         * Convert RGB components to ARGB color with magic green transparency handling
         * @param r Red component
         * @param g Green component  
         * @param b Blue component
         * @param a Alpha component
         * @param index Palette index (0 is special for transparency)
         * @return ARGB color value
         */
        static uint32_t rgbToARGB(uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t index = 0) {
            uint8_t bgra[4] = {b, g, r, a};
            return bgraToARGB(bgra, index);
        }
        
        /**
         * Create a palette with magic green reserved for transparency
         * @param pixels Source pixel data
         * @param maxColors Maximum number of colors (will create maxColors-1 + magic green)
         * @param palette Output palette (index 0 = magic green, indices 1+ = actual colors)
         * @return true if successful
         */
        static bool createPaletteWithMagicGreen(const std::vector<uint32_t>& pixels, 
                                               size_t maxColors, 
                                               std::vector<uint32_t>& palette) {
            // Fast path: if the tile already has <= maxColors-1 unique ARGB colors (ignoring fully transparent),
            // build the palette directly without median cut.
            std::unordered_set<uint32_t> unique;
            unique.reserve(512);
            for (uint32_t c : pixels) {
                if ((c >> 24) == 0) continue; // skip fully transparent; will map to magic green
                unique.insert(c);
                if (unique.size() > maxColors - 1) break;
            }

            std::vector<uint32_t> tempPalette;
            if (unique.size() <= maxColors - 1) {
                tempPalette.assign(unique.begin(), unique.end());
                // Optionally sort for determinism
                std::sort(tempPalette.begin(), tempPalette.end());
            } else {
                // Fallback to median cut for complex tiles
                if (!medianCut(pixels, maxColors - 1, tempPalette, false)) {
                    return false;
                }
            }
            
            // Create final palette with magic green at index 0
            palette.clear();
            palette.push_back(getMagicGreen()); // Index 0 = magic green
            
            // Add actual colors starting at index 1
            palette.insert(palette.end(), tempPalette.begin(), tempPalette.end());
            
            // Ensure we have exactly maxColors
            while (palette.size() < maxColors) {
                palette.push_back(0); // Fill with black
            }
            if (palette.size() > maxColors) {
                palette.resize(maxColors);
            }
            
            return true;
        }
        
        /**
         * Convert pixels to palette indices with magic green transparency handling
         * @param pixels Source pixel data
         * @param palette Palette (index 0 = magic green, indices 1+ = actual colors)
         * @param indices Output palette indices
         * @return true if successful
         */
        static bool pixelsToIndicesWithMagicGreen(const std::vector<uint32_t>& pixels,
                                                 const std::vector<uint32_t>& palette,
                                                 std::vector<uint8_t>& indices) {
            if (palette.empty()) return false;
            
            indices.clear();
            indices.reserve(pixels.size());
            
            // Create a palette for color matching (skip index 0 = magic green)
            std::vector<uint32_t> matchPalette;
            if (palette.size() > 1) {
                matchPalette.assign(palette.begin() + 1, palette.end());
            }

            // Build exact ARGB -> palette index map for fast exact matches
            std::unordered_map<uint32_t, uint8_t> exactMap;
            exactMap.reserve(matchPalette.size());
            for (size_t i = 0; i < matchPalette.size(); ++i) {
                // Index stored is i+1 to account for transparent index at 0
                exactMap.emplace(matchPalette[i], static_cast<uint8_t>(i + 1));
            }
            
            for (uint32_t pixel : pixels) {
                // Check if pixel is transparent (alpha = 0)
                if ((pixel >> 24) == 0) {
                    // Transparent pixel - use index 0 (magic green)
                    indices.push_back(0);
                } else {
                    // Opaque pixel - try exact match in O(1)
                    auto it = exactMap.find(pixel);
                    if (it != exactMap.end()) {
                        indices.push_back(it->second);
                    } else if (matchPalette.empty()) {
                        indices.push_back(0); // Fallback to transparent
                    } else {
                        int nearestIndex = getNearestColor(pixel, matchPalette);
                        indices.push_back(static_cast<uint8_t>(nearestIndex + 1)); // Add 1 to skip transparent index
                    }
                }
            }
            
            return true;
        }
    };
}
#endif // COLOR_REDUCER_H 