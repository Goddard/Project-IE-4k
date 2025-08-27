/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 */

/**
 * @file SClassID.h
 * Defines ID numbers identifying the various plugins.
 * Needed for loading the plugins on MS Windows.
 * @author The GemRB Project
 * @note slightly modified by PIE4K Project to include helper functions
 */

#ifndef SCLASS_ID_H
#define SCLASS_ID_H

#include <cstring>
#include <cstdio>
#include <string> // Added for std::string and std::transform
#include <algorithm> // Added for std::transform
#include <map> // Added for std::map
#include <vector> // Added for std::vector

namespace ProjectIE4k {

/** Type of plugin ID numbers */
using SClass_ID = unsigned long;

#define IE_2DA_CLASS_ID  0x000003F4
#define IE_ACM_CLASS_ID  0x00010000
#define IE_ARE_CLASS_ID  0x000003F2
#define IE_BAM_CLASS_ID  0x000003E8
#define IE_BCS_CLASS_ID  0x000003EF
#define IE_BS_CLASS_ID   0x100003EF // 0x3f9 in the original, but we use a high bit so we can treat it like bcs
#define IE_BIF_CLASS_ID  0x00020000
#define IE_BIO_CLASS_ID  0x000003FE //also .res
#define IE_BMP_CLASS_ID  0x00000001
#define IE_PNG_CLASS_ID  0x00000003
#define IE_CHR_CLASS_ID  0x000003FA
#define IE_CHU_CLASS_ID  0x000003EA
#define IE_CRE_CLASS_ID  0x000003F1
#define IE_DLG_CLASS_ID  0x000003F3
#define IE_EFF_CLASS_ID  0x000003F8
#define IE_FNT_CLASS_ID  0x00000400
#define IE_GAM_CLASS_ID  0x000003F5
#define IE_GLSL_CLASS_ID 0x00000405
#define IE_GUI_CLASS_ID  0x00000402
#define IE_IDS_CLASS_ID  0x000003F0
#define IE_INI_CLASS_ID  0x00000802
#define IE_ITM_CLASS_ID  0x000003ED
#define IE_LUA_CLASS_ID  0x00000409
#define IE_MENU_CLASS_ID 0x00000408 // gap of 3
#define IE_MOS_CLASS_ID  0x000003EC
#define IE_MUS_CLASS_ID  0x00040000
#define IE_MVE_CLASS_ID  0x00000002
#define IE_BIK_CLASS_ID  0x00FFFFFF
#define IE_OGG_CLASS_ID  0x00000007 // gemrb extension
#define IE_PLT_CLASS_ID  0x00000006
#define IE_PRO_CLASS_ID  0x000003FD
#define IE_PVRZ_CLASS_ID 0x00000404
#define IE_SAV_CLASS_ID  0x00050000
#define IE_SPL_CLASS_ID  0x000003EE
#define IE_SQL_CLASS_ID  0x00000403 // yep, no 0x402
#define IE_SRC_CLASS_ID  0x00000803
#define IE_STO_CLASS_ID  0x000003F6
// TGA was at 0x3, but never used
#define IE_TIS_CLASS_ID        0x000003EB
#define IE_TLK_CLASS_ID        0x00060000
#define IE_TOH_CLASS_ID        0x00070000 // 0x407 in ee, merged with tot as toh v2
#define IE_TOT_CLASS_ID        0x00080000 // 0x406 in ee, but unused
#define IE_TTF_CLASS_ID        0x0000040A
#define IE_VAR_CLASS_ID        0x00090000
#define IE_VEF_CLASS_ID        0x000003FC
#define IE_VVC_CLASS_ID        0x000003FB
#define IE_WAV_CLASS_ID        0x00000004
#define IE_WBM_CLASS_ID        0x000003FF
#define IE_WED_CLASS_ID        0x000003E9
#define IE_WFX_CLASS_ID        0x00000005
#define IE_WMP_CLASS_ID        0x000003F7
#define IE_SCRIPT_CLASS_ID     0x000D0000
#define IE_GUI_SCRIPT_CLASS_ID 0x000E0000

namespace SClass {
    
    // Structure to hold resource type information
    struct ResourceTypeInfo {
        SClass_ID id;
        const char* extension;
        const char* description;
    };
    
    // Single source of truth for all resource type mappings
    inline const std::map<SClass_ID, ResourceTypeInfo>& getResourceTypeMap() {
        static const std::map<SClass_ID, ResourceTypeInfo> resourceTypeMap = {
            {IE_2DA_CLASS_ID, {IE_2DA_CLASS_ID, "2DA", "2DA Data"}},
            {IE_ACM_CLASS_ID, {IE_ACM_CLASS_ID, "ACM", "ACM Audio"}},
            {IE_ARE_CLASS_ID, {IE_ARE_CLASS_ID, "ARE", "Area"}},
            {IE_BAM_CLASS_ID, {IE_BAM_CLASS_ID, "BAM", "BAM Animation"}},
            {IE_BCS_CLASS_ID, {IE_BCS_CLASS_ID, "BCS", "BCS Script"}},
            {IE_BS_CLASS_ID, {IE_BS_CLASS_ID, "BS", "BS Script"}},
            {IE_BIF_CLASS_ID, {IE_BIF_CLASS_ID, "BIF", "BIF Archive"}},
            {IE_BIO_CLASS_ID, {IE_BIO_CLASS_ID, "BIO", "BIO Resource"}},
            {IE_BMP_CLASS_ID, {IE_BMP_CLASS_ID, "BMP", "BMP Image"}},
            {IE_PNG_CLASS_ID, {IE_PNG_CLASS_ID, "PNG", "PNG Image"}},
            {IE_CHR_CLASS_ID, {IE_CHR_CLASS_ID, "CHR", "Character"}},
            {IE_CHU_CLASS_ID, {IE_CHU_CLASS_ID, "CHU", "CHU Interface"}},
            {IE_CRE_CLASS_ID, {IE_CRE_CLASS_ID, "CRE", "Creature"}},
            {IE_DLG_CLASS_ID, {IE_DLG_CLASS_ID, "DLG", "Dialog"}},
            {IE_EFF_CLASS_ID, {IE_EFF_CLASS_ID, "EFF", "Effect"}},
            {IE_FNT_CLASS_ID, {IE_FNT_CLASS_ID, "FNT", "Font"}},
            {IE_GAM_CLASS_ID, {IE_GAM_CLASS_ID, "GAM", "Game"}},
            {IE_GLSL_CLASS_ID, {IE_GLSL_CLASS_ID, "GLSL", "GLSL Shader"}},
            {IE_GUI_CLASS_ID, {IE_GUI_CLASS_ID, "GUI", "GUI"}},
            {IE_IDS_CLASS_ID, {IE_IDS_CLASS_ID, "IDS", "IDS Data"}},
            {IE_INI_CLASS_ID, {IE_INI_CLASS_ID, "INI", "INI Config"}},
            {IE_ITM_CLASS_ID, {IE_ITM_CLASS_ID, "ITM", "Item"}},
            {IE_LUA_CLASS_ID, {IE_LUA_CLASS_ID, "LUA", "Lua Script"}},
            {IE_MENU_CLASS_ID, {IE_MENU_CLASS_ID, "MENU", "Menu"}},
            {IE_MOS_CLASS_ID, {IE_MOS_CLASS_ID, "MOS", "MOS Image"}},
            {IE_MUS_CLASS_ID, {IE_MUS_CLASS_ID, "MUS", "Music"}},
            {IE_MVE_CLASS_ID, {IE_MVE_CLASS_ID, "MVE", "Movie"}},
            {IE_BIK_CLASS_ID, {IE_BIK_CLASS_ID, "BIK", "BIK Video"}},
            {IE_OGG_CLASS_ID, {IE_OGG_CLASS_ID, "OGG", "OGG Audio"}},
            {IE_PLT_CLASS_ID, {IE_PLT_CLASS_ID, "PLT", "Palette"}},
            {IE_PRO_CLASS_ID, {IE_PRO_CLASS_ID, "PRO", "Projectile"}},
            {IE_PVRZ_CLASS_ID, {IE_PVRZ_CLASS_ID, "PVRZ", "PVRZ Texture"}},
            {IE_SAV_CLASS_ID, {IE_SAV_CLASS_ID, "SAV", "Save"}},
            {IE_SPL_CLASS_ID, {IE_SPL_CLASS_ID, "SPL", "Spell"}},
            {IE_SQL_CLASS_ID, {IE_SQL_CLASS_ID, "SQL", "SQL"}},
            {IE_SRC_CLASS_ID, {IE_SRC_CLASS_ID, "SRC", "Source"}},
            {IE_STO_CLASS_ID, {IE_STO_CLASS_ID, "STO", "Store"}},
            {IE_TIS_CLASS_ID, {IE_TIS_CLASS_ID, "TIS", "TIS Image"}},
            {IE_TLK_CLASS_ID, {IE_TLK_CLASS_ID, "TLK", "Talk"}},
            {IE_TOH_CLASS_ID, {IE_TOH_CLASS_ID, "TOH", "TOH"}},
            {IE_TOT_CLASS_ID, {IE_TOT_CLASS_ID, "TOT", "TOT"}},
            {IE_TTF_CLASS_ID, {IE_TTF_CLASS_ID, "TTF", "TTF Font"}},
            {IE_VAR_CLASS_ID, {IE_VAR_CLASS_ID, "VAR", "Variable"}},
            {IE_VEF_CLASS_ID, {IE_VEF_CLASS_ID, "VEF", "VEF"}},
            {IE_VVC_CLASS_ID, {IE_VVC_CLASS_ID, "VVC", "VVC"}},
            {IE_WAV_CLASS_ID, {IE_WAV_CLASS_ID, "WAV", "WAV Audio"}},
            {IE_WBM_CLASS_ID, {IE_WBM_CLASS_ID, "WBM", "WBM"}},
            {IE_WED_CLASS_ID, {IE_WED_CLASS_ID, "WED", "WED Image"}},
            {IE_WFX_CLASS_ID, {IE_WFX_CLASS_ID, "WFX", "WFX"}},
            {IE_WMP_CLASS_ID, {IE_WMP_CLASS_ID, "WMP", "World Map"}},
            {IE_SCRIPT_CLASS_ID, {IE_SCRIPT_CLASS_ID, "SCRIPT", "Script"}},
            {IE_GUI_SCRIPT_CLASS_ID, {IE_GUI_SCRIPT_CLASS_ID, "GUI_SCRIPT", "GUI Script"}}
        };
        return resourceTypeMap;
    }
    
    inline const char* getExtension(SClass_ID resourceType) {
        const auto& map = getResourceTypeMap();
        auto it = map.find(resourceType);
        return (it != map.end()) ? it->second.extension : "UNKNOWN";
    }
    
    inline const char* getExtensionWithDot(SClass_ID resourceType) {
        const char* extension = getExtension(resourceType);
        if (strcmp(extension, "UNKNOWN") == 0) {
            return "";
        }
        static char buffer[16];
        snprintf(buffer, sizeof(buffer), ".%s", extension);
        return buffer;
    }
    
    inline SClass_ID getResourceTypeFromExtension(const std::string& extension) {
        // Convert extension to lowercase for case-insensitive comparison
        std::string ext = extension;
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        // Remove leading dot if present
        if (!ext.empty() && ext[0] == '.') {
            ext = ext.substr(1);
        }
        
        // Generate extension-to-ID mapping from the main resource type map
        const auto& resourceMap = getResourceTypeMap();
        for (const auto& [id, info] : resourceMap) {
            std::string mapExt = info.extension;
            std::transform(mapExt.begin(), mapExt.end(), mapExt.begin(), ::tolower);
            if (mapExt == ext) {
                return id;
            }
        }
        
        return 0; // Unknown extension
    }
    
    // Additional utility functions that can now be easily added
    inline const char* getDescription(SClass_ID resourceType) {
        const auto& map = getResourceTypeMap();
        auto it = map.find(resourceType);
        return (it != map.end()) ? it->second.description : "Unknown";
    }
    
    inline bool isValidResourceType(SClass_ID resourceType) {
        const auto& map = getResourceTypeMap();
        return map.find(resourceType) != map.end();
    }
    
    inline std::vector<SClass_ID> getAllResourceTypes() {
        const auto& map = getResourceTypeMap();
        std::vector<SClass_ID> types;
        types.reserve(map.size());
        for (const auto& [id, info] : map) {
            types.push_back(id);
        }
        return types;
    }
}

}

#endif
