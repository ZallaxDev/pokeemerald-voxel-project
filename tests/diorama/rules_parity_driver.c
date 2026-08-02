#include <stdio.h>
#include <string.h>

#include "diorama/rules.generated.h"
#include "diorama/rules.h"

int main(void)
{
    size_t layoutIndex;

    for (layoutIndex = 0; layoutIndex < gDioramaLayoutV2Count; layoutIndex++)
    {
        const struct DioramaGeneratedLayoutV2 *layout = &gDioramaLayoutsV2[layoutIndex];
        uint32_t index;

        for (index = 0; index < layout->terrainRecordCount; index++)
        {
            const struct DioramaGeneratedTerrainV2 *record =
                &gDioramaTerrainV2[layout->terrainRecordOffset + index];
            struct DioramaSceneSnapshot snapshot;
            struct DioramaCellSnapshot cell;
            struct DioramaResolvedCell resolved;
            const char *evidence;
            const char *ambiguity;
            int heightQ16;

            memset(&snapshot, 0, sizeof(snapshot));
            memset(&cell, 0, sizeof(cell));
            snapshot.mapGroup = record->mapGroup;
            snapshot.mapNum = record->mapNumber;
            snapshot.mapLayoutId = layout->id;
            snapshot.visibleCellCount = 1;
            cell.mapX = record->cellOffset % layout->width;
            cell.mapY = record->cellOffset / layout->width;
            cell.sourceMapX = cell.mapX;
            cell.sourceMapY = cell.mapY;
            cell.sourceMapGroup = record->mapGroup;
            cell.sourceMapNum = record->mapNumber;
            cell.sourceLayoutId = layout->id;
            cell.metatileId = record->expectedMetatile;
            cell.flags = DIORAMA_CELL_SOURCE_VALID;
            cell.collision = (record->evidenceFlags & DIORAMA_EVIDENCE_COLLISION) != 0;
            cell.elevation = (record->evidenceFlags & DIORAMA_EVIDENCE_ELEVATION) != 0;
            cell.layerType = (record->evidenceFlags & DIORAMA_EVIDENCE_LAYER) != 0;
            snapshot.cells[0] = cell;
            if (!DioramaRules_ResolveCell(&snapshot, &cell, &resolved))
                return 2;
            evidence = DioramaRules_EvidenceDetails(resolved.evidenceDetailsId);
            ambiguity = DioramaRules_AmbiguityDetails(resolved.ambiguityDetailsId);
            heightQ16 = resolved.groundHeight < 0.0f
                      ? (int)(resolved.groundHeight * 16.0f)
                      : (int)(resolved.featureHeight * 16.0f);

            printf("%u\t%u\t%u\t%u\t%u\t%s\t%d\t%s\t%s\t%u\t%s\t%.9g\t%u\t%s\t%u\t%s\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%d\t%d\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%d\n",
                   layout->id, record->cellOffset, record->expectedMetatile,
                   record->mapGroup, record->mapNumber,
                   DioramaRules_ClassName(resolved.classifierClass), heightQ16,
                   DioramaRules_ArtModeName(resolved.artMode),
                   gDioramaPoolsV2[resolved.semanticPool - 1].logicalId, resolved.authored,
                   DioramaRules_ClassifierSourceName(resolved.classifierSource),
                   resolved.classifierConfidence, resolved.evidenceFlags,
                   evidence != NULL ? evidence : "", resolved.ambiguityFlags,
                   ambiguity != NULL ? ambiguity : "", resolved.groundMode,
                   resolved.baseMetatileId, resolved.shape, resolved.archetype,
                   resolved.terrainClass, resolved.source, resolved.planeAxis,
                   resolved.cliffEdgeMask, resolved.cliffBaseMask,
                   resolved.cliffTransitionMask, resolved.cliffCornerMask,
                   resolved.claimOwner, resolved.regionId, resolved.structureId,
                   resolved.structureTemplateId, resolved.structureX, resolved.structureY,
                   resolved.structureWidth, resolved.structureHeight,
                   resolved.structureLocalX, resolved.structureLocalY,
                   resolved.structureOwnerKind, resolved.doorFold, resolved.voidKind,
                   resolved.rulePriority);
        }
    }
    return 0;
}
