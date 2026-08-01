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

/* These values are part of the generated schema-v2 ABI. */
enum DioramaArchetype
{
    DIORAMA_ARCHETYPE_NONE,
    DIORAMA_ARCHETYPE_GROUND,
    DIORAMA_ARCHETYPE_VOID,
    DIORAMA_ARCHETYPE_WATER,
    DIORAMA_ARCHETYPE_SHALLOW_WATER,
    DIORAMA_ARCHETYPE_WATERFALL,
    DIORAMA_ARCHETYPE_CURRENT,
    DIORAMA_ARCHETYPE_HOT_SPRING,
    DIORAMA_ARCHETYPE_LEDGE,
    DIORAMA_ARCHETYPE_CLIFF,
    DIORAMA_ARCHETYPE_MOUND,
    DIORAMA_ARCHETYPE_WALL_VOLUME,
    DIORAMA_ARCHETYPE_BRIDGE,
    DIORAMA_ARCHETYPE_DECK,
    DIORAMA_ARCHETYPE_RAIL,
    DIORAMA_ARCHETYPE_SUPPORT,
    DIORAMA_ARCHETYPE_STAIRS_N,
    DIORAMA_ARCHETYPE_STAIRS_S,
    DIORAMA_ARCHETYPE_STAIRS_E,
    DIORAMA_ARCHETYPE_STAIRS_W,
    DIORAMA_ARCHETYPE_STAIRS_DOWN_N,
    DIORAMA_ARCHETYPE_STAIRS_DOWN_S,
    DIORAMA_ARCHETYPE_STAIRS_DOWN_E,
    DIORAMA_ARCHETYPE_STAIRS_DOWN_W,
    DIORAMA_ARCHETYPE_ROOF,
    DIORAMA_ARCHETYPE_TOP_SLAB,
    DIORAMA_ARCHETYPE_AWNING,
    DIORAMA_ARCHETYPE_BUILDING,
    DIORAMA_ARCHETYPE_CLAIM_ONLY,
    DIORAMA_ARCHETYPE_BILLBOARD,
    DIORAMA_ARCHETYPE_CUTOUT,
    DIORAMA_ARCHETYPE_CONSOLE,
    DIORAMA_ARCHETYPE_SIGNPOST,
    DIORAMA_ARCHETYPE_POST,
    DIORAMA_ARCHETYPE_COUNTER,
    DIORAMA_ARCHETYPE_TABLE,
    DIORAMA_ARCHETYPE_DESK,
    DIORAMA_ARCHETYPE_BED,
    DIORAMA_ARCHETYPE_BOOKCASE,
    DIORAMA_ARCHETYPE_RELIEF,
    DIORAMA_ARCHETYPE_ROUND_HULL,
    DIORAMA_ARCHETYPE_GROUPED_HULL,
    DIORAMA_ARCHETYPE_STUMP,
    DIORAMA_ARCHETYPE_TREE,
    DIORAMA_ARCHETYPE_FOREST_WALL,
    DIORAMA_ARCHETYPE_SHRUB,
    DIORAMA_ARCHETYPE_HEDGE,
    DIORAMA_ARCHETYPE_ROCK,
    DIORAMA_ARCHETYPE_BOULDER,
    DIORAMA_ARCHETYPE_GRASS,
    DIORAMA_ARCHETYPE_FLOWER,
    DIORAMA_ARCHETYPE_ANIMATED_CUTOUT
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

enum DioramaTerrainClass
{
    DIORAMA_TERRAIN_CLASS_NONE,
    DIORAMA_TERRAIN_CLASS_GROUND,
    DIORAMA_TERRAIN_CLASS_PATH,
    DIORAMA_TERRAIN_CLASS_SAND,
    DIORAMA_TERRAIN_CLASS_ASH,
    DIORAMA_TERRAIN_CLASS_ROCK,
    DIORAMA_TERRAIN_CLASS_PAVEMENT,
    DIORAMA_TERRAIN_CLASS_WOOD,
    DIORAMA_TERRAIN_CLASS_CARPET,
    DIORAMA_TERRAIN_CLASS_VOID,
    DIORAMA_TERRAIN_CLASS_WATER
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

enum DioramaCliffEdge
{
    DIORAMA_CLIFF_EDGE_NORTH = 1u << 0,
    DIORAMA_CLIFF_EDGE_EAST = 1u << 1,
    DIORAMA_CLIFF_EDGE_SOUTH = 1u << 2,
    DIORAMA_CLIFF_EDGE_WEST = 1u << 3
};

enum DioramaCliffCorner
{
    DIORAMA_CLIFF_CORNER_NORTH_WEST = 1u << 0,
    DIORAMA_CLIFF_CORNER_NORTH_EAST = 1u << 1,
    DIORAMA_CLIFF_CORNER_SOUTH_EAST = 1u << 2,
    DIORAMA_CLIFF_CORNER_SOUTH_WEST = 1u << 3
};

#define DIORAMA_MATERIAL_METATILE_SELF UINT16_C(0xFFFF)
#define DIORAMA_MAX_VISUAL_SURFACES 3
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
    uint8_t rotation;
    uint8_t flags;
};

struct DioramaResolvedSurface
{
    float bottomHeight;
    float topHeight;
    uint8_t gameplayElevation;
};

enum DioramaRuleSource
{
    DIORAMA_RULE_SOURCE_MAP,
    DIORAMA_RULE_SOURCE_TILESET,
    DIORAMA_RULE_SOURCE_BUILDING,
    DIORAMA_RULE_SOURCE_BEHAVIOR,
    DIORAMA_RULE_SOURCE_COLLISION_ELEVATION,
    DIORAMA_RULE_SOURCE_HEURISTIC,
    DIORAMA_RULE_SOURCE_FALLBACK,
    DIORAMA_RULE_SOURCE_PATTERN,
    DIORAMA_RULE_SOURCE_CONTEXT,
    DIORAMA_RULE_SOURCE_EVENT
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
    uint8_t archetype;
    uint8_t semanticPool;
    uint8_t semanticProfile;
    uint8_t terrainClass;
    uint8_t groundMode;
    uint8_t effectiveElevation;
    uint8_t surfaceCount;
    uint16_t baseMetatileId;
    uint16_t claimOwner;
    int16_t rulePriority;
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
    uint8_t cliffEdgeMask;
    uint8_t cliffBaseMask;
    uint8_t cliffTransitionMask;
    uint8_t cliffCornerMask;
    float groundHeight;
    float topHeight;
    float featureHeight;
    float structureBodyHeight;
    float structureRoofHeight;
    float structureSouthFacadeUnitHeight;
    struct DioramaResolvedSurface surfaces[DIORAMA_MAX_VISUAL_SURFACES];
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
const char *DioramaRules_GetSha256(void);
const struct DioramaGeneratedBuildingTemplate *DioramaRules_GetBuildingTemplate(uint16_t id);
bool DioramaRules_IsMapSupported(uint8_t mapGroup, uint8_t mapNum, uint16_t layoutId);
const char *DioramaRules_GetUnsupportedReason(uint8_t mapGroup, uint8_t mapNum,
                                              uint16_t layoutId);
bool DioramaRules_GetMapProfile(uint8_t mapGroup, uint8_t mapNum, uint16_t layoutId,
                                struct DioramaMapProfile *profile);
bool DioramaRules_ResolveCell(const struct DioramaSceneSnapshot *snapshot,
                              const struct DioramaCellSnapshot *cell,
                              struct DioramaResolvedCell *resolved);
void DioramaRules_ResolveGrid(const struct DioramaSceneSnapshot *snapshot,
                              struct DioramaResolvedCell *resolvedCells);
bool DioramaRules_ProfileOccupies(uint8_t profileId, uint8_t x, uint8_t y);
bool DioramaRules_ProfileIsFullCell(uint8_t profileId);
float DioramaRules_DefaultGroundHeight(uint8_t behavior);
float DioramaRules_ProfileHeight(uint8_t archetype, uint8_t behavior,
                                 float groundHeight, float featureHeight,
                                 uint8_t pixelX, uint8_t pixelY);
float DioramaRules_ObjectGroundHeight(const struct DioramaResolvedCell *resolved,
                                       uint8_t cellElevation, uint8_t objectElevation,
                                       uint8_t behavior,
                                       uint8_t pixelX, uint8_t pixelY);

#endif
