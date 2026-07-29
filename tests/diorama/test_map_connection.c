#include <assert.h>
#include <stdio.h>

#include "constants/global.h"
#include "diorama/map_connection.h"

static void CheckMapping(uint8_t direction, int32_t offset,
                         int16_t x, int16_t y, int16_t expectedX, int16_t expectedY)
{
    int16_t connectedX = -1;
    int16_t connectedY = -1;

    assert(DioramaConnection_MapCoordinates(direction, offset, x, y,
                                             20, 20, 20, 20,
                                             &connectedX, &connectedY));
    assert(connectedX == expectedX);
    assert(connectedY == expectedY);
}

int main(void)
{
    int16_t x;
    int16_t y;

    CheckMapping(CONNECTION_NORTH, 0, 4, -16, 4, 4);
    CheckMapping(CONNECTION_NORTH, 2, 6, -1, 4, 19);
    CheckMapping(CONNECTION_SOUTH, -2, 2, 20, 4, 0);
    CheckMapping(CONNECTION_SOUTH, 0, 4, 35, 4, 15);
    CheckMapping(CONNECTION_WEST, 3, -1, 7, 19, 4);
    CheckMapping(CONNECTION_EAST, -3, 20, 7, 0, 10);

    assert(!DioramaConnection_MapCoordinates(CONNECTION_NORTH, 0, -1, -1,
                                              20, 20, 20, 20, &x, &y));
    assert(!DioramaConnection_MapCoordinates(CONNECTION_SOUTH, 0, 20, 20,
                                              20, 20, 20, 20, &x, &y));
    assert(!DioramaConnection_MapCoordinates(CONNECTION_DIVE, 0, 0, 0,
                                              20, 20, 20, 20, &x, &y));
    assert(!DioramaConnection_MapCoordinates(CONNECTION_NORTH, 0, 0, -1,
                                              20, 20, 20, 20, NULL, &y));
    puts("diorama map connection tests passed");
    return 0;
}
