#ifndef GUARD_DIORAMA_RULES_H
#define GUARD_DIORAMA_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "diorama/scene_snapshot.h"

enum DioramaShape
{
    DIORAMA_SHAPE_FLAT,
    DIORAMA_SHAPE_EXTRUDED,
    DIORAMA_SHAPE_CLIFF,
    DIORAMA_SHAPE_LEDGE,
    DIORAMA_SHAPE_STAIRS,
    DIORAMA_SHAPE_WATER,
    DIORAMA_SHAPE_BRIDGE,
    DIORAMA_SHAPE_BILLBOARD,
    DIORAMA_SHAPE_CUTOUT,
    DIORAMA_SHAPE_ROOF,
    DIORAMA_SHAPE_BUILDING_PART,
    DIORAMA_SHAPE_HIDDEN
};

enum DioramaRoofProfile
{
    DIORAMA_ROOF_NONE,
    DIORAMA_ROOF_FLAT,
    DIORAMA_ROOF_GABLE_X,
    DIORAMA_ROOF_GABLE_Z
};

enum DioramaMaterialLayer
{
    DIORAMA_MATERIAL_FULL,
    DIORAMA_MATERIAL_BASE,
    DIORAMA_MATERIAL_FOREGROUND,
    DIORAMA_MATERIAL_NONE = 0xFF
};

enum DioramaMaterialFace
{
    DIORAMA_MATERIAL_FACE_TOP,
    DIORAMA_MATERIAL_FACE_NORTH,
    DIORAMA_MATERIAL_FACE_EAST,
    DIORAMA_MATERIAL_FACE_SOUTH,
    DIORAMA_MATERIAL_FACE_WEST,
    DIORAMA_MATERIAL_FACE_PLANE,
    DIORAMA_MATERIAL_FACE_COUNT
};

enum DioramaPlaneAxis
{
    DIORAMA_PLANE_AXIS_X,
    DIORAMA_PLANE_AXIS_Z,
    DIORAMA_PLANE_AXIS_CROSS
};

#define DIORAMA_MATERIAL_METATILE_SELF UINT16_C(0xFFFF)
#define DIORAMA_BUILDING_MAX_FACADE_ROWS 4
#define DIORAMA_BUILDING_PIXELS_PER_CELL 16
#define DIORAMA_BUILDING_MAX_PROFILE_COLUMNS (32 * DIORAMA_BUILDING_PIXELS_PER_CELL + 1)

enum DioramaBuildingFacadeFit
{
    DIORAMA_BUILDING_FACADE_FIT_NONE,
    DIORAMA_BUILDING_FACADE_FIT_NATURAL
};

struct DioramaFaceMaterial
{
    uint16_t metatileId;
    uint8_t layer;
};

enum DioramaRuleSource
{
    DIORAMA_RULE_SOURCE_MAP,
    DIORAMA_RULE_SOURCE_TILESET,
    DIORAMA_RULE_SOURCE_BUILDING,
    DIORAMA_RULE_SOURCE_BEHAVIOR,
    DIORAMA_RULE_SOURCE_COLLISION_ELEVATION,
    DIORAMA_RULE_SOURCE_HEURISTIC,
    DIORAMA_RULE_SOURCE_FALLBACK
};

enum DioramaTilesetId
{
    DIORAMA_TILESET_UNKNOWN,
    DIORAMA_TILESET_GENERAL,
    DIORAMA_TILESET_PETALBURG,
    DIORAMA_TILESET_BUILDING,
    DIORAMA_TILESET_BRENDANS_MAYS_HOUSE,
    DIORAMA_TILESET_LAB
};

enum DioramaCameraProfile
{
    DIORAMA_CAMERA_EXTERIOR,
    DIORAMA_CAMERA_INTERIOR
};

struct DioramaMapProfile
{
    uint8_t cameraProfile;
    float cameraPitch;
    float cameraFocalLength;
};

struct DioramaRuleDefinition
{
    uint8_t shape;
    uint8_t profile;
    uint8_t planeAxis;
    uint16_t baseMetatileId;
    float groundHeight;
    float height;
    struct DioramaFaceMaterial materials[DIORAMA_MATERIAL_FACE_COUNT];
};

struct DioramaResolvedCell
{
    uint8_t shape;
    uint8_t profile;
    uint8_t source;
    uint8_t planeAxis;
    uint16_t baseMetatileId;
    uint16_t structureId;
    uint16_t structureTemplateId;
    int16_t structureX;
    int16_t structureY;
    uint8_t structureWidth;
    uint8_t structureHeight;
    uint8_t structureRoofRows;
    uint8_t structureLocalX;
    uint8_t structureLocalY;
    uint8_t structureSouthFacadeRows;
    float groundHeight;
    float topHeight;
    float featureHeight;
    float structureBodyHeight;
    float structureRoofHeight;
    float structureSouthFacadeUnitHeight;
    struct DioramaFaceMaterial materials[DIORAMA_MATERIAL_FACE_COUNT];
};

struct DioramaGeneratedLayoutRule
{
    uint16_t layoutId;
    uint8_t primaryTileset;
    uint8_t secondaryTileset;
};

struct DioramaGeneratedMapRule
{
    uint8_t mapGroup;
    uint8_t mapNum;
    uint16_t layoutId;
    struct DioramaMapProfile profile;
};

struct DioramaGeneratedBehaviorRule
{
    uint8_t behavior;
    struct DioramaRuleDefinition rule;
};

struct DioramaGeneratedTilesetRule
{
    uint8_t tileset;
    uint16_t metatileId;
    struct DioramaRuleDefinition rule;
};

struct DioramaGeneratedMapOverride
{
    uint8_t mapGroup;
    uint8_t mapNum;
    int16_t x;
    int16_t y;
    struct DioramaRuleDefinition rule;
};

struct DioramaGeneratedBuildingTemplate
{
    uint16_t id;
    uint8_t width;
    uint8_t height;
    uint8_t roofRows;
    uint8_t profile;
    uint8_t southFacadeRows;
    uint8_t facadeFit;
    uint8_t roofSlabPixels;
    uint8_t roofEaveSouthPixels;
    uint16_t roofProfileOffset;
    uint16_t roofProfileCount;
    float bodyHeight;
    float roofHeight;
    float southFacadeUnitHeight;
    struct DioramaFaceMaterial materials[DIORAMA_MATERIAL_FACE_COUNT];
};

struct DioramaGeneratedBuildingPlacement
{
    uint8_t mapGroup;
    uint8_t mapNum;
    uint16_t templateId;
    int16_t x;
    int16_t y;
};

uint32_t DioramaRules_GetGeneration(void);
const struct DioramaGeneratedBuildingTemplate *DioramaRules_GetBuildingTemplate(uint16_t id);
bool DioramaRules_IsMapSupported(uint8_t mapGroup, uint8_t mapNum, uint16_t layoutId);
bool DioramaRules_GetMapProfile(uint8_t mapGroup, uint8_t mapNum, uint16_t layoutId,
                                struct DioramaMapProfile *profile);
bool DioramaRules_ResolveCell(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaCellSnapshot *cell,
                              struct DioramaResolvedCell *resolved);
void DioramaRules_ResolveGrid(const struct DioramaSceneSnapshot *snapshot,
                              struct DioramaResolvedCell *resolvedCells);
float DioramaRules_DefaultGroundHeight(uint8_t behavior);

#endif
