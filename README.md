# SA Android Headshot & NoSpread

More precise shots for GTA: San Andreas for Android 2.10 — bullets land much
closer to where the crosshair points, and the auto-aim favors the head more
than in the original game.

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](./LICENSE)
[![Version](https://img.shields.io/badge/version-1.0-green.svg)](https://github.com/Jean7z/gta-sa-headshot-nospread/releases)
[![Platform](https://img.shields.io/badge/platform-Android-blueviolet.svg)]()

> A mod for [Android Mod Loader (AML)](https://github.com/AndroidModLoader/AndroidModLoader)
> by RusJJ. The official SA Android plugin SDK
> ([aml-psdk](https://github.com/AndroidModLoader/aml-psdk)) is used and provided
> as a git submodule.

---

## Features

- **Increased shot precision** — the bullet spread applied when the player
  fires is reduced, so shots land much closer to the crosshair (arm64).
- **Headshot-focused aiming** — the auto-aim and locked shots favor the head
  far more consistently than in the original game.
- **Player-only by default** — NPC gunfights keep the original behavior; only
  *your* shots are affected.
- **Adjustable aim assist** — how strongly the auto-aim pulls toward the head
  can be tuned in the config.
- **Every feature togglable** via the AML config file.
- **Clean uninstall** — just delete the `.so`, nothing else touched.

## How it works

- **Reduced spread (arm64):** the game applies a bullet-spread value to every
  shot inside `CWeapon::FireInstantHit`. The mod overwrites those values for
  the player, so firefights stay more accurate than in the vanilla game. NPCs
  use the original values.
- **Headshot-focused aim:** the game decides whether to aim at the head or the
  chest each frame. The mod makes the head win far more often, including while
  switching targets, so most hits land on the head. A few shots can still land
  on the body depending on the situation — that's how the original engine
  behaves.
- **Aim distance:** the game picks a "head lock" range per weapon. The mod
  lets you scale and clamp that range (see `HeadRangeMul` / `HeadRangeMin`).

## Requirements

- **GTA: San Andreas** for Android **2.10** (play store version).
- **[Android Mod Loader (AML)](https://github.com/AndroidModLoader/AndroidModLoader)**
  installed and working (the game must load `libAML.so`).
- **arm64-v8a** recommended — the precision/aim improvements are for 64-bit.
  On a 32-bit (`armeabi-v7a`) build only the aim-distance tweak applies.

## Installation

1. Grab the latest **`.so`** from the [Releases](https://github.com/Jean7z/gta-sa-headshot-nospread/releases)
   page. Pick the one that matches your architecture:
   - `libAML_PSDK_Aimbot64.so` → **arm64-v8a** (64-bit, most devices)
   - `libAML_PSDK_Aimbot.so`   → **armeabi-v7a** (32-bit)
2. Push it into the game's mods folder:
   `/Android/data/com.rockstargames.gtasa/mods/` — or the unprotected path on
   your device.
3. Start the game. The mod is active automatically.

### Uninstall

Delete the `.so` from the mods folder. Nothing else is changed.

## Configuration

All keys live under the `[Aimbot]` section of the AML config file:

| Key | Default | Description |
|-----|---------|-------------|
| `Headshot` | `true` | Prefer aiming at the head for the player |
| `NoSpread` | `true` | Reduce bullet spread for the player (arm64) |
| `PlayerOnly` | `true` | Only affect the player's shots; NPCs stay original |
| `HeadRangeMul` | `2.0` | How far the auto-aim can pull toward the head (higher = stronger) |
| `HeadRangeMin` | `30.0` | The auto-aim always works at least this far away |

## Building from source

### Prerequisites

- [Android NDK](https://developer.android.com/ndk/downloads) (r21 or newer; r29 recommended)
- `git`

### Steps

```bash
# 1. Clone the repository including the aml-psdk submodule
git clone --recurse-submodules https://github.com/Jean7z/gta-sa-headshot-nospread.git
cd gta-sa-headshot-nospread

# 2. Build with the NDK's ndk-build
$ANDROID_NDK_HOME/ndk-build NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./Android.mk NDK_APPLICATION_MK=./Application.mk
```

The resulting libraries land in `libs/`:

```
libs/arm64-v8a/libAML_PSDK_Aimbot64.so
libs/armeabi-v7a/libAML_PSDK_Aimbot.so
```

> Environment tip: make sure `ANDROID_NDK_HOME` points at your NDK directory
> (the one containing `ndk-build`), or call the full path to `ndk-build`
> directly.

### Reusing an existing aml-psdk checkout (no submodule needed)

If you already have the SDK checked out somewhere, you can use it instead of
the submodule:

```bash
mv psdk psdk.bak && ln -s /path/to/aml-psdk psdk
```

## Project layout

```
.
├── Android.mk          # ndk-build makefile (selects module name per ABI)
├── Application.mk      # ABI targets and toolchain settings
├── main.cpp            # the entire mod
├── mod/                # AML mod interface helpers (logger/config), vendored
└── psdk/               # aml-psdk submodule (official SA Android SDK headers)
```

The `mod/` helpers and the `psdk/` SDK headers are the same pieces the official
AML mods use (`RusJJ/AndroidModLoader` + `AndroidModLoader/aml-psdk`, both MIT).

## Compatibility

- Game: GTA San Andreas **2.10** for Android.
- The precision and aim patches target the stock **arm64** `libGTASA.so` 2.10;
  `PlayerOnly` mode leaves NPC gunfights untouched.
- Tested alongside the `net.psdk.samod.unlimitedgym` mod.

## Credits

- [RusJJ](https://github.com/AndroidModLoader) — Android Mod Loader, the mod
  interface helpers (`mod/`) and the SA Android SDK (`aml-psdk`), all MIT.
- [GTA: San Andreas Reverse Engineering](https://github.com/gta-reversed/gta-reversed-android)
  — reference documentation of the game's engine and scripts.

## License

MIT — see [LICENSE](./LICENSE). This project is not affiliated with Rockstar
Games or Take-Two Interactive. GTA: San Andreas and its trademarks belong to
their respective owners. Use at your own risk.