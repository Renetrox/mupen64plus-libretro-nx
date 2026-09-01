# Modular Mupen64Plus / libretro experiment

This directory is a separate, non-destructive experiment on the `modular-plugins` branch. The existing Mupen64Plus-Next build remains untouched while the modular architecture is validated.

## Architecture

```text
RetroArch
    |
    v
mupen64plus_modular_libretro.so   (thin frontend / bridge)
    |
    +-- libmupen64plus.so.2       (real standalone Mupen64Plus core)
            |
            +-- mupen64plus-video-glide64mk2.so
            +-- mupen64plus-video-GLideN64.so
            +-- mupen64plus-audio-sdl.so       [temporary]
            +-- mupen64plus-input-sdl.so       [temporary]
            +-- mupen64plus-rsp-hle.so
```

The graphics plugins remain ordinary Mupen64Plus shared libraries. They are not linked into the libretro core.

## Build

From the repository root:

```sh
cd modular
make clean
make -j$(nproc)
```

This currently builds two targets:

```text
m64p-modular-probe
mupen64plus_modular_libretro.so
```

`m64p-modular-probe` validates the standalone Mupen64Plus ABI and plugin loader without RetroArch.

`mupen64plus_modular_libretro.so` is the first real libretro wrapper. It requests a RetroArch OpenGL context, installs a Mupen64Plus `CoreOverrideVidExt()` table, opens the ROM, dynamically loads normal Mupen64Plus plugins and runs the blocking Mupen64Plus execution loop in a libco coroutine. `VidExt_GL_SwapBuffers()` yields back to `retro_run()`.

## Runtime layout

The wrapper automatically searches for `runtime/` beside the libretro core and then one directory above it. The repository development layout can therefore be:

```text
mupen64plus-libretro-nx/
├── modular/
│   └── mupen64plus_modular_libretro.so
└── runtime/
    ├── libmupen64plus.so.2
    ├── mupen64plus.ini                 [recommended if available]
    └── plugins/
        ├── mupen64plus-video-glide64mk2.so
        ├── mupen64plus-video-GLideN64.so
        ├── mupen64plus-audio-sdl.so
        ├── mupen64plus-input-sdl.so
        └── mupen64plus-rsp-hle.so
```

Shared data files used by standalone Mupen64Plus/plugins can also be copied into `runtime/` during early testing.

## Probe example

```sh
./m64p-modular-probe \
  ../runtime/libmupen64plus.so.2 \
  ../runtime/plugins/mupen64plus-video-glide64mk2.so \
  ../runtime/plugins/mupen64plus-audio-sdl.so \
  ../runtime/plugins/mupen64plus-input-sdl.so \
  ../runtime/plugins/mupen64plus-rsp-hle.so
```

## RetroArch proof-of-concept

From the repository root, after building and preparing `runtime/`:

```sh
retroarch -L ./modular/mupen64plus_modular_libretro.so /path/to/game.z64
```

The first core option is:

```text
Video Plugin
  glide64mk2
  GLideN64
```

Restart content after changing the graphics plugin.

## Current limitations

This is deliberately an early proof of architecture:

- audio currently uses the external SDL Mupen64Plus plugin, not a libretro audio bridge;
- input currently uses the external SDL Mupen64Plus plugin, not a libretro input bridge;
- savestates and exposed memory are not implemented yet;
- only OpenGL is requested at this stage;
- plugin-level frameskip and full plugin option categories come later;
- frame yielding currently follows the graphics plugin's buffer-swap path and must be tested with skipped frames.

The next target is to boot a game with external Glide64mk2, then repeat the same test with GLideN64 by changing only the loaded graphics `.so`. After that, SDL audio/input will be replaced by small libretro-native bridge plugins.

FZ remains only a reference for useful per-plugin options and organization. Its Android frontend/profile system is not a runtime dependency.
