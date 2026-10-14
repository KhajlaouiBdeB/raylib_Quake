# Porting Quake to raylib

This fork ports id Software's GPL Quake (1999 source release) to
[raylib](https://www.raylib.com/). It follows the plan that worked for
raylib_DOOM: get the original code building and running on a modern 64-bit
system first, then replace the platform drivers one by one with raylib
drivers, and keep raylib out of the engine code.

## Status

| Stage | What | State |
|-------|------|-------|
| 1 | CMake build, 64-bit and modern-compiler fixes, null drivers, this survey | done |
| 2 | raylib drivers for video, input, sound and system | planned |
| later | GLQuake (`gl_*`), QuakeWorld (`QW/`), other platforms | planned |

## Building (stage 1)

Linux x86_64, GCC or Clang, CMake 3.16 or newer:

```sh
cmake -S . -B build -G Ninja        # or leave out -G Ninja to use make
cmake --build build
```

This gives `build/quake`. It uses the null video, sound, input and CD
drivers, so nothing is shown on screen, but the whole engine runs, including
the software renderer, which draws into a 320x200 memory buffer.

Game data is not in the repository. Put the shareware `pak0.pak` (or your own
registered data) in `<basedir>/id1/pak0.pak` and run:

```sh
./build/quake -basedir <basedir> +timedemo demo1          # software renderer benchmark
./build/quake -basedir <basedir> -dedicated 4 +map e1m1   # server, reads commands from stdin
```

stdout is block buffered when redirected; use `stdbuf -oL` (or a pty) to see
output from a run that is killed by `timeout`. `screenshot` writes a PCX of the
memory frame buffer into `<basedir>/id1/`, which is how the headless runs are
checked. The shareware version refuses `-game`, so use a separate `-basedir`
with its own `autoexec.cfg` for scripted tests.

## Source layout

`WinQuake/` holds one source tree for every platform of the 1996-1999
software-rendered client (DOS, Win32, Linux svgalib/X11, Solaris), plus the
GLQuake files. A platform build picks one driver of each kind.

### Engine core (platform independent)

| Area | Files |
|------|-------|
| Host, commands, cvars, console | `host.c`, `host_cmd.c`, `cmd.c`, `cvar.c`, `console.c`, `keys.c`, `menu.c`, `common.c`, `crc.c`, `mathlib.c`, `zone.c`, `wad.c` |
| Client | `cl_demo.c`, `cl_input.c`, `cl_main.c`, `cl_parse.c`, `cl_tent.c`, `chase.c`, `view.c`, `sbar.c`, `screen.c` |
| Server and QuakeC VM | `sv_main.c`, `sv_move.c`, `sv_phys.c`, `sv_user.c`, `world.c`, `pr_cmds.c`, `pr_edict.c`, `pr_exec.c` |
| Models | `model.c` (software), `gl_model.c` (GL) |
| Software renderer, front end | `r_aclip.c`, `r_alias.c`, `r_bsp.c`, `r_draw.c`, `r_edge.c`, `r_efrag.c`, `r_light.c`, `r_main.c`, `r_misc.c`, `r_part.c`, `r_sky.c`, `r_sprite.c`, `r_surf.c`, `r_vars.c` |
| Software renderer, rasteriser | `d_edge.c`, `d_fill.c`, `d_init.c`, `d_modech.c`, `d_part.c`, `d_polyse.c`, `d_scan.c`, `d_sky.c`, `d_sprite.c`, `d_surf.c`, `d_vars.c`, `d_zpoint.c`, `draw.c` |
| Network protocol | `net_main.c`, `net_dgrm.c` (datagram protocol), `net_loop.c` (local client to local server), `net_vcr.c` (record/playback) |
| Sound mixer | `snd_dma.c`, `snd_mem.c`, `snd_mix.c` (driven by a `SNDDMA_*` back end) |
| x86 assembly | `*.s` and `nonintel.c`; the assembly is never built (see below) |

### Platform drivers

| Kind | Interface | Existing drivers |
|------|-----------|------------------|
| System: files, time, errors, console, main loop | `sys.h` | `sys_linux.c`, `sys_win.c`, `sys_wind.c`, `sys_dos.c`, `sys_sun.c`, `sys_null.c` |
| Video | `vid.h` (`VID_*`, `D_BeginDirectRect`) | `vid_svgalib.c`, `vid_x.c`, `vid_win.c`, `vid_dos.c`, `vid_vga.c`, `vid_ext.c`, `vid_sunx*.c`, `vid_null.c`; GL: `gl_vid*.c` |
| Input | `input.h` (`IN_*`) plus `Sys_SendKeyEvents` | `in_win.c`, `in_dos.c`, `in_sun.c`, `in_null.c` (Linux had input inside `vid_svgalib.c` and `vid_x.c`) |
| Sound | `sound.h` (`S_*` or `SNDDMA_*`) | `snd_linux.c`, `snd_win.c`, `snd_dos.c`, `snd_gus.c`, `snd_sun.c`, `snd_next.c`, `snd_null.c` |
| CD audio | `cdaudio.h` | `cd_linux.c`, `cd_win.c`, `cd_audio.c` (DOS), `cd_null.c` |
| Network transport | `net_*.h` driver tables | `net_udp.c` + `net_bsd.c` (Unix), `net_wins.c`/`net_wipx.c`/`net_win.c` (Windows), DOS IPX/serial/modem drivers, `net_none.c` |

The null drivers are small stubs that were left in the release "to aid porting
efforts":

- `vid_null.c` sets up a 320x200 8-bit buffer, a z-buffer and a 256 KB surface
  cache, and shows nothing. The renderer runs for real.
- `snd_null.c` stubs every `S_*` call, so the mixer files are not built.
- `in_null.c` stubs `IN_*`. It now also has the `Sys_SendKeyEvents` stub that
  the Linux video drivers used to provide.
- `cd_null.c` stubs `CDAudio_*`.
- `sys_null.c` has file I/O and a fake clock; `sys_linux.c` is used instead
  because it has a real clock and reads server commands from stdin.
- `net_none.c` has no network drivers at all; stage 1 keeps loopback and UDP.

### Stage 1 driver set

`sys_linux.c`, `vid_null.c`, `in_null.c`, `snd_null.c`, `cd_null.c`,
`net_udp.c` + `net_bsd.c` (see `QUAKE_DRIVER_SOURCES` in `CMakeLists.txt`).

## Decisions

### Software renderer first

The software renderer draws an 8-bit paletted image into `vid.buffer`. A raylib
video driver only has to turn that into a texture each frame, which is the
same approach as raylib_DOOM. GLQuake would mean porting a fixed-function
OpenGL 1.x renderer instead, so it comes later, as does QuakeWorld (`QW/`),
which is a separate tree with its own client and server.

### No x86 assembly

`quakedef.h` and `quakeasm.h` now only define `id386` if the build has not, and
CMake passes `-Did386=0`. The C versions of every assembly routine are used on
every CPU, and no `.s` file is built. The assembly was written for 32-bit x86
and is no use on x86_64 or arm.

### Compiler settings

- **`-std=gnu11`**: Quake's `typedef enum {false, true} qboolean;` cannot
  compile as C23, where `bool`, `true` and `false` are keywords, and C23 is the
  default from GCC 15. Checked with `-std=gnu2x` on GCC 13 and Clang 18:
  `common.h:30: expected identifier before 'false'`.
- **`-fwrapv -fno-strict-aliasing`**: the code relies on signed overflow
  wrapping and on type punning (DOOM needed the same).
- **`-fsigned-char`**: the code assumes signed plain `char`. On arm, `char`
  is unsigned by default, which broke DOOM's demos (DOOM-28).
- **`-Wall`** with the GCC 14 errors (`implicit-function-declaration`,
  `implicit-int`, `int-conversion`, `incompatible-pointer-types`) turned into
  errors on older compilers too, so a GCC 13 build catches what GCC 14+ would
  reject.

### raylib dependency (stage 2)

As in raylib_DOOM: use an installed raylib 5.x if `find_package(raylib)` finds
one, otherwise download raylib 5.5 with CMake `FetchContent` and link it
statically. `-DCMAKE_DISABLE_FIND_PACKAGE_raylib=ON` forces the download (this
was needed on WSL, where CMake found the Windows MSYS2 raylib).
`QUAKE_RAYLIB` is the switch; it exists now but is `OFF` and stops the
configure step with an error if turned on.

### Planned raylib drivers

raylib.h cannot be included next to Quake headers (`bool`, `KEY_*`,
`Vector3`, `Color` ...), so every raylib call goes in `*_raylib.c` files. These
include only raylib.h and a small plain-C bridge header, never `quakedef.h`.

| File | Replaces | Plan |
|------|----------|------|
| `vid_raylib.c` | `vid_null.c` | Window, 8-bit `vid.buffer` + palette to RGBA texture, scaled drawing, resolution and fullscreen modes, palette shifts (`VID_ShiftPalette`) |
| `in_raylib.c` | `in_null.c` | Map raylib keys and mouse buttons to Quake `K_*` in `Sys_SendKeyEvents`; mouse look in `IN_Move`. Watch for `EndDrawing()` polling input (DOOM gotcha) |
| `snd_raylib.c` | `snd_null.c` | Build `snd_dma.c`, `snd_mem.c` and `snd_mix.c`, and implement `SNDDMA_*` on a raylib `AudioStream` that reads the DMA ring buffer |
| `sys_raylib.c` | `sys_linux.c` | Main loop, `Sys_FloatTime` and errors, for platforms without POSIX; on Linux `sys_linux.c` may stay |
| `cd_null.c` | (unchanged) | There is no CD. Music from OGG files in `id1/music/` could be added later |

## 64-bit and modern-compiler fixes

Found by building with GCC 13.3 and Clang 18 and running on x86_64, then
running under AddressSanitizer and UndefinedBehaviorSanitizer.

- **QuakeC strings** (`pr_edict.c`, `progs.h` and callers): QuakeC stores
  strings as `int` offsets from `pr_strings`. The engine also stores its own
  strings there (`pr_string_temp` from `ftos`/`vtos`/`etos`, `sv.name`, the
  world model name, client names). On 64-bit those can be further away than
  an `int` can reach, and `map e1m1` crashed in `strcmp` inside
  `PR_ExecuteProgram`. All conversions now go through `PR_GetString` and
  `PR_SetEngineString`. Strings out of range go in a table and get negative
  numbers.
- **Surface cache alignment** (`d_surf.c`): blocks were 4-byte aligned, but
  `surfcache_t` holds pointers. They are now pointer aligned (UBSan).
- **`net_dgrm.c`** defined its own `struct in_addr` with an `unsigned long`
  address (8 bytes on 64-bit) instead of using the system headers. The ban
  address and mask are now 32-bit.
- **`net_udp.c`**: `FIONREAD` writes an `int`, but the result was read as an
  `unsigned long`, so the top half was garbage. Socket lengths are `socklen_t`.
- **Pointer to `int` casts**: `STRUCT_FROM_LINK` uses `offsetof`, the surface
  cache size uses `offsetof`, `d_edge.c` and `r_draw.c` use `intptr_t` and
  `uintptr_t`.
- **Out-of-bounds loops**: `model.c` and `r_part.c` filled two-dimensional
  arrays through the first row (undefined behaviour that GCC flags as such).
- **`COM_FileBase`** read before the start of names without a `/`, such as
  `gfx.wad` (ASan).
- **`Draw_BeginDisc`** used `draw_disc` before `Draw_Init` had loaded it
  (UBSan).
- **Link errors with `-fno-common`** (GCC 10+ default): globals defined in
  headers or in more than one file (`d_local.h`, `d_edge.c`, `vid_null.c`).
- **Long paths**: `MAX_OSPATH` was 128, and a longer `-basedir` overflowed
  a buffer at startup (glibc fortify abort, found by the QUAK-3 harness).
  It is now 1024, and the path copies are bounded.
- **Missing prototypes and headers** (errors from GCC 14): `SV_RecursiveHullCheck`,
  `inet_addr`.
- **Buffer overflows**: path building now uses `Q_snprintf`; the server
  message no longer uses the map's text as a `printf` format; the scoreboard
  text is bounded; demo and savegame reads check `fread`/`fscanf` results.
- **C23**: the network poll callback is `void (*)(void)` instead of a
  prototype-less `void (*)()`.
- The rest of the `-Wall` warnings (braces, unused variables, trigraphs,
  signedness) are fixed, so the build has no warnings on GCC 13.3 and Clang 18.

### Still to look at

- About 30 `(long)` pointer casts in the renderer are fine on LP64 (Linux,
  macOS) but would truncate on 64-bit Windows (LLP64). Switch them to
  `intptr_t` when the Windows port starts.
- The heap is still 8 MB (`sys_linux.c`, `-mem` to change). The shareware maps
  fit, but 64-bit pointers make edicts and models bigger, so big custom maps
  may need more.
- `R_RenderView`'s stack sanity check fails under ASan's fake stack; run ASan
  builds with `ASAN_OPTIONS=detect_stack_use_after_return=0`.

## Verification

- `timedemo demo1/2/3` gives the same frame counts on x86_64 and on a 32-bit
  (`-m32`) build of the same code: 969, 985 and 1090.
- Screenshots during `timedemo demo3` show the same frames and game state on
  both. Comparing x86_64 with a 32-bit SSE build, the only differences are 33
  pixels in the 3D view and the console and notify area, which move with real
  time.
- Scripted local game under ASan and UBSan: all eight shareware maps, `god`,
  `impulse 9`, movement, `save`, `load`, then `timedemo demo1`. No reports.
- Dedicated server: `map e1m1`, `status`, `edictcount`, `changelevel e1m2`,
  `map start`, `quit` from stdin.
- QUAK-3 harness (`docs/TESTING.md`) with `--headless`: `launch`,
  `timedemo` and `soak` pass. Its `demo` mode passes `+startdemos` on the
  command line, but `pak0.pak`'s own `quake.rc` (which takes priority over a
  loose `quake.rc` in the same directory) runs `startdemos` again while
  demo1 is playing, and the stock engine then stops the loop after demo1.
  Without `+startdemos`, the attract loop plays demo1, demo2 and demo3.
