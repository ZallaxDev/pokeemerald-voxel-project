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
// Append an unsigned decimal number to buf at offset o; returns the new offset.
int AX_AppendUint(char *buf, int o, int size, u32 val);

// --- Footstep sound effects (src/platform/sfx.c) ---
// Material indices; MUST match sMaterialDirs[] in src/platform/sfx.c.
enum {
    AX_MAT_GRASS, AX_MAT_SHORT_GRASS, AX_MAT_SAND, AX_MAT_SHALLOW_WATER,
    AX_MAT_DEEP_WATER, AX_MAT_ASH, AX_MAT_SNOW, AX_MAT_WOOD, AX_MAT_CONCRETE,
    AX_MAT_DIRT, AX_MAT_GRAVEL, AX_MAT_CARPET, AX_MAT_MARBLE, AX_MAT_METAL,
};
void Sfx_Init(void);
void Sfx_PlayFootstep(int material);

// Spatial overworld cues.
enum { AX_SND_DOOR, AX_SND_PERSON };
void Sfx_PlayPositional(int which, int dx, int dy);
// Periodic scan of nearby NPCs + doors; call once per overworld frame.
void AX_OverworldScan(void);

#endif // GUARD_ACCESSIBILITY_H
