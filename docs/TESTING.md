# Runtime verification

Run from the repository root on Linux x86_64. The harness accepts an executable
path and preserves each trailing argument as a separate argument; it does not
build the engine. Use a direct executable, not a daemonizing launcher. All child
work runs in the foreground and is reaped before the harness returns.

```sh
tools/fetch_shareware.sh
tools/verify.sh launch /absolute/path/to/quake -width 640 -height 480
tools/verify.sh demo /absolute/path/to/quake
tools/verify.sh timedemo /absolute/path/to/quake
tools/verify.sh soak /absolute/path/to/quake
# Null-video build: explicitly acknowledge that no screenshot is possible.
tools/verify.sh launch --headless /absolute/path/to/headless-quake
```

Harness options precede the binary. `--data DIR` selects a directory containing
`id1/pak0.pak`; `--output DIR` must name a new directory; `--timeout SECONDS`
defaults to 300 per demo/timedemo or startup; `--settle SECONDS` defaults to 3
for launch. Soak always observes **60 seconds after initialization**. Outputs
default to unique directories in gitignored `verification-results/`.

## Data provenance

The fetcher downloads only [Quake 1.06 shareware](https://www.gamers.org/pub/idgames/idstuff/quake/quake106.zip).
It checks the archive before extracting `resource.1`, then reads only
`ID1/PAK0.PAK` from its self-extracting LZH with 7-Zip. It never executes DOS
programs or extracts registered content. Hashes are pinned against
[Debian game-data-packager's Quake manifest](https://alioth-lists-archive.debian.net/pipermail/pkg-games-commits/2015-January/023332.html)
and were reproduced by the fetch dry run:

| File | SHA256 |
|---|---|
| `quake106.zip` | `ec6c9d34b1ae0252ac0066045b6611a7919c2a0d78a3a66d9387a8f597553239` |
| Shareware `id1/pak0.pak` (18,689,235 bytes) | `35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af` |

An existing valid PAK is reused; a mismatched one fails without overwriting it.
Downloads have connection/transfer timeouts. `data/`, results, and all `*.pak`
and `*.PAK` files are ignored. Never commit or attach PAKs or registered data.

## Check contract

Each invocation allocates its own Xvfb display with `-displayfd`, disables TCP,
and starts the binary with `-nosound -nocdaudio`, SDL dummy audio, OpenAL null,
and a private ALSA null configuration. These are non-audio tests; they do not
verify audible output or mixer pacing. The harness terminates only its own
engine and Xvfb `Popen` children, with a bounded TERM/KILL fallback, including
on interruption. It never kills by process name or signals Multica.

Each run creates a private basedir, symlinks the checksum-verified shareware
PAK, and overrides `quake.rc` with `exec default.cfg; stuffcmds` on separate
lines. This avoids the packaged attract loop racing test commands and avoids
reading or changing personal configs. Engine args are forwarded first; the
harness owns `-basedir`, `-condebug`, audio flags and test commands. Mods and
registered data are outside this harness's scope.

| Mode | Required evidence |
|---|---|
| `launch` | `Quake Initialized` or harness `QA_VERIFY_READY` echo marker, live process for the settling interval, visible window owned by that PID, window-only PNG showing the console (`+toggleconsole`). Inspect the PNG to confirm recognizable output. |
| `demo` | `+startdemos demo1 demo2 demo3`; ordered log transitions demo1 → demo2 → demo3 → demo1 show a complete loop; live process and window-only PNG. |
| `timedemo` | Separate processes for demo1, demo2, demo3; positive frames/seconds/fps parsed from Quake's report and printed, live process, window-only PNG per demo. |
| `soak` | Initialization followed by 60 s of process liveness, stderr collection, and a window-only PNG. It starts the shareware demo loop. |

All modes fail on premature exit, timeout, missing window, matched engine fatal
errors, or capture failure. `--headless` skips the window/PNG requirement and
prints that skip explicitly; it still requires initialization for launch/soak
and demo/metric evidence for demo/timedemo. A live but hung process can pass soak;
review demo output and screenshots separately. Demo transitions are observable
completion evidence, not a proof of gameplay synchronization or correctness.
The readiness echo is executed through `stuffcmds` and captured by `-condebug`,
so buffered platform stdout cannot hide startup readiness.

No synthetic keys are used: test commands come through Quake's command line.
If adding input checks, hold every synthetic key down at least 100 ms before
key-up. Do not use xdotool's default short taps.

Evidence includes stdout, stderr, Xvfb logs, command argv JSON, console logs,
PNGs and `result.json` (PASS/FAIL, child PIDs, metrics/error). Exit 0 means the
selected automated contract passed; nonzero means failure. Attach logs and PNGs
to the issue; omit the private `data/` symlink tree to avoid distributing assets.

## Tools on raydelto-PC

Observed 2026-10-14 UTC with `dpkg-query -W` and version commands:

| Tool | Version | Use |
|---|---|---|
| Xvfb / xvfb-run | `2:21.1.12-1ubuntu1.8` | Virtual display (harness owns Xvfb directly) |
| xdotool | `1:3.20160805.1-5build1` | PID-owned window discovery |
| ImageMagick convert/import | `8:6.9.12.98+dfsg1-5.2build2` | Window-only PNG capture via import |
| scrot | `1.10-1build2` | Available alternate manual capture tool |
| ffmpeg | `7:6.1.1-3ubuntu5` | Available for later recordings; not required by this harness |
| Python | `3.12.3` | Standard-library process supervisor |
| GCC | `13.3.0` | Stand-in fixture only; no engine build performed |
| 7-Zip | `23.01` | LZH extraction |

Also required: bash, curl, unzip, sha256sum. Fixture validation requires X11
development headers/library. If missing, ask the owner to install:

```sh
sudo apt-get update && sudo apt-get install -y xvfb xdotool imagemagick scrot ffmpeg curl unzip p7zip-full python3 libx11-dev gcc
```

## QA tooling dry run and engine baselines

`tools/tests/standin.c` creates a clearly labelled X11 window and scripted
Quake-shaped log messages. It validates harness lifecycle/capture and failure
paths, **not** the renderer, demos or engine performance. Build it outside the
tracked source tree:

```sh
mkdir -p verification-results
gcc -Wall -Wextra -Werror tools/tests/standin.c -lX11 -o verification-results/standin
tools/verify.sh launch --timeout 10 verification-results/standin
tools/verify.sh demo --timeout 10 verification-results/standin
tools/verify.sh timedemo --timeout 10 verification-results/standin
tools/verify.sh soak --timeout 10 verification-results/standin
tools/verify.sh launch --timeout 2 verification-results/standin --fixture-exit
tools/verify.sh launch --timeout 2 verification-results/standin --fixture-no-ready
tools/verify.sh launch --timeout 2 verification-results/standin --fixture-no-window
tools/verify.sh launch --timeout 2 verification-results/standin --fixture-error
```

The first four should return 0; the last four must return nonzero. Fixture
timedemos emit **100 frames / 1.0 seconds / 100.0 fps**, intentionally synthetic.
Check that the recorded child PIDs are absent after every run.

| Engine baseline | demo1 | demo2 | demo3 |
|---|---|---|---|
| Frames | TBD | TBD | TBD |
| Seconds | TBD | TBD | TBD |
| FPS | TBD | TBD | TBD |

Engine launch, playback, timing, 60 s stability and screenshots are **TBD until
an engine binary exists**. Record engine commit, build command, compiler,
resolution, flags and hardware when filling these numbers. Compare equivalent
configurations; FPS is machine-dependent and no DOOM gametic count applies.
