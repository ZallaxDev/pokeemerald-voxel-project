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

void Sfx_Init(void)
{
    int m, v;

    if (sReady)
        return;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
        return;
    Mix_Init(MIX_INIT_OGG);
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) < 0)
        return;
    Mix_AllocateChannels(16);

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
    Mix_Volume(ch, vol);
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
        Mix_Volume(ch, MIX_MAX_VOLUME / 2);
}
