#ifdef ENABLE_DIORAMA

#include <stdlib.h>
#include <string.h>

#include "diorama/occupancy.h"

static int CompareSpans(const void *leftValue, const void *rightValue)
{
    const struct DioramaOccupancySpan *left = leftValue;
    const struct DioramaOccupancySpan *right = rightValue;

#define COMPARE_FIELD(field) do { if (left->field != right->field) return left->field < right->field ? -1 : 1; } while (0)
    COMPARE_FIELD(x);
    COMPARE_FIELD(z);
    COMPARE_FIELD(yMin);
    COMPARE_FIELD(yMax);
    COMPARE_FIELD(sourceCellX);
    COMPARE_FIELD(sourceCellY);
#undef COMPARE_FIELD
    return memcmp(left->material, right->material, sizeof(left->material));
}

static bool SameProvenance(const struct DioramaOccupancySpan *left,
                           const struct DioramaOccupancySpan *right)
{
    return left->sourceCellX == right->sourceCellX
        && left->sourceCellY == right->sourceCellY
        && left->flags == right->flags
        && memcmp(left->material, right->material, sizeof(left->material)) == 0;
}

bool DioramaOccupancy_Canonicalize(struct DioramaOccupancySpan *spans,
                                   uint32_t *spanCount)
{
    uint32_t read;
    uint32_t write = 0;

    if (spans == NULL || spanCount == NULL)
        return false;
    for (read = 0; read < *spanCount; read++)
        if (spans[read].yMin >= spans[read].yMax)
            return false;
    qsort(spans, *spanCount, sizeof(*spans), CompareSpans);
    for (read = 0; read < *spanCount; read++)
    {
        if (write != 0
         && spans[write - 1].x == spans[read].x
         && spans[write - 1].z == spans[read].z
         && spans[read].yMin <= spans[write - 1].yMax)
        {
            if (!SameProvenance(&spans[write - 1], &spans[read]))
                return false;
            if (spans[read].yMax > spans[write - 1].yMax)
                spans[write - 1].yMax = spans[read].yMax;
        }
        else
        {
            if (write != read)
                spans[write] = spans[read];
            write++;
        }
    }
    *spanCount = write;
    return true;
}

static void FindColumn(const struct DioramaOccupancySpan *spans, uint32_t spanCount,
                       int32_t x, int32_t z, uint32_t *begin, uint32_t *end)
{
    uint32_t low = 0;
    uint32_t high = spanCount;

    while (low < high)
    {
        uint32_t middle = low + (high - low) / 2;
        if (spans[middle].x < x || (spans[middle].x == x && spans[middle].z < z))
            low = middle + 1;
        else
            high = middle;
    }
    *begin = low;
    while (low < spanCount && spans[low].x == x && spans[low].z == z)
        low++;
    *end = low;
}

static bool IsOccupied(const struct DioramaOccupancySpan *spans,
                       uint32_t begin, uint32_t end, int16_t y)
{
    uint32_t i;
    for (i = begin; i < end; i++)
        if (spans[i].yMin <= y && y < spans[i].yMax)
            return true;
    return false;
}

static bool AppendFace(struct DioramaShellFace *faces, uint32_t capacity,
                       uint32_t *count, const struct DioramaOccupancySpan *span,
                       uint8_t face, int32_t plane, int32_t uMin, int32_t uMax,
                       int16_t vMin, int16_t vMax, uint8_t axis, int8_t sign)
{
    struct DioramaShellFace *output;

    if (*count >= capacity)
        return false;
    output = &faces[(*count)++];
    output->plane = plane;
    output->uMin = uMin;
    output->uMax = uMax;
    output->vMin = vMin;
    output->vMax = vMax;
    output->sourceCellX = span->sourceCellX;
    output->sourceCellY = span->sourceCellY;
    output->material = span->material[face];
    output->flags = span->flags;
    output->axis = axis;
    output->sign = sign;
    return true;
}

static bool AppendSideGaps(const struct DioramaOccupancySpan *spans,
                           uint32_t neighborBegin, uint32_t neighborEnd,
                           const struct DioramaOccupancySpan *span,
                           uint8_t face, int32_t plane, int32_t u,
                           uint8_t axis, int8_t sign,
                           struct DioramaShellFace *faces, uint32_t capacity,
                           uint32_t *count)
{
    int16_t cursor = span->yMin;
    uint32_t i;

    for (i = neighborBegin; i < neighborEnd && cursor < span->yMax; i++)
    {
        int16_t coveredMin = spans[i].yMin > cursor ? spans[i].yMin : cursor;
        int16_t coveredMax = spans[i].yMax < span->yMax ? spans[i].yMax : span->yMax;

        if (coveredMax <= cursor || coveredMin >= span->yMax)
            continue;
        if (coveredMin > cursor
         && !AppendFace(faces, capacity, count, span, face, plane, u, u + 1,
                        cursor, coveredMin, axis, sign))
            return false;
        if (coveredMax > cursor)
            cursor = coveredMax;
    }
    return cursor >= span->yMax
        || AppendFace(faces, capacity, count, span, face, plane, u, u + 1,
                      cursor, span->yMax, axis, sign);
}

bool DioramaOccupancy_BuildShell(const struct DioramaOccupancySpan *spans,
                                 uint32_t spanCount,
                                 int32_t ownerMinX, int32_t ownerMinZ,
                                 int32_t ownerMaxX, int32_t ownerMaxZ,
                                 struct DioramaShellFace *faces,
                                 uint32_t faceCapacity, uint32_t *faceCount)
{
    static const int8_t offsets[4][2] = {{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    static const uint8_t faceIds[4] = {DIORAMA_OCCUPANCY_FACE_NORTH,
                                      DIORAMA_OCCUPANCY_FACE_EAST,
                                      DIORAMA_OCCUPANCY_FACE_SOUTH,
                                      DIORAMA_OCCUPANCY_FACE_WEST};
    uint32_t count = 0;
    uint32_t i;

    if (spans == NULL || faces == NULL || faceCount == NULL)
        return false;
    for (i = 0; i < spanCount; i++)
    {
        const struct DioramaOccupancySpan *span = &spans[i];
        uint32_t begin;
        uint32_t end;
        int side;

        if (span->x < ownerMinX || span->x >= ownerMaxX
         || span->z < ownerMinZ || span->z >= ownerMaxZ)
            continue;
        FindColumn(spans, spanCount, span->x, span->z, &begin, &end);
        if (!IsOccupied(spans, begin, end, span->yMax)
         && !AppendFace(faces, faceCapacity, &count, span, DIORAMA_OCCUPANCY_FACE_TOP,
                        span->yMax, span->x, span->x + 1, span->z, span->z + 1, 1, 1))
            return false;
        if (!IsOccupied(spans, begin, end, span->yMin - 1)
         && !AppendFace(faces, faceCapacity, &count, span, DIORAMA_OCCUPANCY_FACE_BOTTOM,
                        span->yMin, span->x, span->x + 1, span->z, span->z + 1, 1, -1))
            return false;
        for (side = 0; side < 4; side++)
        {
            int32_t neighborX = span->x + offsets[side][0];
            int32_t neighborZ = span->z + offsets[side][1];
            uint8_t axis = side == 1 || side == 3 ? 0 : 2;
            int8_t sign = side == 0 || side == 3 ? -1 : 1;
            int32_t plane = axis == 0 ? span->x + (sign > 0) : span->z + (sign > 0);
            int32_t u = axis == 0 ? span->z : span->x;

            FindColumn(spans, spanCount, neighborX, neighborZ, &begin, &end);
            if (!AppendSideGaps(spans, begin, end, span, faceIds[side], plane, u,
                                axis, sign, faces, faceCapacity, &count))
                return false;
        }
    }
    *faceCount = count;
    return true;
}

static int CompareFaces(const void *leftValue, const void *rightValue)
{
    const struct DioramaShellFace *left = leftValue;
    const struct DioramaShellFace *right = rightValue;

#define COMPARE_FIELD(field) do { if (left->field != right->field) return left->field < right->field ? -1 : 1; } while (0)
    COMPARE_FIELD(axis);
    COMPARE_FIELD(sign);
    COMPARE_FIELD(plane);
    COMPARE_FIELD(sourceCellX);
    COMPARE_FIELD(sourceCellY);
    COMPARE_FIELD(material);
    COMPARE_FIELD(flags);
    COMPARE_FIELD(vMin);
    COMPARE_FIELD(vMax);
    COMPARE_FIELD(uMin);
    COMPARE_FIELD(uMax);
#undef COMPARE_FIELD
    return 0;
}

uint32_t DioramaOccupancy_MergeFaces(struct DioramaShellFace *faces,
                                     uint32_t faceCount)
{
    uint32_t read;
    uint32_t write = 0;

    qsort(faces, faceCount, sizeof(*faces), CompareFaces);
    for (read = 0; read < faceCount; read++)
    {
        struct DioramaShellFace *previous = write == 0 ? NULL : &faces[write - 1];
        struct DioramaShellFace *current = &faces[read];

        if (previous != NULL
         && previous->axis == current->axis && previous->sign == current->sign
         && previous->plane == current->plane
         && previous->sourceCellX == current->sourceCellX
         && previous->sourceCellY == current->sourceCellY
         && previous->material == current->material && previous->flags == current->flags
         && previous->vMin == current->vMin && previous->vMax == current->vMax
         && previous->uMax == current->uMin)
            previous->uMax = current->uMax;
        else
            faces[write++] = *current;
    }
    faceCount = write;
    write = 0;
    for (read = 0; read < faceCount; read++)
    {
        struct DioramaShellFace *previous = write == 0 ? NULL : &faces[write - 1];
        struct DioramaShellFace *current = &faces[read];

        if (previous != NULL
         && previous->axis == current->axis && previous->sign == current->sign
         && previous->plane == current->plane
         && previous->sourceCellX == current->sourceCellX
         && previous->sourceCellY == current->sourceCellY
         && previous->material == current->material && previous->flags == current->flags
         && previous->uMin == current->uMin && previous->uMax == current->uMax
         && previous->vMax == current->vMin)
            previous->vMax = current->vMax;
        else
            faces[write++] = *current;
    }
    return write;
}

#endif
