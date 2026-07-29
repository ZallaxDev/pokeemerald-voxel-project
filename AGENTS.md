# AGENTS.md

## Workspace scope

- This directory is not a Git repository. It contains two independent repositories; run Git and build commands from the relevant child directory.
- `/` is the product. Make all implementation changes there unless the user explicitly requests otherwise.
- `../DramaticShapeVoxelMod/` is a Pokemon Gen 1/LÖVE Lua reference only. Use it to study visual rules and techniques; do not treat its APIs, coordinates, collision model, or assets as compatible with Emerald, and do not modify it as part of product work.
- The product objective and implementation order are in `/pokeemerald_diorama_plan_implementacion.md`. It is a target design, not proof that a feature exists; verify proposed files, flags, and commands against the current tree before using them.

## Product invariants

- Preserve the original game as the authority for movement, collision, scripts, events, warps, encounters, and saves. Diorama code may observe and render state but must not decide or write gameplay state.
- Preserve the complete software-rendered 2D path for menus, battles, transitions, unsupported scenes, errors, visual comparison, and builds without diorama support.
- Keep desktop builds 32-bit (`-m32`); game data contains 32-bit pointers. Do not combine diorama work with a 64-bit migration.
- `AgbMain()` runs on a game thread while SDL presents on the main thread. Rendering code must not read mutable globals such as map, object, sprite, VRAM, or palette state concurrently. Publish immutable, value-only snapshots after overworld updates and consume only a fully published snapshot on the graphics thread.
- Follow the plan's dependency order: 2D OpenGL parity, safe snapshots, real flat map, elevation, objects, rules, then the vertical slice. Do not start with decorative geometry while synchronization, map changes, fallback, atlas/palettes, and invalidation remain unresolved.
- The initial vertical slice is Littleroot Town and Route 101. Desktop OpenGL 3.3 is the first target; Android/GLES must not block it.

## Current architecture

- Desktop uses `Makefile_pc`, `PLATFORM_SDL2`, and `RENDERER_EASY_DRAW`. `src/platform/sdl2.c:VDraw()` converts the 240x160 BGR555 software frame to ARGB8888 and uploads an SDL texture.
- `src/overworld.c:OverworldBasic()` updates scripts, tasks, sprites, camera, OAM, palettes, and tileset animations. A future scene snapshot belongs after these updates, without replacing the existing `AX_OverworldScan()` accessibility hook.
- The current tree has no `DIORAMA` make variable or diorama modules. When adding the flag, `DIORAMA=0` must retain current behavior and `DIORAMA=1` must use one OpenGL compositor rather than mixing `SDL_Renderer` and OpenGL on the same window.
- `Makefile_pc` auto-discovers C sources under `src/`, `src/*/`, and `src/*/*/`; new files there normally need no explicit source list edit.

## Accessibility fork

- Read `/CLAUDE.md` before touching UI, text, input, overworld hooks, platform audio, or `src/platform/sdl2.c`; it documents the fork's non-obvious NVDA and SDL2_mixer behavior.
- Keep platform speech isolated in `src/platform/speech.c`, game-side decoding/hooks in `src/accessibility.c`, and optional sound cues in `src/platform/sfx.c`. Missing NVDA, its DLL, or optional audio must remain a safe no-op.
- The game audio and accessibility SFX use separate SDL audio devices; do not merge them accidentally. SDL2_mixer channel 0 is reserved for high-quality cries.
- Reader keys are edge-triggered and globally latched; a new consumer must drain `Platform_GetReaderKeys()` when its screen opens to avoid replaying stale presses.
- After adding or renaming maps, regenerate announcements with `python tools/gen_ax_map_names.py` from the product repository.

## Build and verification

- Native Linux desktop: `make -f Makefile_pc linux -j4`, then `./pokeemerald`. This is a 32-bit build and needs a multilib compiler plus 32-bit SDL2 and SDL2_image development packages.
- Windows from MSYS2 Git Bash: `make -f Makefile_pc -j4 PREFIX= CPP=cpp SDL_DIR=/mingw32 TMP="C:/Users/<you>/AppData/Local/Temp" TEMP="C:/Users/<you>/AppData/Local/Temp"`. Pass `TMP` and `TEMP` as make variables; do not set `TMPDIR` to a Windows path. The current Makefile invokes `magick` to generate missing border BMPs.
- Android setup is one-time: `git submodule update --init --recursive`, then `git -C android/SDL2 apply ../patches/sdl2-android-lifecycle.patch`. Build with `android/SDL2/android-project/gradlew -p android :app:assembleDebug` after setting `JAVA_HOME` and `ANDROID_HOME`.
- There is currently no repository-level lint, unit-test, or CI command. For changes now, at minimum build the affected desktop target and smoke-test launch, input, audio, save/load, resize, and fullscreen as applicable.
- Once `DIORAMA` exists, every renderer change must build both classic and diorama paths; the planned Linux diorama command is `make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1`, but it is not valid until the flag is implemented.
- Do not commit `build/`, executables, DLLs, generated border BMPs, `pokeemerald.sav`, `pokeemerald.cfg`, or downloaded `sounds/cries/`. Preserve committed `sounds/steps/` and `sounds/interacts/` assets.

## Reference map

- In `../DramaticShapeVoxelMod/`, start with `main.lua` for pipeline/fallback behavior, `lib/TileShape.lua` plus `data/voxel_heights.lua` for classification, `lib/Structures.lua` and `lib/Buildings.lua` for grouped geometry, and `lib/ChunkMesher.lua` for caching/invalidation.
- Reuse concepts, not code assumptions: the reference consumes Gen 1 extractor data and LÖVE render pipelines, while the product consumes Emerald metatiles, behaviors, elevations, OAM, palettes, and SDL/OpenGL state.
