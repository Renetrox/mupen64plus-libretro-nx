# Modular Mupen64Plus / libretro experiment

This directory starts a separate, non-destructive experiment on the `modular-plugins` branch.
The existing Mupen64Plus-Next build is intentionally left untouched while the modular architecture is validated.

## Goal

The final runtime should keep the normal Mupen64Plus module boundary instead of compiling every graphics/RSP plugin into one libretro binary:

```text
RetroArch
    |
    v
mupen64plus_modular_libretro.so   (frontend / bridge only)
    |
    +-- libmupen64plus.so
    |
    +-- mupen64plus-video-glide64mk2.so
    +-- mupen64plus-video-rice.so
    +-- mupen64plus-video-gln64.so
    |
    +-- mupen64plus-rsp-hle.so
    +-- ...
```

Graphics plugins remain ordinary Mupen64Plus shared libraries. Selecting a different graphics plugin should only change which `.so` the libretro bridge loads when content starts.

## Phase 0: prove the module boundary

`m64p-modular-probe` is deliberately **not** a libretro core yet. It validates the important part first:

1. `dlopen()` a real external `libmupen64plus.so`.
2. Resolve the official frontend API (`CoreStartup`, `CoreAttachPlugin`, `CoreDoCommand`, `CoreOverrideVidExt`, ...).
3. `dlopen()` normal external Mupen64Plus plugins.
4. Call each plugin's `PluginStartup()` with the real core handle.
5. Attach each plugin through `CoreAttachPlugin()`.
6. Detach/shutdown everything cleanly.

This tells us whether the standalone Linux core/plugins can be kept intact before adding RetroArch video, audio and input bridging.

## Build (Linux)

From the repository root:

```sh
cd modular
make
```

The test program only links against `libdl`; it does **not** link Mupen64Plus or a graphics plugin into the executable.

## Examples

Let the dynamic linker find the Mupen64Plus core and test only a graphics plugin:

```sh
./m64p-modular-probe - /path/to/mupen64plus-video-glide64mk2.so
```

Test the complete normal Mupen64Plus plugin set:

```sh
./m64p-modular-probe \
  /path/to/libmupen64plus.so.2 \
  /path/to/mupen64plus-video-glide64mk2.so \
  /path/to/mupen64plus-audio-sdl.so \
  /path/to/mupen64plus-input-sdl.so \
  /path/to/mupen64plus-rsp-hle.so
```

Use `-` for any plugin slot that should be skipped.

## Next milestone

Once this probe succeeds on Linux/ARM64, the next layer is the actual libretro bridge:

- reuse the proven libretro execution/cothread strategy from Mupen64Plus-Next;
- provide `CoreOverrideVidExt()` callbacks backed by the libretro hardware context;
- provide libretro-native audio and input plugins/bridges;
- expose a `Video Plugin` core option;
- load Glide64mk2 first, then Rice as the second proof of interchangeable external graphics modules.

FZ is only a reference for how useful options can be grouped/exposed per plugin. It is not a runtime dependency and its Android frontend/profile system is not part of this design.
