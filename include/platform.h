#ifndef GUARD_PLATFORM_H
#define GUARD_PLATFORM_H

#include "global.h"
#include "siirtc.h"

void Platform_StoreSaveFile(void);
void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size);
void Platform_QueueAudio(float *audioBuffer, s32 samplesPerFrame);
u16 Platform_GetKeyInput(void);
u8 Platform_GetBorderBackgroundCount(void);
u8 Platform_GetBorderBackground(void);
void Platform_SetBorderBackground(u8 selection);

enum PlatformSetting
{
    PLATFORM_SETTING_FULLSCREEN,
    PLATFORM_SETTING_WINDOW_SCALE,
    PLATFORM_SETTING_INTEGER_SCALE,
    PLATFORM_SETTING_VSYNC,
    PLATFORM_SETTING_BORDER,
    PLATFORM_SETTING_VOLUME,
    PLATFORM_SETTING_CRY_VOLUME, // 0..10 -> 0..100% trim on the HQ Pokemon cries
    PLATFORM_SETTING_COUNT,
};

// --- Screen-reader hotkeys --------------------------------------------------
// Extra keys that aren't mapped to GBA buttons. Screens can query these to read
// out a specific field on demand instead of announcing everything at once.
// Edge-triggered: each press is reported once, and reading clears it.
#define READER_KEY_LEVEL      (1 << 0) // L
#define READER_KEY_EXP        (1 << 1) // Shift+L
#define READER_KEY_HP         (1 << 2) // H
#define READER_KEY_ITEM       (1 << 3) // U
#define READER_KEY_STATUS     (1 << 4) // T
#define READER_KEY_ALL        (1 << 5) // Y - read the whole entry

u16 Platform_GetReaderKeys(void);

u8 Platform_GetSetting(enum PlatformSetting setting);
void Platform_SetSetting(enum PlatformSetting setting, u8 value);
void Platform_GetStatus(struct SiiRtcInfo *rtc);
void Platform_SetStatus(struct SiiRtcInfo *rtc);
static void UpdateInternalClock(void);
void Platform_GetDateTime(struct SiiRtcInfo *rtc);
void Platform_SetDateTime(struct SiiRtcInfo *rtc);
void Platform_GetTime(struct SiiRtcInfo *rtc);
void Platform_SetTime(struct SiiRtcInfo *rtc);
void Platform_SetAlarm(u8 *alarmData);

#endif
