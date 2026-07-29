#ifdef ENABLE_DIORAMA

#include <stddef.h>

#include "constants/global.h"
#include "diorama/map_connection.h"

bool DioramaConnection_MapCoordinates(uint8_t direction, int32_t offset,
                                      int16_t activeMapX, int16_t activeMapY,
                                      int16_t activeWidth, int16_t activeHeight,
                                      int16_t connectedWidth, int16_t connectedHeight,
                                      int16_t *connectedX, int16_t *connectedY)
{
    int32_t x;
    int32_t y;

    if (connectedX == NULL || connectedY == NULL)
        return false;
    switch (direction)
    {
    case CONNECTION_NORTH:
        x = activeMapX - offset;
        y = connectedHeight + activeMapY;
        break;
    case CONNECTION_SOUTH:
        x = activeMapX - offset;
        y = activeMapY - activeHeight;
        break;
    case CONNECTION_WEST:
        x = connectedWidth + activeMapX;
        y = activeMapY - offset;
        break;
    case CONNECTION_EAST:
        x = activeMapX - activeWidth;
        y = activeMapY - offset;
        break;
    default:
        return false;
    }
    if (x < 0 || y < 0 || x >= connectedWidth || y >= connectedHeight)
        return false;
    *connectedX = x;
    *connectedY = y;
    return true;
}

#endif
