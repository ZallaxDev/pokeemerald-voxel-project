// Game-side accessibility helpers: turn the game's charmap-encoded strings into
// plain text and hand them to the screen-reader backend (src/platform/speech.c).

#include "global.h"
#include "string_util.h"
#include "constants/characters.h"
#include "event_object_movement.h"
#include "fieldmap.h"
#include "metatile_behavior.h"
#include "overworld.h"
#include "region_map.h"
#include "accessibility.h"
#include "data/ax_map_names.h"

// Decode one game-encoded string (terminated by EOS) into ASCII/UTF-8 `out`.
int AX_DecodeString(const u8 *src, char *out, int outSize)
{
    int o = 0;

    if (src == NULL || out == NULL || outSize < 1)
        return 0;

    while (*src != EOS && o < outSize - 1)
    {
        u8 c = *src;

        // Stop at a page break. These mark "wait for the player to press A",
        // so speaking past one reads the whole conversation in a single burst
        // and leaves the speech far ahead of what's on screen. text.c speaks
        // each following page as the printer reaches it.
        if (c == CHAR_PROMPT_SCROLL || c == CHAR_PROMPT_CLEAR)
            break;

        // Extended control code: skip the 0xFC marker, its sub-code and args.
        if (c == EXT_CTRL_CODE_BEGIN)
        {
            u8 sub;

            src++;
            sub = *src;
            src += GetExtCtrlCodeLength(sub);

            // Horizontal positioning codes are how the game lays list rows out
            // in columns (GetStringClearToWidth puts one between a trainer's
            // class and name). They carry no glyph, so dropping them silently
            // runs the columns together: "RAD NEIGHBORMAY". Emit a separator.
            switch (sub)
            {
            case EXT_CTRL_CODE_CLEAR:
            case EXT_CTRL_CODE_SKIP:
            case EXT_CTRL_CODE_CLEAR_TO:
            case EXT_CTRL_CODE_SHIFT_RIGHT:
                if (o > 0 && out[o - 1] != ' ' && out[o - 1] != ',' && o < outSize - 2)
                {
                    out[o++] = ',';
                    out[o++] = ' ';
                }
                break;
            }
            continue;
        }
        // Unexpanded placeholder marker (rare here): skip the marker + id byte.
        if (c == PLACEHOLDER_BEGIN)
        {
            src += 2;
            continue;
        }

        if (c >= CHAR_A && c <= CHAR_A + 25)        // A-Z (0xBB..0xD4)
        {
            out[o++] = 'A' + (c - CHAR_A);
        }
        else if (c >= 0xD5 && c <= 0xEE)            // a-z
        {
            out[o++] = 'a' + (c - 0xD5);
        }
        else if (c >= CHAR_0 && c <= CHAR_0 + 9)    // 0-9 (0xA1..0xAA)
        {
            out[o++] = '0' + (c - CHAR_0);
        }
        else
        {
            const char *rep = NULL; // multi-character replacement
            char ch = 0;            // single-character replacement

            switch (c)
            {
            case CHAR_SPACE:
            case CHAR_SPACER:
            case CHAR_NEWLINE:
                ch = ' ';
                break;
            case CHAR_EXCL_MARK:     ch = '!';  break;
            case CHAR_QUESTION_MARK: ch = '?';  break;
            case CHAR_PERIOD:        ch = '.';  break;
            case CHAR_HYPHEN:        ch = '-';  break;
            case CHAR_COMMA:         ch = ',';  break;
            case CHAR_SLASH:         ch = '/';  break;
            case CHAR_COLON:         ch = ':';  break;
            case 0x5C:               ch = '(';  break;
            case 0x5D:               ch = ')';  break;
            case 0xB1: case 0xB2:    ch = '"';  break; // curly double quotes
            case 0xB3: case 0xB4:    ch = '\''; break; // curly single quote / apostrophe
            case CHAR_ELLIPSIS:      rep = "..."; break;    // 0xB0
            case CHAR_E_ACUTE:       ch = 'E';  break;      // É
            // The game spells POKéMON / POKéNAV / POKéDEX in caps with a
            // lowercase é. Emitting plain 'e' gives "POKeMON", which readers
            // stumble over, so match the case of what came before.
            case CHAR_e_ACUTE:
                ch = (o > 0 && out[o - 1] >= 'A' && out[o - 1] <= 'Z') ? 'E' : 'e';
                break;
            case 0xB5:               rep = " male";   break; // ♂
            case 0xB6:               rep = " female"; break; // ♀
            default:                 ch = 0;    break;      // drop unknown glyphs
            }

            if (rep != NULL)
            {
                while (*rep != '\0' && o < outSize - 1)
                    out[o++] = *rep++;
            }
            else if (ch != 0)
            {
                out[o++] = ch;
            }
        }
        src++;
    }

    out[o] = '\0';
    return o;
}

void AX_SayGameString(const u8 *gameStr, int interrupt)
{
    char buf[512];

    if (gameStr == NULL)
        return;
    if (AX_DecodeString(gameStr, buf, sizeof(buf)) > 0)
        Speech_Say(buf, interrupt);
}

void AX_Say(const char *text, int interrupt)
{
    Speech_Say(text, interrupt);
}

// Overworld spatial radar (person + door cues) removed for now — both were
// unreliable. The per-frame call site in OverworldBasic is still useful, so it
// now drives the map-change announcement instead.
void AX_OverworldScan(void)
{
    AX_MapCheck();
}

int AX_AppendUint(char *buf, int o, int size, u32 val)
{
    char tmp[12];
    int t = 0;

    if (val == 0)
    {
        if (o < size - 1)
            buf[o++] = '0';
        return o;
    }
    while (val > 0 && t < (int)sizeof(tmp))
    {
        tmp[t++] = '0' + (val % 10);
        val /= 10;
    }
    while (t > 0 && o < size - 1)
        buf[o++] = tmp[--t];
    return o;
}

int AX_AppendInt(char *buf, int o, int size, s32 val)
{
    if (val < 0)
    {
        if (o < size - 1)
            buf[o++] = '-';
        return AX_AppendUint(buf, o, size, (u32)-val);
    }
    return AX_AppendUint(buf, o, size, (u32)val);
}

int AX_AppendStr(char *buf, int o, int size, const char *s)
{
    if (s == NULL)
        return o;
    while (*s != '\0' && o < size - 1)
        buf[o++] = *s++;
    buf[o] = '\0';
    return o;
}

int AX_AppendGameStr(char *buf, int o, int size, const u8 *s)
{
    if (s == NULL || o >= size - 1)
        return o;
    return o + AX_DecodeString(s, buf + o, size - o);
}

int AX_AppendSep(char *buf, int o, int size)
{
    if (o == 0)
        return o;
    return AX_AppendStr(buf, o, size, ", ");
}

// ---------------------------------------------------------------------------
// Map announcements
// ---------------------------------------------------------------------------

const char *AX_MapName(u8 group, u8 num)
{
    if (group >= AX_MAP_GROUP_COUNT)
        return NULL;
    if (num >= sAxMapNameCounts[group])
        return NULL;
    return sAxMapNames[group][num];
}

void AX_SayCurrentMap(void)
{
    u8 group = gSaveBlock1Ptr->location.mapGroup;
    u8 num = gSaveBlock1Ptr->location.mapNum;
    const char *name = AX_MapName(group, num);

    if (name != NULL)
    {
        Speech_Say(name, 1);
        return;
    }

    // Dynamic maps (secret bases, link rooms, Battle Frontier sub-maps) aren't
    // in the generated table — fall back to the region-map section name.
    {
        u8 gameName[32];
        GetMapName(gameName, gMapHeader.regionMapSectionId, 0);
        AX_SayGameString(gameName, 1);
    }
}

// Track the last map we announced so the per-frame check stays silent until the
// player actually changes map. -1 marks "nothing announced yet".
static s16 sLastMapGroup = -1;
static s16 sLastMapNum = -1;

void AX_MapCheck(void)
{
    u8 group, num;

    if (gSaveBlock1Ptr == NULL)
        return;

    group = gSaveBlock1Ptr->location.mapGroup;
    num = gSaveBlock1Ptr->location.mapNum;

    if (group == sLastMapGroup && num == sLastMapNum)
        return;

    sLastMapGroup = group;
    sLastMapNum = num;
    AX_SayCurrentMap();
}
