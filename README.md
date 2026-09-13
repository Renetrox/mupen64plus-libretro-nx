# Mupen64Plus-Next

Mupen64Plus-Next is a N64 emulation library for the [libretro API](http://www.libretro.com/), based on Mupen64Plus (see below).

It is also the successor of the old Mupen64Plus libretro core.

> You can *always* rely on it to give you an excellent Majora's Mask experience. Seriously.

## Experimental HLE Frameskip branch

The `global-frameskip` branch adds an experimental **HLE Frameskip** mode derived from the automatic frameskip work tested in the Mupen64Plus FZ/ReARMed-based fork.

The first prototype works at the RSP HLE display-list boundary. When emulation falls behind, it skips the expensive graphics display-list call before the renderer runs, while still raising the normal DP completion signal so Mupen64Plus can schedule its regular DP interrupt.

Current core option:

- **HLE Frameskip**
  - `Disabled` (default)
  - `Auto`

The feature only affects the **HLE RSP** path. LLE RSP paths such as ParaLLEl-RSP and CXD4 are deliberately not skipped.

For this first validation build, the existing **Frame Duplication** option slot is temporarily reused for HLE Frameskip. Frame Duplication is therefore unavailable on this experimental branch until HLE Frameskip receives a permanent core-option entry.

### Linux PC test build

```bash
git clone -b global-frameskip https://github.com/Renetrox/mupen64plus-libretro-nx.git
cd mupen64plus-libretro-nx
make -j$(nproc) platform=unix
```

The resulting core should be `mupen64plus_next_libretro.so`. Test first with **RSP Plugin = HLE** and **RDP Plugin = GLideN64**. Compare `HLE Frameskip = Disabled` against `Auto` using the same game, resolution and RetroArch settings.

On a fast desktop PC, automatic frameskip may never need to activate. For validation, use a sufficiently demanding configuration or temporarily constrain CPU resources so the emulator falls behind real time.

#### How is this different from any N64 libretro-core, ever?

Due to the amount of libraries that are used and are in regular need of maintenance, I have strict rules about adding dependencies.  
This allows for easy maintenance, so available time can be spent on useful improvements and lowers the burden.  
By default the experience will be very simliar to the N64 emulators you know and love with *a lot extra*.

> **Sidenote:**  
While I accept pretty much every reasonable contribution, hacks must not impact behavior by default, unless justified.  
If you need to add a dependency, please consult me first.  
Force-pushes on all branches but `develop` and `master` are fair game.
`master` has the best stability memes, if that's your *thing*.

#### Used Technologies

The following projects have been incorporated into this repository:

- [mupen64plus](https://github.com/mupen64plus/mupen64plus-core)
- [GLideN64](https://github.com/gonetz/GLideN64)
- [cxd4](https://github.com/cxd4/rsp)
- [parallel-rsp](https://github.com/Themaister/parallel-rsp)
- [angrylion-rdp-plus](https://github.com/ata4/angrylion-rdp-plus) (Currently based on it's [ParaLLel](https://github.com/libretro/parallel-n64/) variant)

#### Acknowledgments

A special thanks to:

- The Mupen64Plus Team, especially Gillou68310
- gonetz and those that have worked on GLideN64, especially fzurita
- The Authors of cxd4 and angrylion-rdp-plus (ata4)
- themaister for parallel-rsp and parallel-rdp (including the Vulkan integration)
- Everyone in the libretro Team


\- m4xw
