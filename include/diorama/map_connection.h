#ifndef GUARD_DIORAMA_MAP_CONNECTION_H
#define GUARD_DIORAMA_MAP_CONNECTION_H

#include <stdbool.h>
#include <stdint.h>

bool DioramaConnection_MapCoordinates(uint8_t direction, int32_t offset,
                                      int16_t activeMapX, int16_t activeMapY,
                                      int16_t activeWidth, int16_t activeHeight,
                                      int16_t connectedWidth, int16_t connectedHeight,
                                      int16_t *connectedX, int16_t *connectedY);

#endif
