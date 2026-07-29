#ifndef GUARD_DIORAMA_WEATHER_H
#define GUARD_DIORAMA_WEATHER_H

#include <stdbool.h>
#include <stdint.h>

enum DioramaWeatherEffect
{
    DIORAMA_WEATHER_CLEAR,
    DIORAMA_WEATHER_RAIN,
    DIORAMA_WEATHER_FOG,
    DIORAMA_WEATHER_ASH,
    DIORAMA_WEATHER_UNSUPPORTED
};

enum DioramaWeatherEffect DioramaWeather_Classify(uint8_t weather);
bool DioramaWeather_CanRender(uint8_t currentWeather, uint8_t nextWeather);
uint8_t DioramaWeather_RainDropCount(uint8_t visibleCount);
uint8_t DioramaWeather_FogAlpha(uint16_t blendEVA);

#endif
