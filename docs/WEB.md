# Browser (WebAssembly) build

The software-rendered `quake` target also builds with Emscripten and raylib's
web platform (`PLATFORM_WEB`, WebGL 1 / OpenGL ES 2). It plays the
shareware episode in a page; it is live at https://www.raydelto.org/quake.

## Build

Needs an activated [emsdk](https://emscripten.org/docs/getting_started/downloads.html)
(built and tested with Emscripten 6.0.10), CMake ≥ 3.16 and Ninja or Make.
raylib 6.0 is downloaded by CMake, as for the desktop build.

```sh
./tools/fetch_shareware.sh            # data/id1/pak0.pak + data/shareware/{SLICNSE,LICINFO}.TXT
source ~/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -G Ninja
cmake --build build-web
python3 -m http.server -d build-web 8000   # then open http://localhost:8000/
```

Configuring fails if `pak0.pak` is not the checksum-pinned shareware 1.06
file (`-DQUAKE_WEB_PAK=...` points elsewhere) or the license texts are
missing. The PAK is only ever in the build directory, never in git.

`build-web/` then holds everything a web server needs:

| File | What |
|---|---|
| `index.html` (= `quake.html`) | The page, from `web/shell.html`: canvas, loading bar, controls, credits and license links |
| `quake.js`, `quake.wasm` | The engine (≈ 0.2 MB + 0.7 MB) |
| `quake.data` | The unmodified shareware `pak0.pak`, preloaded into `/quake/id1/pak0.pak` (18.7 MB) |
| `SLICNSE.TXT`, `LICINFO.TXT` | id's shareware license and distribution terms, which must accompany the PAK |
| `gnu.txt` | The GPL v2 |
| `.htaccess` | `application/wasm` for `.wasm`, `DirectoryIndex index.html` (Apache / LiteSpeed) |

Serve it over HTTP(S); `file://` URLs cannot load the `.data`/`.wasm`.

## How it differs from the desktop build

- **Frame loop:** `main` runs `Host_Init` and then hands
  `Sys_WebFrame` to `emscripten_set_main_loop`, which the browser calls
  once per display frame (`requestAnimationFrame`). `Host_Frame` already
  returns at once between its 72 Hz frames, so nothing else had to change.
  This was chosen over ASYNCIFY, which would keep the native `while (1)`
  loop but instruments the whole engine (larger, slower wasm) for one
  blocking call. The only blocking wait left, `SCR_ModalMessage` (the
  "start a new game?" question while a game is running), answers yes in
  the browser; raylib's web `WindowShouldClose` (it sleeps) is not called.
- **Data and saves:** `-basedir /quake`. The page mounts IndexedDB (IDBFS)
  at `/persist`, copies its files into `/quake/id1` before the game starts,
  and copies `config.cfg` and `*.sav` back whenever they change (checked
  once a second from the frame loop, on quit, and when the tab is hidden).
  The PAK is never stored in IndexedDB.
- **Quit and errors:** `Sys_Quit` and `Sys_Error` stop the main loop and
  call the page (`Module.quakeQuit` / `Module.quakeError`), which shows a
  message and a "Play again" (reload) button.
- **Audio:** the same `snd_raylib.c` DMA driver on raylib's miniaudio Web
  Audio backend. The game starts from the "Click to play" button, so the
  audio context is created inside a user gesture and is allowed to run.
- **Mouse:** browsers allow pointer lock only from a user's event, so the
  bridge tells the page when the game wants the mouse
  (`Module.quakeWantsPointer`: playing, not in a menu, the console or a
  demo) and the page locks it on the next click on the canvas. Motion comes
  from the browser's `movementX/Y`. `Esc` (the browser's) lets it go.
  The browser can still refuse the lock; the page then logs
  `Pointer lock refused: <reason>` (a console warning) and says so in the
  hint over the game, instead of leaving an unhandled promise rejection.
- **Touch:** on devices whose main pointer is a finger
  (`matchMedia('(pointer: coarse)')`; `?touch=1` / `?touch=0` force it on
  or off) the page puts touch controls over the game, which then fills the
  screen (landscape; a hint asks to turn the device in portrait; the
  controls keep clear of `env(safe-area-inset-*)`). The game tells the page
  which layout to show (`Module.quakePlaying`, set by `RL_SetPlaying` from
  `in_raylib.c`, the same test as for the mouse):
  - *play:* a stick wherever the left thumb lands (half way walks, the rim
    runs), drag on the right half to look (yaw and pitch, through
    `sensitivity`, `m_yaw`, `m_pitch`, no lookspring), FIRE, JUMP, WPN
    (next weapon), MENU;
  - *menu* (menus, console, demos): arrows, OK (Enter), BACK (Esc), Y, N,
    a tap on the picture is Enter, a swipe moves the cursor, and ⌨ opens
    the phone's keyboard (console, player name). `&console=1` adds a
    console button.

  The page calls two exports in `bridge_raylib.c`:
  `RL_WebTouch(bits, stickx, sticky, lookdx, lookdy)` and
  `RL_WebTouchKey(ascii)`. Button bits become key events in the bridge's
  queue: menu buttons are Esc, Enter, Y, N and the arrows; FIRE, JUMP and
  WPN are the otherwise unused `AUX1`–`AUX3` keys, bound to `+attack`,
  `+jump` and `impulse 10` the first time they are pressed unbound
  (default.cfg's `unbindall` clears them; Customize controls can rebind
  them), so they work whatever the keyboard is bound to. The stick and the
  look drag go to `IN_Move` (`RL_GetTouchMove`). Typed characters are a
  press and a release each, queued in order. Touches are taken in the
  document's capture phase and their default prevented, so raylib (GLFW)
  never sees them as mouse clicks and a finger never asks for pointer
  lock; touchend also resumes miniaudio's context (the game itself starts
  from the Play tap, a user gesture). The hidden text input's keys are
  stopped before GLFW's window-capture key listener. The desktop is
  unchanged: no overlay, the same keyboard, mouse and pointer lock.
- **Video:** the frame is 640×480 (`-width`/`-height` still work), drawn 2×
  into a 1280×960 canvas, which the page scales to fit, 4:3. Fullscreen is
  the page's: the Fullscreen button or `Alt+Enter`.
- **Network:** no sockets in a page; the build links id's `net_none.c`
  (loopback only), so single player and demos work and multiplayer does not.

Extra command-line options go in the URL: `?args=-skill%202%20%2Bmap%20e1m2`.

## Testing

What was checked for QUAK-25 (headless Chromium and Firefox through
Playwright, SwiftShader WebGL): the page loads with no console errors, the
demo loop runs after the click, the Web Audio context is `running`, Esc →
Single Player → New Game starts `start.bsp`, moving and firing work, the
game wants the pointer in play, `save` writes a `.sav` that reaches
IndexedDB, `quit` shows the end screen, and after a reload `config.cfg`
and the save are restored and `load` works. Not tested: real speakers,
real pointer lock with a physical mouse, Safari.

Touch (QUAK-31): `scripts/web_touch.mjs` (Playwright, headless Chromium,
Pixel 7 and iPhone 14 landscape emulation with real touch events through
CDP `Input.dispatchTouchEvent`, plus a desktop run):

```sh
node scripts/web_touch.mjs build-web web-touch [--only pixel7|iphone14|desktop]
# PLAYWRIGHT=/path/to/node_modules/playwright/index.mjs if not the default
```

On each phone, by touch only: the overlay turns on by itself, the audio
context runs, title → New Game (start map) → E1M1 (typed into the
console on the phone keyboard), save, move with the stick while firing,
look (yaw and pitch), jump, next weapon, save, load the first save and
check it came back, menu open/close, the portrait hint, and no pointer
lock request. Game state is read from the save files (origin, `v_angle`,
shells, weapon). Desktop: no overlay, keyboard play, pointer lock on a
click, mouse look, and `?touch=1` shows the overlay. Screenshots,
`result.json` and the console log go to the output directory. Emulation
is not a phone: real iOS Safari and Android Chrome are untested.

Headed Chromium under a bare Xvfb (no window manager) sometimes refuses
every pointer lock for the whole session with `WrongDocumentError` ("The
root document of this element is not valid for pointer lock."): the
browser process does not consider its window focused, although
`document.hasFocus()` is true under Playwright's focus emulation and
keys still arrive. QUAK-26 hit it in 15 of 18 runs of six or nine
concurrent sessions during one window, then in none of 36 more; it never
happened on a single session. Treat it as the test display, not the game,
and rerun that browser alone.

## Data license

The engine is GPL. The game data is not: only the unmodified shareware
`pak0.pak` is bundled, free of charge, together with `SLICNSE.TXT` (§6 lets
it be redistributed electronically, as a whole, free, with the agreement
accompanying it). Registered Quake data must never go into a page.
