#ifdef ENABLE_DIORAMA

#include "constants/weather.h"
#include "diorama/weather.h"

enum DioramaWeatherEffect DioramaWeather_Classify(uint8_t weather)
{
    switch (weather)
    {
    case WEATHER_NONE:
    case WEATHER_SUNNY:
        return DIORAMA_WEATHER_CLEAR;
    case WEATHER_RAIN:
        return DIORAMA_WEATHER_RAIN;
    case WEATHER_FOG_HORIZONTAL:
        return DIORAMA_WEATHER_FOG;
    case WEATHER_VOLCANIC_ASH:
        return DIORAMA_WEATHER_ASH;
    default:
        return DIORAMA_WEATHER_UNSUPPORTED;
    }
}

bool DioramaWeather_CanRender(uint8_t currentWeather, uint8_t nextWeather)
{
    return DioramaWeather_Classify(currentWeather) != DIORAMA_WEATHER_UNSUPPORTED
        && DioramaWeather_Classify(nextWeather) != DIORAMA_WEATHER_UNSUPPORTED;
}

uint8_t DioramaWeather_RainDropCount(uint8_t visibleCount)
{
    return visibleCount > 24 ? 24 : visibleCount;
}

uint8_t DioramaWeather_FogAlpha(uint16_t blendEVA)
{
    return blendEVA >= 12 ? 96 : blendEVA * 8;
}

#endif
