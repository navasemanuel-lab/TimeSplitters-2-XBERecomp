# TimeSplitters 2 - XBE Recomp

An unofficial native PC port of **TimeSplitters 2** (Xbox, USA release), made by static recompilation of the game's `default.xbe`, with a Direct3D 12 renderer, mouse and keyboard, modern controller support and a number of fixes and extras over the original.

Built with the [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp) tool.

> [!NOTE]
> This project does not include the game. You must provide your own legally acquired copy of the **USA Xbox** version (an `.iso`, or the extracted files). Other regions or versions are not supported: the launcher checks the `default.xbe` of the image and refuses a different one.

## Installation

1. Download the release zip and extract it anywhere.
2. Run `TS2Launcher.exe`. Press **Seleccionar ISO USA...** and pick your ISO (or put the extracted game files, `default.xbe` first, in the `assets` folder).
3. Press **JUGAR**. The first start compiles the game's code for your PC with your own copy; it takes a few minutes and happens once.
4. In game, **F1** opens the settings panel (Back + Start on a pad).

The launcher has five pages: Play, Options, Profiles, MapMaker maps and Updates (new versions are published as GitHub releases and installed from there).

## What is different from the original

### Controls
- Keyboard and mouse as the first player's pad, every key rebindable (primary and alternate). True 1:1 raw-input mouse aim, with separate sensitivity, vertical speed and aim speed; the mouse also works in the front end's menus.
- Xbox, PlayStation and Switch controllers, hot plugging, rumble, and optional gyro aiming (DualSense, DualShock 4, Switch Pro).
- Per-axis speed for the stick and the mouse, adjustable crosshair sway.
- Button prompts drawn as the buttons of the input in use (keyboard, Xbox, PlayStation, Switch), and the game's A and Y button pictures drawn the right way up.
- Typing on the profile name screen with the PC keyboard.
- Free camera (F6) for looking around a level.

### Video and interface
- Direct3D 12 renderer; field of view up to 100.
- Menus, loading screens and movies stretched to the screen's shape instead of 4:3 with bars; the front end's frame laid out for wide screens as TS2 Redux does it.
- HUD options: ammunition display larger and lower, tall health and shield arcs, crosshair always shown, a TimeSplitters 1 style health bar, HUD pictures kept at the shape they were made for, and a weapon bar like Future Perfect's.
- HD front-end pictures and HUD pieces (TS2 Redux, bekoha), in the release as `hd_textures.bin` and `hd_hud.bin`.
- The levels' fine detail textures on every piece of the level, not only the near ones.
- Lights: the lights that follow models and the light of particles are kept across frames.
- Performance overlay and logging (frames a second, where each frame's time goes).


### Content that was in the game but cut
- Hidden Arcade maps **Dam** and **Wild West** opened in the Arcade list.
- The MapMaker's cut **Gothic tileset**, MapMaker Story maps playable by every connected controller (up to four) and the MapMaker's memory caps lifted.
- The cut **Default Gun** (a Desert Eagle) in the Arcade weapon lists.
- Characters that are in the game's tables but not in the Select Character list (Gasmask Special, Consultant, the NeoTokyo pedestrians, X-Ray Skel).
- **Chemical Plant**: the TimeSplitters 1 level, converted from the PS2 game's data into the Arcade map list. It is made from your own files by `tools/mk_chemical_plant.py` and is shown only if `resourcepacks/chemical_plant` exists. It is a work in progress (some floor and brick textures are misaligned).


### Options
Options that change the game's balance or look are off by default, so the defaults behave like the original:
Rocket Launcher in every Story level, infinite health, one-hit kills, always-shown crosshair, TimeSplitters 1 health bar, and **TS2 Beta Weapons**.
- An **Unlock Everything** line in Player Options, and a **Quit to Desktop** line in the main menu.

**TS2 Beta Weapons** replaces the gun models with the beta weapons from `resourcepacks/beta_weapons/gun.pak` (only the gun models; no level is touched). The release includes that pak.


### Launcher
`TS2Launcher.exe` (Dear ImGui, Direct3D 11): a blue theme with the game logo and rotating key art, ISO check and import, the options above, profile and MapMaker map import and export, and updates from GitHub releases (a token can be set for a private repository).

### Tools
`tools/` has the Python scripts used to build this: the HD texture bundles, button icon sheets, language tables, pak inspection, and converters for TimeSplitters 1 level data (`ts1level.py`, `ts1_to_xbr.py`) used for Chemical Plant.

## System requirements

- Windows 10 64-bit
- A GPU with support for Direct3D 12
- Your own copy of TimeSplitters 2 (Xbox, USA)

## Building

1. Clone this repository.
2. Put the extracted game files in `assets` (`default.xbe` with the game's folders beside it).
3. Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows** (`build.bat` offers to add the last two).
4. Run `build.bat`. The first build recompiles the game and takes a while. The exe and the launcher are written to `out\build\release`.
5. `release.bat` builds the release zip.

## Credits

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBE-Recomp), the recompiler and runtime this is built on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) for the overridable function layout and instruction hooks.
- [Zydis](https://github.com/zyantific/zydis), [toml++](https://github.com/marzer/tomlplusplus), [Dear ImGui](https://github.com/ocornut/imgui) and [SDL](https://github.com/libsdl-org/SDL).
- TS2 Redux and bekoha for the HD pictures, fonts and HUD pieces.

The full list of credits is in [CREDITS.md](CREDITS.md). TimeSplitters 2 belongs to its rights holders; this is a fan project and is not affiliated with them.
