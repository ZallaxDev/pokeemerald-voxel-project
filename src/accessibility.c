// Game-side accessibility helpers: turn the game's charmap-encoded strings into
// plain text and hand them to the screen-reader backend (src/platform/speech.c).

#include "global.h"
#include "string_util.h"
#include "constants/characters.h"
#include "event_object_movement.h"
#include "fieldmap.h"
#include "metatile_behavior.h"
#include "accessibility.h"

// Decode one game-encoded string (terminated by EOS) into ASCII/UTF-8 `out`.
int AX_DecodeString(const u8 *src, char *out, int outSize)
{
    int o = 0;

    if (src == NULL || out == NULL || outSize < 1)
        return 0;

    while (*src != EOS && o < outSize - 1)
    {
        u8 c = *src;

        // Extended control code: skip the 0xFC marker, its sub-code and args.
        if (c == EXT_CTRL_CODE_BEGIN)
        {
            src++;
            src += GetExtCtrlCodeLength(*src);
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
            case CHAR_PROMPT_SCROLL:
            case CHAR_PROMPT_CLEAR:
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
            case CHAR_E_ACUTE:       ch = 'E';  break;      // É  (POKéMON)
            case CHAR_e_ACUTE:       ch = 'e';  break;      // é
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

// Overworld spatial radar (person + door cues) removed for now — both were
// unreliable. Kept as a no-op so the call site in OverworldBasic stays valid.
void AX_OverworldScan(void)
{
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
