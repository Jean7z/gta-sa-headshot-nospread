# SA Android Headshot & NoSpread

Zero-spread headshots for GTA: San Andreas for Android 2.10 — your shots go
exactly where the reticle points, and the auto-aim always locks onto the head.

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](./LICENSE)
[![Version](https://img.shields.io/badge/version-1.0-green.svg)](https://github.com/Jean7z/gta-sa-headshot-nospread/releases)
[![Platform](https://img.shields.io/badge/platform-Android-blueviolet.svg)]()

> A mod for [Android Mod Loader (AML)](https://github.com/AndroidModLoader/AndroidModLoader)
> by RusJJ. The official SA Android plugin SDK
> ([aml-psdk](https://github.com/AndroidModLoader/aml-psdk)) is used and provided
> as a git submodule.

---

## Features

- **Headshot always** — both the reticle and the hit point are forced to the
  head bone, including while switching targets.
- **NoSpread** — the bullet spread picked up inside
  `CWeapon::FireInstantHit` is zeroed, so bullets land where the crosshair is.
- **Player-only by default** — NPC gunfights stay 100% stock: the scatter is
  removed only when the *player* fires.
- **Softened aim magnet** — the auto-aim head range is scaled/floored through
  config (no more 1e6 instant-snap).
- **Every feature togglable** via the AML config file.
- **Clean uninstall** — just delete the `.so`, nothing else touched.

## How it works

### NoSpread (arm64)

`CWeapon::FireInstantHit` loads/recomputes the weapon spread into `s8` at 9
sites. Each site is replaced with `fmov s8, wzr` (zero spread).

In **Player-only** mode each site is instead redirected with a 5-instruction
trampoline that executes the *original* scatter op (vanilla for NPCs) and then
zeroes `s8` only when the shooter is the player ped (`CPed::m_pPlayerData` at
`+0x540` is non-null **only** for the player):

```
ldr s8, [x8, #0xb54]     ; vanilla spread load (NPCs)
ldr x16, [x19, #0x540]   ; m_pPlayerData — null unless player
cbz x16, +8              ; skip the zeroing for NPCs
fmov s8, wzr             ; player -> zero spread
ret
```

Trampolines live in free anonymous mappings placed as close as possible
within `bl` range of the sites (movable code, no game code is patched besides
the single `bl`s).

### Always-head bone (arm64)

`CPlayerPed::ProcessControl` ends its target-bone decision with
`csel w8, w2, w8, NE` so the HEAD bone wins only while the target is strictly
in-range; a frame spent re-aiming picks SPINE and the shot lands on the chest.
That instruction is replaced with `mov w8, #5` (BONE_HEAD), so the reticle and
the impact are always on the head. It lives inside `ProcessControl`, therefore
only affects the player.

### Head range hook (both ABIs)

`CWeaponInfo::GetTargetHeadRange` already computes a dynamic, weapon-derived
value (`m_fWeaponRange * K * (skill + 2)`). The mod scales that natural value
by `HeadRangeMul` and floors it at `HeadRangeMin`, softening the aim
"magnetism" instead of snapping to 1e6.

## Requirements

- **GTA: San Andreas** for Android **2.10** (play store version).
- **[Android Mod Loader (AML)](https://github.com/AndroidModLoader/AndroidModLoader)**
  installed and working (the game must load `libAML.so`).
- **arm64-v8a** recommended — the NoSpread + head-bone patches are arm64.
  On a 32-bit (`armeabi-v7a`) build only the head-range hook applies.

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
| `Headshot` | `true` | Always aim/lock onto the head bone |
| `NoSpread` | `true` | Zero the bullet spread (arm64) |
| `PlayerOnly` | `true` | Only affect the player's shots; NPCs stay vanilla |
| `HeadRangeMul` | `2.0` | Scale of the natural auto-aim head range |
| `HeadRangeMin` | `30.0` | Floor for the auto-aim head range |

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
- NoSpread/head-bone patches are verified against the stock **arm64**
  `libGTASA.so` 2.10; `PlayerOnly` mode leaves NPC gunfights untouched.
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