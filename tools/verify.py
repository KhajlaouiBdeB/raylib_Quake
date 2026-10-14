#!/usr/bin/env python3
"""Foreground Quake runtime checks; each child is reaped before returning."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
PAK_SHA = '35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af'
FATAL = re.compile(r'^Error:|Host_Error|Sys_Error|couldn.t open|couldn.t load|segmentation fault|AddressSanitizer|runtime error:', re.I | re.M)
METRIC = re.compile(r'(\d+) frames\s+([\d.]+) seconds\s+([\d.]+) fps')


def stop(child):
    # Popen retains ownership and poll() reaps exits before sending any signal.
    if child is not None and child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait(timeout=3)


def interrupted(signum, frame):
    raise RuntimeError(f'interrupted by signal {signum}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['launch', 'demo', 'timedemo', 'soak'])
    parser.add_argument('--headless', action='store_true', help='explicitly skip window/screenshot checks')
    parser.add_argument('--data', type=Path, default=ROOT / 'data')
    parser.add_argument('--output', type=Path, help='new directory for evidence')
    parser.add_argument('--timeout', type=float, default=300, help='per demo/timedemo or startup deadline')
    parser.add_argument('--settle', type=float, default=3, help='launch observation seconds after readiness')
    parser.add_argument('binary')
    parser.add_argument('args', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not math.isfinite(args.timeout) or not math.isfinite(args.settle) or args.timeout <= 0 or args.settle < 0:
        parser.error('timeout must be positive and settle nonnegative')
    binary = Path(args.binary).resolve()
    if not binary.is_file() or not os.access(binary, os.X_OK):
        parser.error(f'not executable: {binary}')
    required = ['Xvfb'] + ([] if args.headless else ['xdotool', 'import'])
    for tool in required:
        if not shutil.which(tool):
            parser.error(f'missing tool: {tool}')
    pak = args.data.resolve() / 'id1/pak0.pak'
    if not pak.is_file() or hashlib.sha256(pak.read_bytes()).hexdigest() != PAK_SHA:
        parser.error('missing or invalid shareware pak0.pak; run tools/fetch_shareware.sh')
    if args.output:
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=False)
    else:
        (ROOT / 'verification-results').mkdir(exist_ok=True)
        output = Path(tempfile.mkdtemp(prefix=args.mode + '-', dir=ROOT / 'verification-results'))
    print(f'Evidence: {output}', flush=True)
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, interrupted)
    xvfb = engine = None
    result = {'mode': args.mode, 'headless': args.headless, 'status': 'FAIL'}
    try:
        with (output / 'xvfb.log').open('wb') as log:
            read_fd, write_fd = os.pipe()
            try:
                xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0',
                                         '1024x768x24', '-nolisten', 'tcp', '-ac'],
                                        pass_fds=(write_fd,), stdout=log, stderr=log)
                result['xvfb_pid'] = xvfb.pid
                os.close(write_fd)
                write_fd = None
                if not select.select([read_fd], [], [], 10)[0]:
                    raise RuntimeError('Xvfb startup timeout')
                display = os.read(read_fd, 32).decode().strip()
                if not display.isdigit():
                    raise RuntimeError('Xvfb failed to allocate display')
            finally:
                os.close(read_fd)
                if write_fd is not None:
                    os.close(write_fd)
        env = os.environ.copy()
        env.update(DISPLAY=':' + display, SDL_AUDIODRIVER='dummy', ALSOFT_DRIVERS='null',
                   ALSA_CONFIG_PATH=str(output / 'alsa.conf'))
        (output / 'alsa.conf').write_text('pcm.!default { type null }\nctl.!default { type hw card 0 }\n')
        basedir = output / 'data'
        id1 = basedir / 'id1'
        id1.mkdir(parents=True)
        (id1 / 'pak0.pak').symlink_to(pak)
        # Keep the stock quake.rc attract loop. PAK files take precedence over
        # loose files in this directory, so a loose override cannot disable it.
        checks = ['demo1', 'demo2', 'demo3'] if args.mode == 'timedemo' else [args.mode]
        for check in checks:
            if args.mode == 'demo':
                commands = []
            elif args.mode == 'timedemo':
                commands = ['+timedemo', check]
            else:
                commands = ['+toggleconsole'] if args.mode == 'launch' else []
            command = [str(binary), *args.args, '-basedir', str(basedir), '-condebug',
                       '-nosound', '-nocdaudio', '+developer', '1', '+echo', 'QA_VERIFY_READY', *commands]
            label = check if args.mode == 'timedemo' else args.mode
            stdout = output / (label + '.stdout.log')
            stderr = output / (label + '.stderr.log')
            console = id1 / 'qconsole.log'
            console.unlink(missing_ok=True)
            (output / (label + '.command.json')).write_text(json.dumps(command, indent=2) + '\n')
            with stdout.open('wb') as out, stderr.open('wb') as err:
                engine = subprocess.Popen(command, cwd=basedir, env=env, stdout=out, stderr=err)
                result.setdefault('engine_pids', []).append(engine.pid)
                start = time.monotonic()
                ready_at = None
                window = None
                progress = 0
                expected = ['demo1', 'demo2', 'demo3', 'demo1']
                while True:
                    text = stdout.read_text(errors='replace') + stderr.read_text(errors='replace')
                    if console.exists():
                        text += console.read_text(errors='replace')
                    if FATAL.search(text):
                        raise RuntimeError(f'{label}: engine error in logs')
                    if engine.poll() is not None:
                        raise RuntimeError(f'{label}: premature engine exit {engine.returncode}')
                    if xvfb.poll() is not None:
                        raise RuntimeError('Xvfb exited during check')
                    now = time.monotonic()
                    if not args.headless and window is None and now - start > args.timeout:
                        raise RuntimeError(f'{label}: visible window timeout')
                    if ready_at is None and ('Quake Initialized' in text or 'QA_VERIFY_READY' in text):
                        ready_at = now
                    if not args.headless and window is None:
                        found = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(engine.pid)],
                                               env=env, capture_output=True, text=True, timeout=3)
                        if found.returncode == 0 and found.stdout.strip():
                            window = found.stdout.splitlines()[0]
                    if args.mode == 'demo':
                        # stdout and qconsole may duplicate messages; use one source.
                        events = console.read_text(errors='replace') if console.exists() else stdout.read_text(errors='replace')
                        demos = re.findall(r'Playing demo from (?:.*[/\\])?(demo[123])\.dem\.', events)
                        progress = 0
                        for demo in demos:
                            if demo == expected[progress]:
                                progress += 1
                                if progress == len(expected):
                                    break
                        complete = progress == len(expected)
                        deadline = args.timeout * max(1, progress)
                    elif args.mode == 'timedemo':
                        match = METRIC.search(text)
                        complete = match is not None
                        deadline = args.timeout
                        if complete:
                            frames, seconds, fps = map(float, match.groups())
                            if min(frames, seconds, fps) <= 0:
                                raise RuntimeError('invalid timedemo metrics')
                            result.setdefault('timedemos', {})[check] = match.group(0)
                    else:
                        duration = 60 if args.mode == 'soak' else args.settle
                        complete = ready_at is not None and now - ready_at >= duration
                        deadline = args.timeout if ready_at is None else now - start + 1
                    if complete and (args.headless or window is not None):
                        if args.mode == 'timedemo':
                            print(f'{check}: {match.group(0)}', flush=True)
                        if window is not None:
                            subprocess.run(['import', '-window', window, str(output / (label + '.png'))],
                                           env=env, check=True, timeout=10)
                        if engine.poll() is not None:
                            raise RuntimeError(f'{label}: exited during capture')
                        final_text = stdout.read_text(errors='replace') + stderr.read_text(errors='replace')
                        if console.exists():
                            final_text += console.read_text(errors='replace')
                        if FATAL.search(final_text):
                            raise RuntimeError(f'{label}: engine error during capture')
                        result.setdefault('elapsed_seconds', {})[label] = round(now - start, 3)
                        if ready_at is not None:
                            result.setdefault('observation_seconds', {})[label] = round(now - ready_at, 3)
                        print(f'PASS: {label}' + (' (headless; screenshot SKIP)' if args.headless else ' (window captured)'), flush=True)
                        break
                    # A missing visible window must still have a finite deadline.
                    if now - start > deadline:
                        raise RuntimeError(f'{label}: timeout (readiness/window/completion missing)')
                    time.sleep(0.1)
                if console.exists():
                    shutil.copyfile(console, output / (label + '.console.log'))
                stop(engine)
                engine = None
        result['status'] = 'PASS'
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        result['error'] = str(error)
        print(f'FAIL: {error}', file=sys.stderr, flush=True)
    finally:
        stop(engine)
        stop(xvfb)
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
