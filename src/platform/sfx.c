// Footstep / UI sound-effect layer for the accessible port.
//
// Uses SDL2_mixer on its own audio device (independent of the m4a music path)
// to play short OGG samples with overlap. Footstep clips live on disk under
// sounds/steps/<material>/<n>.ogg and are loaded once at startup. If anything
// fails to load, calls become harmless no-ops.
//
// Pure platform code (no game headers): the game side calls Sfx_PlayFootstep()
// with a material index that must match sMaterialDirs below.

#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>

#define MAX_VARIANTS 32

// Material index -> folder name. MUST stay in sync with the enum the game hook
// uses (include/accessibility.h AX_MAT_*).
static const char *const sMaterialDirs[] = {
    "grass",         // 0
    "short_grass",   // 1
    "sand",          // 2
    "shallow_water", // 3
    "deep_water",    // 4
    "ash",           // 5
    "snow1",         // 6
    "wood1",         // 7
    "concrete",      // 8
    "dirt",          // 9
    "gravel",        // 10
    "carpet",        // 11
    "marble",        // 12
    "metal",         // 13
};
#define NUM_MATERIALS ((int)(sizeof(sMaterialDirs) / sizeof(sMaterialDirs[0])))

static Mix_Chunk *sChunks[NUM_MATERIALS][MAX_VARIANTS];
static int sVariantCount[NUM_MATERIALS];
static Mix_Chunk *sInteract[2]; // 0 = door, 1 = person (spatial cues)
static int sReady;
static unsigned int sRng = 0x1234567u;

// --- High-quality Pokemon cries --------------------------------------------
// Optional replacements for the GBA's 8-bit cry samples, read from
// sounds/cries/<species>.(ogg|mp3) where <species> is the game's internal
// species number. Absent files simply fall back to the built-in cries.
#define CRY_MAX_SPECIES 1024
#define CRY_CACHE_SIZE  96
#define CRY_CHANNEL     0   // reserved, so footsteps never cut a cry short

static Mix_Chunk *sCryCache[CRY_CACHE_SIZE];
static int sCryCacheSpecies[CRY_CACHE_SIZE];
static int sCryCacheNext;
static unsigned char sCryMissing[CRY_MAX_SPECIES]; // don't re-probe absent files

// --- Volume ----------------------------------------------------------------
// The game's own audio is mixed by m4a and scaled in Platform_QueueAudio by the
// Volume option. Nothing here goes through that path, so these samples have to
// apply the same master volume themselves or they drown the game out.
//
// sCryGain trims the HQ cries on top of that: they're mastered near full scale,
// while the GBA cries come out of a mixer that leaves a lot of headroom.
static int sMasterVolume = 100; // 0..100, mirrors the Volume option
static int sCryGain = 50;       // 0..100, "Cry volume" option

static int ClampPercent(int v)
{
    if (v < 0)
        return 0;
    if (v > 100)
        return 100;
    return v;
}

void Sfx_SetMasterVolume(int percent)
{
    sMasterVolume = ClampPercent(percent);
}

void Sfx_SetCryGain(int percent)
{
    sCryGain = ClampPercent(percent);
}

void Sfx_Init(void)
{
    int m, v;

    if (sReady)
        return;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        return;
    Mix_Init(MIX_INIT_OGG | MIX_INIT_MP3);
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0)
        return;
    Mix_AllocateChannels(16);
    Mix_ReserveChannels(1); // channel 0 belongs to cries

    for (m = 0; m < NUM_MATERIALS; m++)
    {
        for (v = 1; v <= MAX_VARIANTS; v++)
        {
            char path[256];
            Mix_Chunk *c;
            snprintf(path, sizeof(path), "sounds/steps/%s/%d.ogg", sMaterialDirs[m], v);
            c = Mix_LoadWAV(path);
            if (c == NULL)
                break; // variants are numbered contiguously from 1
            sChunks[m][sVariantCount[m]++] = c;
        }
    }

    sInteract[0] = Mix_LoadWAV("sounds/interacts/door.ogg");
    sInteract[1] = Mix_LoadWAV("sounds/interacts/person.ogg");

    sReady = 1;
}

// Play a spatial cue (0 = door, 1 = person) positioned by tile offset from the
// player: dx east(+)/west(-), dy south(+)/north(-). Panned left/right by dx and
// attenuated by distance. Used for the overworld door/NPC "radar".
void Sfx_PlayPositional(int which, int dx, int dy)
{
    Mix_Chunk *c;
    int ch, p, adx, ady, dist, left, right, vol;

    if (!sReady || which < 0 || which > 1)
        return;
    c = sInteract[which];
    if (c == NULL)
        return;

    ch = Mix_PlayChannel(-1, c, 0);
    if (ch < 0)
        return;

    // Pan by horizontal offset, clamped to +/-8 tiles.
    p = dx;
    if (p > 8) p = 8;
    if (p < -8) p = -8;
    left = 255;
    right = 255;
    if (p > 0)         // object to the east: quieter on the left
        left = 255 - (p * 255 / 8);
    else if (p < 0)    // object to the west: quieter on the right
        right = 255 - ((-p) * 255 / 8);
    Mix_SetPanning(ch, (Uint8)left, (Uint8)right);

    // Attenuate by Manhattan distance; keep these cues gentle.
    adx = (dx < 0) ? -dx : dx;
    ady = (dy < 0) ? -dy : dy;
    dist = adx + ady;
    vol = (MIX_MAX_VOLUME * 3 / 4) - (dist * (MIX_MAX_VOLUME * 3 / 4) / 14);
    if (vol < 8)
        vol = 8;
    Mix_Volume(ch, vol * sMasterVolume / 100);
}

// Look up (loading on first use) the HQ cry sample for a species.
// Returns NULL when the player hasn't installed one.
static Mix_Chunk *LoadCry(int species)
{
    static const char *const exts[] = { "ogg", "mp3" };
    char path[64];
    Mix_Chunk *c = NULL;
    int i;

    for (i = 0; i < CRY_CACHE_SIZE; i++)
    {
        if (sCryCacheSpecies[i] == species)
            return sCryCache[i];
    }

    for (i = 0; i < (int)(sizeof(exts) / sizeof(exts[0])) && c == NULL; i++)
    {
        snprintf(path, sizeof(path), "sounds/cries/%d.%s", species, exts[i]);
        c = Mix_LoadWAV(path);
    }
    if (c == NULL)
    {
        sCryMissing[species] = 1;
        return NULL;
    }

    // Round-robin eviction. Stop the cry channel first: freeing a chunk that is
    // still being mixed would read freed memory.
    if (sCryCache[sCryCacheNext] != NULL)
    {
        Mix_HaltChannel(CRY_CHANNEL);
        Mix_FreeChunk(sCryCache[sCryCacheNext]);
    }
    sCryCache[sCryCacheNext] = c;
    sCryCacheSpecies[sCryCacheNext] = species;
    sCryCacheNext = (sCryCacheNext + 1) % CRY_CACHE_SIZE;
    return c;
}

// Play the HQ cry for `species` (the game's internal species number), panned by
// `pan` (m4a panpot, -64 = full left .. 63 = full right) at `volume` percent.
// Returns 1 if an HQ cry was started, 0 if the caller should use the GBA cry.
int Sfx_PlayCry(int species, int pan, int volumePercent)
{
    Mix_Chunk *c;
    int ch, left, right, vol;

    if (!sReady || species <= 0 || species >= CRY_MAX_SPECIES)
        return 0;
    if (sCryMissing[species])
        return 0;

    c = LoadCry(species);
    if (c == NULL)
        return 0;

    Mix_HaltChannel(CRY_CHANNEL); // a new cry always replaces the old one
    ch = Mix_PlayChannel(CRY_CHANNEL, c, 0);
    if (ch < 0)
        return 0;

    // m4a panpot is roughly -64..63; fold that into stereo attenuation.
    if (pan > 63) pan = 63;
    if (pan < -64) pan = -64;
    left = 255;
    right = 255;
    if (pan > 0)
        left = 255 - (pan * 255 / 64);
    else if (pan < 0)
        right = 255 - ((-pan) * 255 / 64);
    Mix_SetPanning(ch, (Uint8)left, (Uint8)right);

    // Per-cry volume (from the cry mode), then the user's cry trim, then the
    // game-wide Volume option — so cries track the rest of the audio.
    vol = MIX_MAX_VOLUME * ClampPercent(volumePercent) / 100;
    vol = vol * sCryGain / 100;
    vol = vol * sMasterVolume / 100;
    Mix_Volume(ch, vol);
    return 1;
}

void Sfx_StopCry(void)
{
    if (sReady)
        Mix_HaltChannel(CRY_CHANNEL);
}

// Play a random footstep variant for the given material at a modest volume.
void Sfx_PlayFootstep(int material)
{
    int idx, ch;

    if (!sReady || material < 0 || material >= NUM_MATERIALS)
        return;
    if (sVariantCount[material] == 0)
        return;

    sRng = sRng * 1103515245u + 12345u;
    idx = (int)((sRng >> 16) % (unsigned int)sVariantCount[material]);

    ch = Mix_PlayChannel(-1, sChunks[material][idx], 0);
    if (ch >= 0)
        Mix_Volume(ch, (MIX_MAX_VOLUME / 2) * sMasterVolume / 100);
}
