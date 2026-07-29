#include <stdio.h>
#include <stdlib.h>

#include "constants/weather.h"
#include "diorama/weather.h"

#define CHECK(condition) Check((condition), #condition, __LINE__)

static void Check(bool condition, const char *expression, int line)
{
    if (!condition)
    {
        fprintf(stderr, "test_weather.c:%d: check failed: %s\n", line, expression);
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    CHECK(DioramaWeather_Classify(WEATHER_NONE) == DIORAMA_WEATHER_CLEAR);
    CHECK(DioramaWeather_Classify(WEATHER_SUNNY) == DIORAMA_WEATHER_CLEAR);
    CHECK(DioramaWeather_Classify(WEATHER_RAIN) == DIORAMA_WEATHER_RAIN);
    CHECK(DioramaWeather_Classify(WEATHER_FOG_HORIZONTAL) == DIORAMA_WEATHER_FOG);
    CHECK(DioramaWeather_Classify(WEATHER_VOLCANIC_ASH) == DIORAMA_WEATHER_ASH);
    CHECK(DioramaWeather_Classify(WEATHER_DOWNPOUR) == DIORAMA_WEATHER_UNSUPPORTED);
    CHECK(DioramaWeather_CanRender(WEATHER_RAIN, WEATHER_SUNNY));
    CHECK(DioramaWeather_CanRender(WEATHER_VOLCANIC_ASH, WEATHER_VOLCANIC_ASH));
    CHECK(!DioramaWeather_CanRender(WEATHER_RAIN, WEATHER_RAIN_THUNDERSTORM));
    CHECK(DioramaWeather_RainDropCount(0) == 0);
    CHECK(DioramaWeather_RainDropCount(10) == 10);
    CHECK(DioramaWeather_RainDropCount(30) == 24);
    CHECK(DioramaWeather_FogAlpha(0) == 0);
    CHECK(DioramaWeather_FogAlpha(8) == 64);
    CHECK(DioramaWeather_FogAlpha(12) == 96);
    puts("weather tests passed");
    return EXIT_SUCCESS;
}
