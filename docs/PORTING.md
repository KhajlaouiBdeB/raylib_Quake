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
| 2 | raylib drivers for video, input and system (QUAK-5) | done |
| 2 | raylib sound driver (QUAK-6) | planned |
| later | GLQuake (`gl_*`), QuakeWorld (`QW/`), other platforms | planned |

## Building

Linux x86_64, GCC or Clang, CMake 3.16 or newer. raylib's GLFW needs the X11
development headers (on Ubuntu: `libx11-dev libxrandr-dev libxinerama-dev
libxcursor-dev libxi-dev libgl1-mesa-dev`).

```sh
cmake -S . -B build -G Ninja        # or leave out -G Ninja to use make
cmake --build build
```

This gives two programs:

- `build/quake`: the game in a raylib window (`QUAKE_RAYLIB`, on by default).
- `build/quake-null`: the null video, sound, input and CD drivers and no
  raylib at all. Nothing is shown on screen, but the whole engine runs,
  including the software renderer, which draws into a 320x200 memory buffer.
  This is the headless and dedicated server build.

`-DQUAKE_RAYLIB=OFF` builds only the null drivers, as `build/quake`, and
needs neither raylib nor X11.

Game data is not in the repository. Put the shareware `pak0.pak` (or your own
registered data) in `<basedir>/id1/pak0.pak` and run:

```sh
./build/quake -basedir <basedir>                          # play
./build/quake -basedir <basedir> +timedemo demo1          # software renderer benchmark
./build/quake -basedir <basedir> -dedicated 4 +map e1m1   # server, reads commands from stdin
```

Either program runs a dedicated server; `quake` opens no window then, so it
also works without a display.

stdout is block buffered when redirected; use `stdbuf -oL` (or a pty) to see
output from a run that is killed by `timeout`. `screenshot` writes a PCX of the
memory frame buffer into `<basedir>/id1/`, which is how the headless runs are
checked. The shareware version refuses `-game`, so use a separate `-basedir`
with its own `autoexec.cfg` for scripted tests.

### macOS build

Xcode Command Line Tools (`xcode-select --install`) and CMake. raylib 6.0 is
downloaded and linked statically, so the executables depend only on system
frameworks (Cocoa, OpenGL, IOKit, CoreFoundation...). OpenGL is deprecated on
macOS; raylib still uses it and CMake prints a developer warning about it.
See the README for the full build and game-data steps.

```sh
cmake -S . -B build && cmake --build build                    # host architecture
cmake -S . -B build-x86_64 -DCMAKE_OSX_ARCHITECTURES=x86_64   # Intel / Rosetta
build/quake-null -dedicated -basedir data +map start          # headless server
```

`CMAKE_OSX_DEPLOYMENT_TARGET` defaults to 11.0 (the first arm64 release).
Pick one architecture per build tree: universal binaries are rejected at
configure time. `otool -L` on the executables lists only system libraries.

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

### Driver sets

- `quake` (raylib): `sys_raylib.c`, `vid_raylib.c`, `in_raylib.c`,
  `bridge_raylib.c`, `snd_null.c`, `cd_null.c`, `net_udp.c` + `net_bsd.c`
  (`QUAKE_RAYLIB_DRIVER_SOURCES`). Sound stays null until QUAK-6.
- `quake-null`: `sys_linux.c`, `vid_null.c`, `in_null.c`, `snd_null.c`,
  `cd_null.c`, `net_udp.c` + `net_bsd.c` (`QUAKE_NULL_DRIVER_SOURCES`).

The engine itself is compiled once (`quake_core`, an object library) and
linked into both.

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

### raylib dependency

raylib **6.0** (released 2026-04-23). CMake uses an installed raylib 6.x if
`find_package(raylib 6.0)` finds one, otherwise it downloads the 6.0 tag with
`FetchContent` and links it statically. `-DCMAKE_DISABLE_FIND_PACKAGE_raylib=ON`
forces the download (this was needed on WSL in raylib_DOOM, where CMake found
the Windows MSYS2 raylib); `-DFETCHCONTENT_SOURCE_DIR_RAYLIB=<dir>` builds a
raylib source tree that is already on disk. It uses raylib's default desktop
backend (GLFW), not the experimental native Win32 or Emscripten ones.

The downloaded raylib is built with `SUPPORT_SCREEN_CAPTURE=0`: raylib
otherwise saves a PNG of the window whenever F12 is pressed, and F12 is a
Quake key. An installed raylib may still do that.

The two raylib 5.5 problems from raylib_DOOM, checked again on 6.0:

- **No display**: with no `DISPLAY`, GLFW fails to initialise and
  `InitWindow` returns without a window; `quake` then stops with
  `Error: Couldn't open a window (is there a display?)` and exit status 1. It
  doesn't crash. A dedicated server never opens a window.
- **X11 only**: raylib 6.0's CMake still builds its GLFW with X11 only
  (`GLFW_BUILD_WAYLAND` is off by default), so on a Wayland desktop the game
  runs through XWayland. `-DGLFW_BUILD_WAYLAND=ON` builds X11 and Wayland
  support together (it needs `libwayland-dev`, `libxkbcommon-dev` and
  `wayland-protocols`); that build compiles and links but hasn't been run on a
  Wayland desktop yet.

### raylib drivers

raylib.h cannot be included next to Quake headers (`bool`, `KEY_*`,
`Vector3`, `Color` ...). `bridge_raylib.c` is the only file that includes
raylib.h, and it includes no Quake header. The engine-side drivers include
`quakedef.h` and the plain-C `bridge_raylib.h`, never raylib.h.

| File | Replaces | What it does |
|------|----------|--------------|
| `bridge_raylib.c` / `.h` | | Window, texture upload and scaled drawing, fullscreen, vsync; key, mouse and close-button polling. Reports keys as ASCII or its own `RLK_*` codes |
| `vid_raylib.c` | `vid_null.c` | Allocates the 8-bit frame buffer, z-buffer and surface cache; `VID_SetPalette`/`VID_ShiftPalette` fill a 256-entry RGBA table; `VID_Update` converts the frame through it and presents it |
| `in_raylib.c` | `in_null.c` | `Sys_SendKeyEvents` maps `RLK_*` to `K_*` and calls `Key_Event`; mouse look in `IN_Move`; decides when the pointer is captured |
| `sys_raylib.c` | `sys_linux.c` | Main loop, monotonic `Sys_FloatTime`, stdio file I/O, errors and quit. The POSIX parts (clock, `mkdir`, server commands from stdin) are marked |
| `snd_raylib.c` | `snd_null.c` | QUAK-6: build `snd_dma.c`, `snd_mem.c` and `snd_mix.c`, and implement `SNDDMA_*` on a raylib `AudioStream` that reads the DMA ring buffer |
| `cd_null.c` | (unchanged) | There is no CD. Music from OGG files in `id1/music/` could be added later |

#### Video

- `-width W -height H` set the size the renderer draws at (default 640x480,
  from 320x200 up to Quake's `MAXWIDTH` x `MAXHEIGHT`, 1280x1024).
  `-scale N` sets the first window size in multiples of that (default 2 up to
  400 wide, else 1), or `-winwidth`/`-winheight` set it directly.
- The window can be resized. The frame is scaled to fit with its aspect kept
  and black bars. `vid_integerscale 1` scales by whole multiples only;
  `vid_filter 1` uses bilinear filtering instead of nearest (both saved).
- Alt+Enter, `-fullscreen` or `vid_fullscreen 1` switch to borderless
  fullscreen on the current monitor (raylib's `ToggleBorderlessWindowed`; no
  video mode change). `vid_fullscreen` is not saved in `config.cfg`.
- Palette: the renderer still draws palette indices. A 256-entry table turns
  them into RGBA on the CPU before upload (640x480 is 300K lookups a frame).
  Damage, pickup and underwater flashes and the `gamma` cvar reach the
  driver as a new palette through `VID_ShiftPalette`/`VID_SetPalette`, as
  before. A palette shader could replace the table later.
- The whole frame is uploaded every `VID_Update`; the dirty rectangles are
  ignored. The "accessing disk" icon (`D_BeginDirectRect`) isn't drawn.

#### Frame rate and vsync

Quake itself runs at most 72 frames a second: `Host_FilterTime` returns
without running a frame if less than 1/72 s has passed (except in a
timedemo). This build has no `host_maxfps` cvar; that came with later
engines.

- `vid_vsync 1` (default, saved): `EndDrawing` waits for the display. At 60
  Hz the game runs at 60 frames a second; at 72 Hz or more it runs at 72, and
  frames of the display between Quake frames show the last one again (some
  judder at refresh rates that aren't a multiple of 72).
- `vid_vsync 0`: the game runs at 72 frames a second whatever the display.
- A timedemo always turns vsync off while it runs, so it measures the
  renderer, not the display. The frame counts (969/985/1090) don't depend on
  it.
- Between Quake frames the main loop sleeps 1 ms instead of spinning.

#### Input

- Keys: the bridge reports letters, digits and punctuation as lowercase ASCII
  (raylib's key codes are US layout positions), and everything else as
  `RLK_*` codes, which `in_raylib.c` maps to `K_*`. Left and right
  Shift/Ctrl/Alt, and the keypad keys with a main keyboard equivalent, are
  merged into one key, which goes up only when the last of them is
  released. The console's shifted characters come from keys.c's own table,
  as with every Quake driver, so the console types US layout characters.
- `EndDrawing` polls input and each poll empties raylib's pressed-key queue
  and auto-repeat flags (DOOM gotcha). `Sys_SendKeyEvents` reads what
  `EndDrawing`'s poll collected, polls again and reads again. A key pressed
  and released between polls gets a down and an up; a key pressed again
  before the poll that saw its release (a fast "pp") gets an up between its
  two downs.
- Auto-repeat: every OS repeat becomes another down event, as X11 sent them.
  Quake's `Key_Event` ignores repeats of every key except Backspace and Pause
  (keys.c), which is the stock 1.09 behaviour.
- Alt+Enter toggles fullscreen and neither the Enter press nor its release
  reaches Quake, so key events stay balanced.
- Mouse: while playing (`key_dest` is the game, connected, not a demo, and
  `_windowed_mouse 1`), the pointer is captured with raylib's
  `DisableCursor`, which also turns on GLFW raw (unaccelerated) motion where
  the platform has it. The console, menus and demo loop leave the pointer
  free. Motion is in pixels, the units the X11 driver used, so `sensitivity`
  feels the same; `m_filter` works as before. Buttons 1-3 and the wheel
  come in as `K_MOUSE1`-`K_MOUSE3` and `K_MWHEELUP`/`K_MWHEELDOWN` while
  captured. `-nomouse` turns the mouse off.
- Focus: the pointer is captured only while the window has focus; it is
  let go on Alt+Tab and captured again when the window gets focus back.
  GLFW releases held keys when focus goes, so none stay stuck.
- The window's close button quits at once (no "are you sure" menu) and
  saves `config.cfg`. raylib's Esc-to-close is turned off; Esc is Quake's
  menu key.
- Compile with `-DIN_RAYLIB_KEYLOG` to print every key event, the mouse
  motion and pointer capture changes to stdout.

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
- The heap is 16 MB in `quake` (`sys_raylib.c`) and still 8 MB in
  `quake-null` (`sys_linux.c`); `-mem` changes it. The shareware maps fit in
  8 MB, but 64-bit pointers make edicts and models bigger, so big custom maps
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

## macOS notes (QUAK-14)

- Built and linked on Apple Silicon (arm64, Apple Clang) with the FetchContent raylib 6.0; no project warnings.
- **Retina**: the window is opened with `FLAG_WINDOW_HIGHDPI`, so the frame is drawn at device-pixel resolution. `vid_integerscale` counts whole *device* pixels (2x on a Retina screen = 1 window unit per frame pixel).
- **Fullscreen**: Alt+Enter (Option+Enter) and Cmd+Enter toggle borderless fullscreen.
- **Data and save paths**: with no `-basedir`, the driver uses the current directory if it has `id1/`, then the executable's directory, then `../Resources` (app bundle), then `~/Library/Application Support/raylib_Quake` (created with an empty `id1/` so config.cfg and saves have a home).
- **Case-insensitive lookup**: `Sys_FileOpenRead`/`Sys_FileTime` retry a missing path ignoring case per component, so `ID1/PAK0.PAK` works on case-sensitive APFS volumes (checked on a case-sensitive APFS disk image).
- `SIGPIPE` is ignored.
- Not tested on a real Retina display with game data (no pak0.pak on this host): HiDPI scaling and mouse feel are unverified. Cocoa is known to swallow key-up events for keys released while Cmd is held; Quake does not use Cmd.
