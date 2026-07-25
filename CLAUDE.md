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
- `src/platform/sfx.c` — SDL2_mixer engine for footsteps (its own audio device).
  `Sfx_Init`, `Sfx_PlayFootstep(material)`. (Positional `Sfx_PlayPositional` exists but
  the overworld radar that used it is currently disabled.)
- `src/accessibility.c` / `include/accessibility.h` — game-side. `AX_DecodeString`
  converts the game's charmap encoding to readable text; `AX_SayGameString` speaks it.
  Shared helpers (`AX_AppendUint`) live here too.

Speech hooks (search for `AX_` / `Speech_` in these files):
- Dialogue/messages: `src/menu.c` (`AddTextPrinterForMessage*`), `src/battle_message.c`.
- Menus: `src/menu.c` (generic sMenu capture + `RedrawMenuCursor`), `src/list_menu.c`,
  `src/start_menu.c`, `src/main_menu.c`.
- Bag: `src/item_menu.c` (name + quantity, clean TM/HM/berry names).
- Battle: `src/battle_controller_player.c` (action menu, move menu + PP), `src/party_menu.c` (level/HP).

Footsteps: `src/event_object_movement.c` (`AX_PlayerFootstep` maps metatile behavior +
map type to a material). Init in `src/main.c` (`Speech_Init` + `Sfx_Init`).

## Conventions

- Guard every hook so it's a no-op when speech/audio is unavailable — never crash the game.
- New `src/*.c` and `src/*/*.c` are auto-globbed by `Makefile_pc`; no Makefile edit needed.
- Do not commit build outputs (`*.exe`, `*.dll`, `build/`) or the generated border BMPs.
- Prefer decoding real game data over scraping rendered text (e.g. build "TM 46, Thief"
  from item data rather than the on-screen label).

## Not yet accessible (roadmap)

Options screen, PC / item storage, naming screen, shop buy/sell confirmations,
Pokémon summary screen, bag Use/Give/Toss submenu.
