# Pokémon Emerald Voxel Project

An experimental Windows, Linux, and Android port of the [Pokemon Emerald decompilation](https://github.com/pret/pokeemerald).

This repository is a fork of [pokeemerald-multiplatform](https://github.com/KamiKitsune420/pokeemerald-multiplatform), created and maintained by **Adel Spence (KamiKitsune420)**. It runs the decompiled game code directly through SDL2; it is not a bundled GBA emulator and does not include a commercial ROM.

The main fork adds a desktop **Diorama renderer**: an OpenGL-based 3D presentation of the overworld that is built from the original Emerald maps, metatiles, palettes, behaviors, and sprites. The original game remains authoritative for gameplay, movement, collision, scripts, events, warps, encounters, and saves. The classic software-rendered 2D path remains available for the complete game and as a fallback for unsupported scenes.

## Platform Status

| Platform | Status | Output |
| --- | --- | --- |
| Windows | Working through the SDL2 backend | `pokeemerald.exe` |
| Linux | Working native 32-bit SDL2 build | `pokeemerald` |
| Android | Working experimental ARMv7 SDL2 build | `android/app/build/outputs/apk/debug/app-debug.apk` |
| GBA ROM | Upstream target | `pokeemerald.gba` |

## Port Changes

- Repaired the portable MP2K/M4A music player and sound mixer build.
- Added SDL2 float audio output at 42060 Hz.
- Sanitized invalid floating-point samples independently in the M4A and CGB audio paths, eliminating loud buzzing without discarding valid audio.
- Added output headroom and clipping protection.
- Fixed structure and pointer conversions required by the portable audio engine.
- Fixed portable BIOS, DMA, flash-save, trainer-card, and sound-related compilation errors.
- Added working save-file access through `pokeemerald.sav`.
- Added a Wine launcher for the Windows build.
- Added native 32-bit Linux compilation and SDL2 linkage.
- Added aspect-ratio-preserving 3:2 rendering with independently scaled background artwork and a transparent frame.
- Added persistent display settings with automatic support for additional numbered background images.
- Added an experimental Android SDL2/Gradle project and an ARMv7 cross-compilation pipeline.
- Added Android rendering, frame pacing, audio output, writable save storage, and lifecycle handling.
- Added an Android-native labeled multitouch overlay and SDL game-controller input.
- Added launcher icons on Android and an embedded multi-resolution icon on Windows.
- Added the desktop Diorama renderer, terrain meshing, OpenGL composition, and an explicit fallback to the original 2D renderer.

## Controls

| GBA control | Keyboard |
| --- | --- |
| A | `Z` |
| B | `X` |
| Start | `Enter` |
| Select | `Backslash` |
| L | `A` |
| R | `S` |
| D-pad | Arrow keys |
| Fast-forward | `Space` |
| Pause | `Ctrl+P` |
| Soft reset | `Ctrl+R` |

Windows XInput controllers are supported by the SDL2 backend. Android supports SDL-compatible gamepads, including D-pad and left analog-stick movement. Native Linux currently uses keyboard input.

### Diorama Controls

The following controls are available in desktop Diorama builds:

| Key or input | Action |
| --- | --- |
| `F3` | Toggle terrain mesh visualization and debug information |
| `F4` | Toggle between Diorama mode and the original 2D renderer |
| `,` | Decrease camera pitch |
| `.` | Increase camera pitch |
| Mouse wheel | Zoom the camera in or out |

## Windows Build (Classic Renderer)

The classic Windows target uses the 32-bit MinGW toolchain, SDL2, and ImageMagick. This build has not been tested and may not work. ImageMagick converts the PNG border assets to alpha-preserving BMP files supported by the Windows SDL2 build:

```sh
make -f Makefile_pc -j4
```

Place `SDL2.dll` beside `pokeemerald.exe`. On Linux, the Windows build can be launched through Wine with:

```sh
./launch.sh
```

## Linux Build (Classic Renderer)

The game data contains 32-bit pointers, so the native Linux target must currently be built as a 32-bit executable. Install a multilib C toolchain plus 32-bit SDL2 and SDL2_image development files, then run:

```sh
make -f Makefile_pc linux -j4
./pokeemerald
```

Linux objects are kept separately under `build/linux`, so they do not interfere with the Windows build.

The resulting executable is `pokeemerald` in the repository root.

## Diorama Desktop Build

The Diorama renderer is a separate desktop build configuration. Set `DIORAMA=1` explicitly to enable the OpenGL compositor and 3D overworld; leaving the variable unset or using `DIORAMA=0` builds the classic renderer. Diorama does not replace the original game logic or the software 2D path. It is currently supported on desktop Windows and Linux; the Android build remains on the classic renderer.

### Windows Diorama Build (MSYS2)

From an MSYS2 Git Bash shell, install the 32-bit MinGW SDL2 development packages and ImageMagick, then run. This build has not been tested and may not work:

```sh
make -f Makefile_pc DIORAMA=1 -j4 PREFIX= CPP=cpp SDL_DIR=/mingw32 \
  TMP="C:/Users/<you>/AppData/Local/Temp" \
  TEMP="C:/Users/<you>/AppData/Local/Temp"
```

The build uses the desktop OpenGL implementation supplied by Windows. Place `SDL2.dll` beside `pokeemerald.exe` before launching it. The same `./launch.sh` command can be used from Linux with Wine.

### Linux Diorama Build

The Linux Diorama target is also 32-bit and requires SDL2, SDL2_image, SDL2_mixer, and OpenGL development files for the 32-bit environment:

```sh
make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1 \
  PKG_CONFIG_32_PATH=/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig \
  -j4
./pokeemerald
```

On CachyOS or Arch Linux, install the validated dependencies first:

```sh
sudo pacman -S --needed base-devel pkgconf sdl2_image sdl2_mixer \
  lib32-sdl2-compat lib32-sdl2_mixer lib32-libpng lib32-zlib-ng-compat \
  lib32-libglvnd lib32-mesa
sudo pacman -U https://archive.archlinux.org/packages/l/lib32-sdl2_image/lib32-sdl2_image-2.8.12-1-x86_64.pkg.tar.zst
```

`lib32-sdl2_image` is no longer in the current multilib repositories; the second command installs the matching archived official package. Build and run the Diorama target with:

```sh
make -f Makefile_pc NATIVE_LINUX=1 DIORAMA=1 \
  PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig \
  -j"$(nproc)"
./pokeemerald
```

The `PKG_CONFIG_32_PATH` override is required when the Makefile defaults to Ubuntu's i386 pkg-config directory. The resulting executable is `pokeemerald` in the repository root, with Diorama objects stored separately under `build/linux-diorama`.

To build the matching classic Linux target on the same system, omit `DIORAMA=1`:

```sh
make -f Makefile_pc NATIVE_LINUX=1 \
  PKG_CONFIG_32_PATH=/usr/lib32/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig \
  -j"$(nproc)"
./pokeemerald
```

## Display Settings

The in-game Options menu includes a `DISPLAY` page. Settings apply immediately and are written to `pokeemerald.cfg`; Android stores the same config in the app's private storage.

Desktop builds support fullscreen, window size, integer scaling, VSync, border frame visibility, background selection, and volume. Android supports border frame visibility, background selection, and volume.

## Border Artwork

Windows, Linux, and Android use the same border assets from the repository root:

- `Border.png` is the transparent frame fitted around the centered 3:2 gameplay viewport.
- `BG.png` is the default background and scales independently to fill the complete output.
- `BG1.png`, `BG2.png`, and subsequent sequentially numbered files add selectable backgrounds after the default `BG` entry.

The background selector order is `BG`, `BG 1`, `BG 2`, and so on, followed by `OFF` for a plain black background. Numbered files must be contiguous; for example, `BG2.png` is only detected when `BG1.png` is also present.

Backgrounds and the frame should use a 1280x720 canvas. Keep the frame opening centered at the same location and dimensions as `Border.png` so it remains aligned at different output aspect ratios.

## Saving

Save data is read from and written to:

```text
pokeemerald.sav
```

Keep this file if you clean or move the build.

## Android Build

The Android project targets API 36 and `armeabi-v7a`. The 32-bit ABI is required by the game's current pointer layout. Android SDK 36, NDK `26.3.11579264`, CMake 3.22.1, and a compatible JDK are required.

Initialize SDL2 and apply the Android lifecycle patch once after cloning:

```sh
git submodule update --init --recursive
git -C android/SDL2 apply ../patches/sdl2-android-lifecycle.patch
```

Set `JAVA_HOME` and `ANDROID_HOME`, then build with SDL2's Gradle wrapper:

```sh
android/SDL2/android-project/gradlew -p android :app:assembleDebug
```

Install the debug APK on a connected device with:

```sh
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

Android saves are stored in the app's writable private storage. Windows and Linux continue to use `pokeemerald.sav` in the working directory.

Android includes a labeled multitouch overlay for the D-pad, A, B, Start, Select, L, and R.

## Upstream Project

This repository is based on the Pokémon Emerald decompilation. The upstream project builds the following ROM:

- `pokeemerald.gba`
- SHA-1: `f3ae088181bf583e55daf962a92bb46f4f1d07b7`

See [INSTALL.md](INSTALL.md) for the original decompilation setup and [pret.github.io](https://pret.github.io/) for other pret projects.

## Legal

Pokémon and Pokémon Emerald are trademarks of Nintendo, Creatures Inc., and GAME FREAK inc. This is an unofficial fan project and is not affiliated with or endorsed by those companies.

The scoped license in [LICENSE](LICENSE) applies only to original multiplatform-port modifications contributed through this fork. It does not relicense upstream code, third-party components, or copyrighted game assets.
