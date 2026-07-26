#ifndef GUARD_ACCESSIBILITY_H
#define GUARD_ACCESSIBILITY_H

// --- Win32 speech backend (src/platform/speech.c) ---
// Speaks through the user's running NVDA; no-ops safely if NVDA is absent.
void Speech_Init(void);
void Speech_Say(const char *text, int interrupt);
void Speech_Silence(void);

// --- Game-side helpers (src/accessibility.c) ---
// Decode a game-encoded (charmap) string into an ASCII buffer for the reader.
// Returns the number of characters written (excluding the NUL terminator).
int AX_DecodeString(const u8 *src, char *out, int outSize);
// Decode gameStr and speak it. interrupt != 0 cuts off current speech.
void AX_SayGameString(const u8 *gameStr, int interrupt);
// Speak a plain C string (thin wrapper so game files needn't include speech.h).
void AX_Say(const char *text, int interrupt);

// Buffer builders. Each appends at offset o and returns the new offset; all of
// them respect `size` (including room for the NUL) and never overflow.
int AX_AppendUint(char *buf, int o, int size, u32 val);
int AX_AppendInt(char *buf, int o, int size, s32 val);
int AX_AppendStr(char *buf, int o, int size, const char *s);
int AX_AppendGameStr(char *buf, int o, int size, const u8 *s);
// Append ", " if the buffer already has content — for building list phrases.
int AX_AppendSep(char *buf, int o, int size);

// Spoken name of the game's currency. The UI only ever draws the Poke-dollar
// glyph, so a bare number tells the player nothing about what it counts.
#define AX_MONEY_UNIT " Pokedollars"

// --- Map announcements (src/accessibility.c) ---
// Spoken name for a map, e.g. "Littleroot Town, Brendan's House, 1F".
// Returns NULL when the map isn't in the generated table (dynamic maps).
const char *AX_MapName(u8 group, u8 num);
// Speak the current map's name immediately.
void AX_SayCurrentMap(void);
// Called once per overworld frame: speaks the map name when the player has
// moved to a different map. No-op while the map is unchanged.
void AX_MapCheck(void);

// --- Footstep sound effects (src/platform/sfx.c) ---
// Material indices; MUST match sMaterialDirs[] in src/platform/sfx.c.
enum {
    AX_MAT_GRASS, AX_MAT_SHORT_GRASS, AX_MAT_SAND, AX_MAT_SHALLOW_WATER,
    AX_MAT_DEEP_WATER, AX_MAT_ASH, AX_MAT_SNOW, AX_MAT_WOOD, AX_MAT_CONCRETE,
    AX_MAT_DIRT, AX_MAT_GRAVEL, AX_MAT_CARPET, AX_MAT_MARBLE, AX_MAT_METAL,
};
void Sfx_Init(void);
void Sfx_PlayFootstep(int material);

// Optional high-quality Pokemon cries, read from sounds/cries/<species>.ogg
// (or .mp3). Returns 1 when an HQ cry was started and the caller should mute
// the built-in GBA cry, 0 when no replacement is installed for that species.
// `pan` is the m4a panpot (-64 left .. 63 right); `volumePercent` is 0..100.
int Sfx_PlayCry(int species, int pan, int volumePercent);
void Sfx_StopCry(void);

// These samples don't pass through Platform_QueueAudio, so the platform layer
// has to push the volume settings down to them. Both take 0..100.
// Master mirrors the Volume option; cry gain is the separate "Cry volume" trim.
void Sfx_SetMasterVolume(int percent);
void Sfx_SetCryGain(int percent);

// Spatial overworld cues.
enum { AX_SND_DOOR, AX_SND_PERSON };
void Sfx_PlayPositional(int which, int dx, int dy);
// Periodic scan of nearby NPCs + doors; call once per overworld frame.
void AX_OverworldScan(void);

#endif // GUARD_ACCESSIBILITY_H
