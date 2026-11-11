#!/usr/bin/env node
// Renders a Transition Forge transition to a transparent video for OBS's Stinger transition.
// Usage: node render-stinger.mjs "<forge URL>" [--fps 30] [--width 1920] [--height 1080] [--crf 24] [--out ltn-stinger.webm]
// An output name ending in .mov is encoded as ProRes 4444 instead of VP9 alpha WebM.
import { chromium } from 'playwright';
import { spawn, spawnSync } from 'node:child_process';

const args = process.argv.slice(2);
if (!args[0] || args[0].startsWith('--')) {
  console.error('Usage: node render-stinger.mjs "<forge URL>" [--fps 30] [--width 1920] [--height 1080] [--crf 24] [--out file.webm|file.mov]');
  process.exit(1);
}
const flag = (n, d) => { const i = args.indexOf('--' + n); return i > 0 ? args[i + 1] : d; };
const fps = +flag('fps', 30), width = +flag('width', 1920), height = +flag('height', 1080);
const crf = String(flag('crf', 24)), out = flag('out', 'ltn-stinger.webm');
const ffmpegBin = flag('ffmpeg', process.env.FFMPEG_PATH || 'ffmpeg');

const url = new URL(args[0]);
url.searchParams.set('mode', 'full');
url.searchParams.set('render', '1');
if (!url.searchParams.has('t')) url.searchParams.set('t', 'iris');

let stage = 'starting';
const step = s => { stage = s; console.log('> ' + s); };
const withTimeout = (p, ms, what) => Promise.race([p, new Promise((_, rej) => setTimeout(() => rej(new Error('Timed out waiting for ' + what)), ms))]);

const isMov = /\.mov$/i.test(out);
const codec = isMov
  ? ['-c:v', 'prores_ks', '-profile:v', '4444', '-pix_fmt', 'yuva444p10le', '-vendor', 'apl0']
  : ['-c:v', 'libvpx-vp9', '-pix_fmt', 'yuva420p', '-auto-alt-ref', '0', '-b:v', '0', '-crf', crf, '-row-mt', '1'];

step('Checking ffmpeg');
{
  const probe = spawnSync(ffmpegBin, ['-hide_banner', '-encoders'], { encoding: 'utf8' });
  if (probe.error || probe.status !== 0) {
    console.error(`\nCould not run ffmpeg ("${ffmpegBin}"): ${probe.error ? probe.error.message : 'exit code ' + probe.status}`);
    console.error('Install it, then open a NEW terminal window so the PATH updates:');
    console.error('  winget install Gyan.FFmpeg');
    console.error('Or point to it directly: add  --ffmpeg "C:\\path\\to\\ffmpeg.exe"  to the command.');
    process.exit(1);
  }
  const need = /\.mov$/i.test(out) ? 'prores_ks' : 'libvpx-vp9';
  if (!probe.stdout.includes(need)) {
    console.error(`\nThis ffmpeg build has no "${need}" encoder. Install a full build (winget install Gyan.FFmpeg) or use --ffmpeg to point at one.`);
    process.exit(1);
  }
}
step('Starting ffmpeg');
const ff = spawn(ffmpegBin, ['-y', '-f', 'image2pipe', '-vcodec', 'png', '-framerate', String(fps), '-i', '-', ...codec, '-an', out],
  { stdio: ['pipe', 'ignore', 'pipe'] });
let ffErr = '', ffExited = false;
ff.stderr.on('data', d => { ffErr += d; });
ff.stdin.on('error', () => {});
const ffDone = new Promise((res, rej) => {
  ff.on('error', e => { ffExited = true; rej(new Error('Could not start ffmpeg. Is it installed and on your PATH? (' + e.message + ')')); });
  ff.on('close', code => { ffExited = true; code === 0 ? res() : rej(new Error('ffmpeg failed:\n' + ffErr.split('\n').slice(-12).join('\n'))); });
});
ffDone.catch(() => {});

let browser;
try {
  step('Launching Chromium');
  browser = await chromium.launch({
    ...(process.env.CHROME_PATH ? { executablePath: process.env.CHROME_PATH } : {}),
    args: ['--disable-gpu', '--disable-renderer-backgrounding', '--disable-backgrounding-occluded-windows', '--disable-background-timer-throttling'],
    headless: !args.includes('--headed'),
    timeout: 120000
  });
  const page = await browser.newPage({ viewport: { width, height }, deviceScaleFactor: 1 });
  page.setDefaultTimeout(120000);
  page.on('pageerror', e => console.error('  [page error] ' + e.message));
  page.on('console', m => { if (m.type() === 'error' || m.type() === 'warning') console.error('  [page ' + m.type() + '] ' + m.text()); });
  page.on('requestfailed', r => console.error('  [request failed] ' + r.url().slice(0, 100)));

  step('Opening the Forge page');
  await page.goto(url.href, { waitUntil: 'domcontentloaded' });
  step('Waiting for the render hook');
  try {
    await page.waitForFunction(() => window.__forgeRender, null, { timeout: 20000, polling: 100 });
  } catch (e) {
    const info = await page.evaluate(() => ({
      href: location.href.slice(0, 200), title: document.title, bodyClass: document.body.className,
      hasGenerator: !!document.getElementById('app'), hook: typeof window.__forgeRender
    })).catch(() => null);
    console.error('  Page state: ' + JSON.stringify(info));
    if (info && info.bodyClass !== 'player') throw new Error('The page did not switch to player mode. The URL needs a ?t=... parameter, and the HTML must be the latest transition-forge.html.');
    if (info && info.bodyClass === 'player') throw new Error('The page loaded but has no render hook, so this copy of transition-forge.html is an older version. Use the latest one (from E:\\CSS Transitions), open it, and copy the command again.');
    throw new Error('The page never finished loading (see messages above).');
  }

  step('Preparing transition');
  const { d } = await withTimeout(page.evaluate(() => window.__forgeRender.prepare()), 20000, 'the logo image to load (check the logo URL, or clear it)');

  const frames = Math.ceil(d * fps / 1000) + 1;
  step(`Rendering ${frames} frames (${d} ms @ ${fps} fps, ${width}x${height})`);
  for (let i = 0; i < frames; i++) {
    if (ffExited) { await ffDone; throw new Error('ffmpeg stopped early:\n' + ffErr.split('\n').slice(-12).join('\n')); }
    const t = Math.min(i * 1000 / fps, d);
    await page.evaluate(t => { window.__forgeRender.seek(t); void document.body.offsetWidth; }, t);
    const png = await page.screenshot({ type: 'png', omitBackground: true, timeout: 60000 });
    if (!ff.stdin.write(png)) await Promise.race([new Promise(r => ff.stdin.once('drain', r)), ffDone.catch(() => {})]);
    if (i % 5 === 0 || i === frames - 1) process.stdout.write(`\r  frame ${i + 1}/${frames}`);
  }
  console.log();
  step('Encoding');
  ff.stdin.end();
  await ffDone;
  console.log(`Done: ${out}`);
  console.log(`OBS Stinger Transition Point: ${Math.round(d / 2)} ms (frame ${Math.round(fps * d / 2000)})`);
} catch (e) {
  console.error(`\nFailed during: ${stage}\n${e.message}`);
  process.exitCode = 1;
} finally {
  if (browser) await browser.close().catch(() => {});
  try { ff.stdin.end(); } catch {}
}
