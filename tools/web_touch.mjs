// web_touch.mjs - play the browser build with the touch controls, and
// check keyboard and mouse still work on a desktop (QUAK-29).
//
//   PLAYWRIGHT=/path/to/node_modules/playwright/index.mjs \
//     node tools/web_touch.mjs [BUILD_DIR] [OUT_DIR] [--only pixel7|iphone14|desktop]
//
// BUILD_DIR defaults to build-web, OUT_DIR to web-touch. Chromium only:
// Playwright's mobile emulation (Pixel 7 and iPhone 14 viewports, user
// agents, coarse pointer and touch) is not a real phone, so iOS Safari and
// Android Chrome themselves are not covered. Touches are real touch events
// (CDP Input.dispatchTouchEvent), several fingers at once.
//
// Phones, by touch only: Tap to play, tap the demo for the main menu,
// Single Player -> New Game (start.bsp), the console button and the phone
// keyboard for "map e1m1", then move with the stick while firing, look by
// dragging, jump, next weapon, the menu open and closed, save in slot 0
// and load it, the quit question answered N, and portrait. Desktop: no
// touch controls, keys move, pointer lock on a click, the mouse turns, and
// ?touch=1 forces the overlay. Fails on any page or console error. Page
// shots show the controls; canvas shots the game.

import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';

const pw = await import(process.env.PLAYWRIGHT || 'playwright');
const args = process.argv.slice(2);
const flag = (name, def) => {
  const i = args.indexOf(name);
  if (i < 0)
    return def;
  const v = args[i + 1];
  args.splice(i, 2);
  return v;
};
const only = flag('--only', null);
const buildDir = resolve(args[0] || 'build-web');
const outDir = resolve(args[1] || 'web-touch');
mkdirSync(outDir, { recursive: true });

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const port = 8900 + Math.floor(Math.random() * 300);
const server = spawn('python3', ['-m', 'http.server', String(port), '--bind', '127.0.0.1',
  '-d', buildDir], { stdio: 'ignore' });
const base = `http://127.0.0.1:${port}/quake.html`;

const log = [];
const errors = [];
const result = { checks: {} };
let failed = false;
function check(name, ok, detail) {
  result.checks[name] = { ok: !!ok, detail };
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? ': ' + detail : ''}`);
  if (!ok)
    failed = true;
}

// SwiftShader WebGL; audio may only start from a user's gesture, as on a
// phone, so the audio check means something.
const browser = await pw.chromium.launch({ headless: true, args: ['--use-angle=swiftshader',
  '--enable-unsafe-swiftshader', '--autoplay-policy=user-gesture-required'] });
result.chromium = browser.version();

// Headless Chromium now and then fails a capture; try again once.
async function retry(fn) {
  try { return await fn(); } catch { await sleep(700); return fn(); }
}

// A small greyscale copy of the game picture (the 4:3 frame inside the
// canvas element), or of the band [y0, y1) of it. The WebGL canvas cannot
// be read back after it is shown, so this decodes a screenshot in the page.
async function sig(page, y0 = 0, y1 = 1) {
  const png = await retry(() => page.locator('#canvas').screenshot());
  return page.evaluate(async ([b64, y0, y1]) => {
    const bytes = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
    const img = await createImageBitmap(new Blob([bytes], { type: 'image/png' }));
    const s = Math.min(img.width / 4, img.height / 3);
    const w = 4 * s, h = 3 * s, x = (img.width - w) / 2, y = (img.height - h) / 2;
    const c = document.createElement('canvas');
    c.width = 160; c.height = 120;
    const g = c.getContext('2d');
    g.drawImage(img, x, y + h * y0, w, h * (y1 - y0), 0, 0, 160, 120);
    const d = g.getImageData(0, 0, 160, 120).data, out = [];
    for (let i = 0; i < d.length; i += 4)
      out.push((d[i] + d[i + 1] + d[i + 2]) / 3);
    return out;
  }, [png.toString('base64'), y0, y1]);
}
function diff(a, b) {
  let d = 0;
  for (let i = 0; i < a.length; i++)
    d += Math.abs(a[i] - b[i]);
  return d / a.length;
}
// The status bar (ammo, weapon): the bottom tenth of the frame.
const HUD = [0.9, 1];

async function openPage(context, name, query) {
  const page = await context.newPage();
  page.on('console', (m) => {
    const line = `[${name}] ${m.type()}: ${m.text()}`;
    log.push(line);
    if (m.type() === 'error')
      errors.push(line);
  });
  page.on('pageerror', (e) => {
    log.push(`[${name}] pageerror: ${e.name}: ${e.message}`);
    errors.push(`[${name}] ${e.name}: ${e.message}`);
  });
  for (let i = 0; i < 50; i++) {
    try { await fetch(base); break; } catch { await sleep(100); }
  }
  await page.goto(base + (query ? '?' + query : ''));
  await page.waitForFunction(() => document.getElementById('status').textContent === 'Ready.',
    null, { timeout: 120000 });
  return page;
}

// Fingers through CDP: every event carries all fingers still down.
class Fingers {
  constructor(cdp) {
    // A touch that is not handled in 10 s means the page hangs.
    this.cdp = { send: (m, p) => Promise.race([cdp.send(m, p), sleep(10000).then(() => {
      throw new Error('page did not take a touch event in 10 s (hung?)');
    })]) };
    this.down = new Map();
    this.next = 1;
  }
  points() { return [...this.down.entries()].map(([id, p]) => ({ x: p.x, y: p.y, id })); }
  async start(x, y) {
    const id = this.next++;
    this.down.set(id, { x, y });
    await this.cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: this.points() });
    return id;
  }
  async move(id, x, y) {
    this.down.set(id, { x, y });
    await this.cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: this.points() });
  }
  async end(id) {
    this.down.delete(id);
    await this.cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: this.points() });
  }
  // A drag in `steps` moves over `ms`.
  async drag(id, x0, y0, x1, y1, steps, ms) {
    for (let i = 1; i <= steps; i++) {
      await this.move(id, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
      await sleep(ms / steps);
    }
  }
  async tap(x, y, ms = 120) {
    const id = await this.start(x, y);
    await sleep(ms);
    await this.end(id);
  }
}

async function center(page, sel) {
  const b = await page.locator(sel).boundingBox();
  return [b.x + b.width / 2, b.y + b.height / 2];
}

const touchMode = (page) => page.evaluate(() => Module.quakeTouchMode);
const waitMode = (page, m, ms = 15000) => page.waitForFunction((m) => Module.quakeTouchMode === m,
  m, { timeout: ms }).then(() => true, () => false);
const audioState = (page) => page.evaluate(() => (typeof miniaudio !== 'undefined' && miniaudio.devices)
  ? miniaudio.devices.filter(Boolean).map((d) => d.webaudio && d.webaudio.state).join(',') : 'none');

async function phone(deviceName, tag) {
  const dev = pw.devices[deviceName];
  const context = await browser.newContext({ ...dev });
  const page = await openPage(context, tag, 'console=1');
  const cdp = await context.newCDPSession(page);
  const f = new Fingers(cdp);
  const vp = page.viewportSize();
  const shotPage = (n) => retry(() => page.screenshot({ path: join(outDir, `${tag}-${n}.png`) }));
  const shot = (n) => retry(() => page.locator('#canvas').screenshot({ path: join(outDir, `${tag}-${n}.png`) }));
  const tapPic = async (ms) => { await f.tap(vp.width / 2, vp.height / 2, ms); await sleep(500); };
  const tapBtn = async (sel, ms = 150) => { const [x, y] = await center(page, sel); await f.tap(x, y, ms); await sleep(400); };
  const stick = { x: vp.width * 0.18, y: vp.height * 0.7 };
  const r = result[tag] = { device: deviceName, viewport: vp };

  r.coarse = await page.evaluate(() => matchMedia('(pointer: coarse)').matches);
  r.touchHelpShown = await page.locator('p.touchhelp').isVisible();
  r.playLabel = await page.textContent('#play');
  check(`${tag}: coarse pointer, touch help and "Tap to play" on the page`,
    r.coarse && r.touchHelpShown && r.playLabel === 'Tap to play');
  await shotPage('0-start');

  await page.tap('#play');
  await sleep(4000);
  r.audio = await audioState(page);
  check(`${tag}: the tap started the Web Audio context`, /running/.test(r.audio), r.audio);
  check(`${tag}: touch controls shown, game fills the screen`,
    await page.locator('#touch').isVisible() &&
    await page.evaluate(() => document.body.classList.contains('touchgame')));
  r.modeDemo = await touchMode(page);
  check(`${tag}: demo loop shows the menu layout`, r.modeDemo === 0 &&
    await page.locator('#tok').isVisible() && !(await page.locator('#tfire').isVisible()));
  await shotPage('1-demo');
  await tapPic();                       // any key over a demo: main menu
  await sleep(500);
  await shotPage('2-main-menu');
  // Main menu on Single Player: Enter. Then New Game: OK.
  await tapPic();
  await sleep(500);
  await shot('3-single-player');
  await tapBtn('#tok');
  r.startPlay = await waitMode(page, 1, 20000);
  await sleep(1500);
  await shotPage('4-start-map');
  check(`${tag}: New Game by touch reaches the start map with the play layout`,
    r.startPlay && await page.locator('#tfire').isVisible() && !(await page.locator('#tok').isVisible()));
  r.pointerLocked = await page.evaluate(() => !!document.pointerLockElement);
  r.hintShown = await page.locator('#hint').isVisible();
  check(`${tag}: no pointer lock and no mouse hint on a phone`, !r.pointerLocked && !r.hintShown);

  // E1M1 through the console, with the console button and the phone's
  // keyboard (?console=1).
  await tapBtn('#ttop .contoggle');
  r.consoleMode = await waitMode(page, 2, 5000);
  await tapBtn('#tkbd');
  r.keyboardFocused = await page.evaluate(() => document.activeElement.id === 'tinput');
  await page.keyboard.insertText('map e1m1');
  await sleep(300);
  await shotPage('5-console');
  await page.keyboard.press('Enter');
  await sleep(500);
  check(`${tag}: console layout and phone keyboard for "map e1m1"`,
    r.consoleMode && r.keyboardFocused);
  // the console stays down after a map command: close it with ~
  await sleep(2500);
  if (await touchMode(page) === 2)
    await tapBtn('#tcon');
  r.e1m1 = await waitMode(page, 1, 15000);
  await sleep(1500);
  await shotPage('6-e1m1-controls');
  await shot('6-e1m1');
  check(`${tag}: E1M1 running, play layout`, r.e1m1 &&
    log.some((l) => l.startsWith(`[${tag}]`) && /the Slipgate Complex/i.test(l)));

  // Move forward with the stick while FIRE is held (two fingers).
  {
    const before = await sig(page), hud = await sig(page, ...HUD);
    const id = await f.start(stick.x, stick.y);
    await f.drag(id, stick.x, stick.y, stick.x, stick.y - 80, 4, 150);
    const [fx, fy] = await center(page, '#tfire');
    const fire = await f.start(fx, fy);
    await sleep(300);
    await shotPage('7-move-and-fire');
    await sleep(500);
    await f.end(fire);
    await sleep(300);
    await f.end(id);
    await sleep(500);
    const d = diff(before, await sig(page)), dh = diff(hud, await sig(page, ...HUD));
    await shot('7-moved-fired');
    check(`${tag}: stick moves while FIRE is held (multi-touch), ammo count changes`,
      d > 2 && dh > 0.1, `picture ${d.toFixed(2)}, status bar ${dh.toFixed(2)}`);
  }
  // Look around by dragging on the right half: left-right, then up-down.
  {
    let before = await sig(page);
    const lx = vp.width * 0.65, ly = vp.height * 0.4;
    let id = await f.start(lx, ly);
    await f.drag(id, lx, ly, lx + 150, ly, 10, 400);
    await f.end(id);
    await sleep(400);
    const yaw = diff(before, await sig(page));
    await shot('8-looked-right');
    before = await sig(page);
    id = await f.start(lx, ly);
    await f.drag(id, lx, ly, lx, ly + 60, 6, 300);
    await f.end(id);
    await sleep(400);
    const pitch = diff(before, await sig(page));
    await shot('8-looked-down');
    // look level again and back
    id = await f.start(lx, ly + 60);
    await f.drag(id, lx, ly + 60, lx - 150, ly, 10, 400);
    await f.end(id);
    await sleep(300);
    check(`${tag}: drag on the right half turns and looks up/down`, yaw > 2 && pitch > 1,
      `turn ${yaw.toFixed(2)}, pitch ${pitch.toFixed(2)}`);
  }
  // Jump: the view rises and lands.
  {
    const before = await sig(page);
    const [jx, jy] = await center(page, '#tjump');
    await f.tap(jx, jy, 120);
    await sleep(200);
    const up = diff(before, await sig(page));
    await shot('9-jump');
    await sleep(1200);
    const down = diff(before, await sig(page));
    check(`${tag}: JUMP lifts the view and it lands`, up > 1 && down < up,
      `in the air ${up.toFixed(2)}, landed ${down.toFixed(2)}`);
  }
  // Next weapon: the status bar changes (shotgun -> axe).
  {
    const before = await sig(page, ...HUD);
    await tapBtn('#tweapon');
    await sleep(500);
    const d = diff(before, await sig(page, ...HUD));
    await shot('10-next-weapon');
    await tapBtn('#tweapon');          // and back round to the shotgun
    await sleep(300);
    check(`${tag}: WPN changes the weapon`, d > 0.3, `status bar ${d.toFixed(2)}`);
  }
  // Menu open and closed.
  {
    await tapBtn('#ttop button[data-bit="256"]');
    r.menuOpen = await waitMode(page, 0, 5000);
    await shotPage('11-menu-in-game');
    await tapBtn('#tback');
    r.menuClosed = await waitMode(page, 1, 5000);
    check(`${tag}: MENU opens the menu, BACK closes it`, r.menuOpen && r.menuClosed);
  }
  // Save in slot 0 from the menu, walk away, load it.
  {
    await tapBtn('#ttop button[data-bit="256"]');      // main menu, on Single Player
    await tapBtn('#tok');                               // Single Player, on New Game
    await tapBtn('#tdown');
    await tapBtn('#tdown');                             // Save
    await tapBtn('#tok');
    await sleep(300);
    await shotPage('12-save-menu');
    await tapBtn('#tok');                               // slot 0
    r.afterSave = await waitMode(page, 1, 8000);
    await sleep(1500);
    const saved = await sig(page);
    r.saves = await page.evaluate(() => Module.FS.readdir('/quake/id1').filter((n) => /\.sav$/.test(n)));
    await sleep(1500);                                  // copied to IndexedDB once a second
    r.persisted = await page.evaluate(() => Module.FS.readdir('/persist').filter((n) => /\.sav$/.test(n)));
    // walk away and turn
    const id = await f.start(stick.x, stick.y);
    await f.drag(id, stick.x, stick.y, stick.x + 70, stick.y - 40, 3, 100);
    await sleep(900);
    await f.end(id);
    await sleep(500);
    const away = diff(saved, await sig(page));
    await tapBtn('#ttop button[data-bit="256"]');
    await tapBtn('#tok');                               // Single Player (cursor on Save)
    await tapBtn('#tup');                               // Load
    await tapBtn('#tok');
    await sleep(300);
    await shot('13-load-menu');
    await tapBtn('#tok');                               // slot 0
    r.afterLoad = await waitMode(page, 1, 10000);
    await sleep(2500);
    const back = diff(saved, await sig(page));
    await shot('14-loaded');
    check(`${tag}: saved slot 0 by touch (also in IndexedDB) and loaded it back`,
      r.saves.includes('s0.sav') && r.persisted.includes('s0.sav') && r.afterSave && r.afterLoad
      && away > 2 && back < away / 2,
      `saves ${r.saves}, persisted ${r.persisted}, away ${away.toFixed(2)}, after load ${back.toFixed(2)}`);
  }
  // Quit question: N keeps playing.
  {
    await tapBtn('#ttop button[data-bit="256"]');
    for (let i = 0; i < 4; i++)
      await tapBtn('#tdown', 120);                     // Quit
    await tapBtn('#tok');
    await sleep(300);
    await shotPage('15-quit-question');
    await tapBtn('#tno');
    await sleep(500);
    r.stillRunning = await page.evaluate(() => !document.getElementById('overlay').classList.contains('hidden') ? 0 : 1);
    await tapBtn('#tback');
    r.playingAfterQuitNo = await waitMode(page, 1, 5000);
    check(`${tag}: quit question answered N by touch, game goes on`,
      r.stillRunning === 1 && r.playingAfterQuitNo);
  }
  // Portrait: rotate hint, no scrolling.
  await page.setViewportSize({ width: vp.height, height: vp.width });
  await sleep(800);
  r.rotateHint = await page.locator('#rotate').isVisible();
  await shotPage('16-portrait');
  check(`${tag}: portrait shows the rotate hint`, r.rotateHint);
  r.scroll = await page.evaluate(() => [window.scrollX, window.scrollY]);
  check(`${tag}: page never scrolled`, r.scroll[0] === 0 && r.scroll[1] === 0);
  await context.close();
}

async function desktop() {
  const tag = 'desktop';
  const context = await browser.newContext({ viewport: { width: 1100, height: 900 } });
  const page = await openPage(context, tag, 'args=%2Bmap%20e1m1');
  const r = result[tag] = {};
  const shot = (n) => retry(() => page.locator('#canvas').screenshot({ path: join(outDir, `${tag}-${n}.png`) }));
  const key = async (k, ms = 120) => {
    await page.keyboard.down(k); await sleep(ms); await page.keyboard.up(k); await sleep(250);
  };
  check(`${tag}: no touch help, "Click to play"`, !(await page.locator('p.touchhelp').isVisible()) &&
    await page.textContent('#play') === 'Click to play');
  await page.click('#play');
  await page.waitForFunction(() => Module.quakeWantsPointer === 1, null, { timeout: 20000 }).catch(() => {});
  await sleep(1500);
  check(`${tag}: no touch controls, page layout unchanged`, !(await page.locator('#touch').isVisible()) &&
    !(await page.evaluate(() => document.body.classList.contains('touchgame'))));
  await shot('1-e1m1');
  let a = await sig(page);
  await key('ArrowUp', 1000);
  await key('ArrowLeft', 400);
  check(`${tag}: arrows move and turn`, diff(a, await sig(page)) > 2);
  a = await sig(page, ...HUD);
  await key('Control', 300);
  await sleep(300);
  check(`${tag}: Ctrl fires (ammo changes)`, diff(a, await sig(page, ...HUD)) > 0.1);

  r.hint = await page.locator('#hint').isVisible();
  const box = await page.locator('#canvas').boundingBox();
  await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
  await sleep(600);
  r.locked = await page.evaluate(() => document.pointerLockElement === Module.canvas);
  check(`${tag}: mouse hint, pointer lock after a click`, r.hint && r.locked);
  // Headless Chromium reports movementX = 0 for Playwright's moves under
  // pointer lock, so dispatch the motion the browser would (QUAK-25).
  a = await sig(page);
  await page.evaluate(() => {
    for (let i = 0; i < 10; i++)
      document.dispatchEvent(new MouseEvent('mousemove', { movementX: 30, movementY: 0, bubbles: true }));
  });
  await sleep(500);
  r.mouseTurn = diff(a, await sig(page));
  await shot('2-mouse-turned');
  check(`${tag}: mouse motion turns`, r.mouseTurn > 2, `picture ${r.mouseTurn.toFixed(2)}`);
  r.scroll = await page.evaluate(() => [window.scrollX, window.scrollY]);
  check(`${tag}: game keys do not scroll`, r.scroll[0] === 0 && r.scroll[1] === 0);
  await context.close();

  // ?touch=1 on a desktop: overlay on, the keyboard still drives the game.
  const c2 = await browser.newContext({ viewport: { width: 1100, height: 900 } });
  const p2 = await openPage(c2, 'desktop-touch1', 'touch=1');
  await p2.click('#play');
  await sleep(3000);
  r.forcedOverlay = await p2.locator('#touch').isVisible();
  await p2.keyboard.press('Escape');
  await sleep(500);
  await p2.keyboard.press('Enter');
  await sleep(500);
  await p2.keyboard.press('Enter');
  r.forcedPlay = await waitMode(p2, 1, 20000);
  await sleep(1000);
  await p2.screenshot({ path: join(outDir, 'desktop-3-touch1.png') });
  check(`${tag}: ?touch=1 forces the overlay; the keyboard still starts a game`,
    r.forcedOverlay && r.forcedPlay && await p2.locator('#tfire').isVisible());
  await c2.close();
}

try {
  if (!only || only === 'pixel7')
    await phone('Pixel 7 landscape', 'pixel7');
  if (!only || only === 'iphone14')
    await phone('iPhone 14 landscape', 'iphone14');
  if (!only || only === 'desktop')
    await desktop();
  check('no page errors or console errors', errors.length === 0, errors.slice(0, 5).join(' | '));
} catch (e) {
  check('script ran to the end', false, String(e && e.stack || e));
} finally {
  await browser.close();
  server.kill();
  writeFileSync(join(outDir, 'console.log'), log.join('\n') + '\n');
  writeFileSync(join(outDir, 'result.json'), JSON.stringify(result, null, 2) + '\n');
}
process.exit(failed ? 1 : 0);
