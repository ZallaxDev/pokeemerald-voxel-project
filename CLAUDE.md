# CLAUDE.md

Guidance for Claude Code (and humans) working in this repository.

## What this is

An **accessible fork** of `pokeemerald-multiplatform` — a native Windows/Linux/Android
port of the Pokémon Emerald decompilation (SDL2, no emulator). This fork adds a
**screen-reader (NVDA) accessibility layer** and **terrain-based footstep audio** so the
game is playable by a blind player entirely by ear.

- Upstream: https://github.com/gradenGnostic/pokeemerald-multiplatform
- This fork: https://github.com/KamiKitsune420/pokeemerald-multiplatform

## Maintainer & contributors

- **KamiKitsune420** — maintainer of this accessible fork.
- **Kalahami** — contributor.

## Building (Windows, msys2)

The Windows target is 32-bit (the game data holds 32-bit pointers). Build from Git Bash:

```sh
export PATH="/c/msys64/mingw32/bin:/c/msys64/usr/bin:$PATH"
make -f Makefile_pc -j4 PREFIX= CPP=cpp SDL_DIR=/mingw32 \
  TMP="C:/Users/<you>/AppData/Local/Temp" TEMP="C:/Users/<you>/AppData/Local/Temp"
```

Why the extra flags (msys2 dropped some 32-bit packages; gcc is modern):
- `PREFIX=` — only `i686-w64-mingw32-gcc` is prefixed here; `as`/`ld`/`objcopy` are plain names.
- `CPP=cpp` — there is no `i686-w64-mingw32-cpp`.
- `TMP`/`TEMP` **must** be passed as make variables (Windows path). msys2 `make` blanks
  the env `TMP`/`TEMP` in recipes, so `as` tries to write to `C:\WINDOWS` and fails.
- Do **not** set `TMPDIR` to a Windows path — it breaks the msys assembler.

Dependencies (once): `pacman -S mingw-w64-i686-{SDL2,SDL2_mixer,pkgconf,libpng,zlib}`.
SDL2_image (32-bit, dropped from msys2) comes from the official SDL_image mingw dev
release, copied into `/c/msys64/mingw32/`. ImageMagick is not required — the cosmetic
border BMPs can be generated with Python Pillow.

Makefile changes this fork relies on (already applied to `Makefile_pc`):
- SSE/MMX intrinsics guard defines (modern gcc defaults SSE2 on; SDL headers clash with `-fno-builtin`).
- Windows C-compile uses the `gcc` driver, not bare `cc1` (so target flags reach the compiler).
- Link uses a response file (`@objs.rsp`) — Windows' command line can't hold every object.
- `-lSDL2_mixer` added to the Windows link.

## Running

Run `pokeemerald.exe`. These files must sit **beside** it (they are gitignored, not committed):
- `SDL2.dll`, `SDL2_image.dll`, `SDL2_mixer.dll` and mixer codec DLLs
  (libFLAC, libogg, libvorbis, libvorbisfile, libopus, libopusfile, libmpg123, libwavpack, libxmp, libgcc_s_dw2, libwinpthread).
- `nvdaControllerClient32.dll` — the NVDA speech bridge.

Committed assets that must ship: `sounds/steps/<material>/<n>.ogg` (footsteps),
`sounds/interacts/*.ogg`. Save data lives in `pokeemerald.sav`.

## Accessibility architecture

Two layers. Keep speech backend pure Win32; keep game-side decoding/hooks separate.

- `src/platform/speech.c` — loads `nvdaControllerClient32.dll` at runtime and speaks
  through the user's NVDA. No-ops safely if NVDA/DLL absent. API: `Speech_Init`,
  `Speech_Say(const char*, int interrupt)`, `Speech_Silence`.
- `src/platform/sfx.c` — SDL2_mixer engine for footsteps, positional cues and the
  optional HQ cries, on its own audio device (the game's own music/SFX go through
  m4a → `Platform_QueueAudio`, a separate SDL audio device). `Sfx_Init`,
  `Sfx_PlayFootstep(material)`, `Sfx_PlayCry(species, pan, volumePercent)`,
  `Sfx_StopCry`. Channel 0 is reserved for cries. (Positional `Sfx_PlayPositional`
  exists but the overworld radar that used it is currently disabled.)
- `src/accessibility.c` / `include/accessibility.h` — game-side. `AX_DecodeString`
  converts the game's charmap encoding to readable text; `AX_SayGameString` speaks it.
  Shared buffer builders (`AX_Say`, `AX_AppendStr/GameStr/Uint/Int/Sep`) live here too,
  along with the map-change watcher (`AX_MapCheck`, `AX_SayCurrentMap`).

Speech hooks (search for `AX_` / `Speech_` in these files):
- Dialogue/messages: `src/menu.c` (`AddTextPrinterForMessage*`), `src/battle_message.c`.
- Menus: `src/menu.c` (generic sMenu capture + `RedrawMenuCursor`), `src/list_menu.c`,
  `src/start_menu.c`, `src/main_menu.c`.
- Bag: `src/item_menu.c` (name + quantity, clean TM/HM/berry names, toss/sell counts).
- Battle: `src/battle_controller_player.c` (action menu, move menu + PP), `src/party_menu.c` (level/HP).
- Options: `src/option_menu.c` (`AX_SpeakOption`, both the main page and the display sub-page).
- Naming screen: `src/naming_screen.c` (key under cursor, entered text, keyboard page).
- Shop: `src/shop.c` (price per item, quantity + running total).
- PC / storage: `src/pokemon_storage_system.c` (`AX_SpeakStorageCursor` — slot contents
  plus row/column, box names).
- Summary screen: `src/pokemon_summary_screen.c` (`AX_SpeakSummaryPage`, `AX_SpeakMoveDetails`).
- Pokedex: `src/pokedex.c` (`AX_DexListCheck` polls `selectedPokemon` from the two list
  tasks; `AX_SpeakDexInfoScreen` reads the entry page including height/weight/description).
- Region map / Fly: `src/region_map.c` (`AX_SpeakRegionMapSec` — section name plus
  whether it's a Fly destination).
- Trainer card: `src/trainer_card.c` (`AX_SpeakTrainerCard`, read on open).
- Mail: `src/mail.c` (whole letter plus signature, in `PrintMailText`).
- Pokenav: `src/pokenav_menu_handler.c` (menu entries) and `src/pokenav_list.c`
  (Match Call / Ribbons / Condition rows — the four cursor movers are thin wrappers
  around `*_Internal` so the announcement sits in one place per direction).

Footsteps: `src/event_object_movement.c` (`AX_PlayerFootstep` maps metatile behavior +
map type to a material). Init in `src/main.c` (`Speech_Init` + `Sfx_Init`).

Map announcements: `AX_MapCheck()` runs once per overworld frame (called from
`AX_OverworldScan` in `OverworldBasic`) and speaks the map name when the player's
map group/number changes. Names come from `src/data/ax_map_names.h`, generated by
`python tools/gen_ax_map_names.py` from `data/maps/map_groups.json` — re-run it
after adding or renaming maps. Interiors get real names ("Littleroot Town,
Brendan's House, 1F"); maps missing from the table fall back to the region-map
section name.

High-quality cries: `PlayCryInternal` (`src/sound.c`) first tries
`Sfx_PlayCry(species, ...)`, which looks for `sounds/cries/<species>.ogg` or `.mp3`
(`<species>` = the internal `SPECIES_*` number). If one is found, the GBA cry is
still played at volume 0 so cry priority, `IsCryPlaying()` timing and BGM ducking
behave unchanged. Fetch the files with `python tools/fetch_hq_cries.py`; they are
gitignored, never committed.

## Conventions

- Guard every hook so it's a no-op when speech/audio is unavailable — never crash the game.
- New `src/*.c` and `src/*/*.c` are auto-globbed by `Makefile_pc`; no Makefile edit needed.
- Do not commit build outputs (`*.exe`, `*.dll`, `build/`) or the generated border BMPs.
- Prefer decoding real game data over scraping rendered text (e.g. build "TM 46, Thief"
  from item data rather than the on-screen label).

## Not yet accessible (roadmap)

Pokédex, region map / fly menu, trainer card, Pokénav, mail, decoration and secret
base menus, contests, the slot machine and other Game Corner minigames, berry
blender, and the Battle Frontier front-ends.

Also open: real spatial audio. SDL2_mixer only does stereo amplitude panning
(`Mix_SetPanning`), so it cannot convey "in front of / behind you". An HRTF-capable
backend (OpenAL Soft is the obvious candidate — LGPL, built-in HRTF, works on
Windows/Linux/Android) would replace `src/platform/sfx.c` without touching any
game-side hook, since everything goes through the `Sfx_*` API.

## Screen-reader hotkeys

Keys that aren't mapped to GBA buttons are latched in `src/platform/sdl2.c` and
drained via `Platform_GetReaderKeys()` (edge-triggered; reading clears). Bits are
`READER_KEY_*` in `include/platform.h`.

Currently consumed by the party menu (`AX_HandlePartyReaderKeys` in
`src/party_menu.c`), which speaks only the nickname on cursor moves and leaves
the detail to these:

- `L` level, `Shift+L` experience to next level
- `H` hit points, `U` held item, `T` status, `Y` everything

The latch is global, so a screen that starts consuming these should drain it on
open (as `InitPartyMenu` does) or it will replay presses made elsewhere.

## Text decoding notes

`AX_DecodeString` is where most "it reads wrong" bugs live:

- Horizontal positioning control codes (`CLEAR`, `SKIP`, `CLEAR_TO`,
  `SHIFT_RIGHT`) become ", ". `GetStringClearToWidth` uses them to lay out list
  columns, so dropping them ran fields together ("RAD NEIGHBORMAY").
- `CHAR_PROMPT_SCROLL` / `CHAR_PROMPT_CLEAR` **terminate** the decode. They mean
  "wait for A", so speaking past one reads a whole conversation at once and puts
  speech ahead of the screen. `src/text.c` speaks each following page when the
  printer resumes (`RENDER_STATE_CLEAR` / `RENDER_STATE_SCROLL_START`).
- `CHAR_e_ACUTE` matches the case of the preceding letter, so POKéMON reads as
  POKEMON rather than "POKeMON".
- Money is spoken with `AX_MONEY_UNIT`; never read a bare price number.
