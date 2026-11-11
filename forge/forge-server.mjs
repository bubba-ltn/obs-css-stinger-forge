#!/usr/bin/env node
// Local helper for Transition Forge. Serves the page on http://127.0.0.1:5177, saves picked logos
// into this folder, and runs render-stinger.mjs when you click "Export video".
// Start it with start-forge.bat (or: node forge-server.mjs). Press Ctrl+C to stop.
import http from 'node:http';
import fs from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { spawn, exec } from 'node:child_process';
import os from 'node:os';

const ROOT = path.dirname(fileURLToPath(import.meta.url));
const HTML = 'transition-forge.html';
const argv = process.argv.slice(2);
const PORT = +(argv[argv.indexOf('--port') + 1] || process.env.PORT || 5177);
const HOST = '127.0.0.1';
// Where the OBS plugin looks for presets: <OBS config>/plugin_config/forge-transitions/presets
function obsPresetsDir() {
  const o = argv.indexOf('--presets-dir');
  if (o >= 0 && argv[o + 1]) return path.resolve(argv[o + 1]);
  if (process.env.FORGE_PRESETS_DIR) return path.resolve(process.env.FORGE_PRESETS_DIR);
  const home = os.homedir();
  const cfg = process.platform === 'win32' ? (process.env.APPDATA || path.join(home, 'AppData', 'Roaming'))
    : process.platform === 'darwin' ? path.join(home, 'Library', 'Application Support')
    : (process.env.XDG_CONFIG_HOME || path.join(home, '.config'));
  return path.join(cfg, 'obs-studio', 'plugin_config', 'forge-transitions', 'presets');
}
const PRESETS = obsPresetsDir();
const slug = n => String(n).toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '').slice(0, 50);
const MIME = {
  '.html': 'text/html; charset=utf-8', '.png': 'image/png', '.jpg': 'image/jpeg', '.jpeg': 'image/jpeg',
  '.gif': 'image/gif', '.webp': 'image/webp', '.svg': 'image/svg+xml', '.webm': 'video/webm', '.js': 'text/javascript; charset=utf-8'
};
const IMG_EXT = ['.png', '.jpg', '.jpeg', '.gif', '.webp', '.svg'];
let busy = null;

// SVG logos get a sidecar "<name>.svg.js" that registers the markup, so the page can inline the SVG
// (for stroke-only / filled rows) even when opened from file:// where fetch() is blocked.
async function writeSidecar(name) {
  try {
    const txt = await fs.readFile(path.join(ROOT, name), 'utf8');
    const js = `(window.__svgLogos=window.__svgLogos||{})[${JSON.stringify(name)}]=${JSON.stringify(txt)};\n`;
    await fs.writeFile(path.join(ROOT, name + '.js'), js);
    return true;
  } catch (e) { console.log(`Could not write sidecar for ${name}: ${e.message}`); return false; }
}
async function syncSidecars() {
  const files = await fs.readdir(ROOT).catch(() => []);
  for (const f of files) if (f.toLowerCase().endsWith('.svg')) await writeSidecar(f);
}

const json = (res, code, obj) => { res.writeHead(code, { 'Content-Type': 'application/json' }); res.end(JSON.stringify(obj)); };
const sameOrigin = req => {
  const o = req.headers.origin;
  if (!o) return true;
  try { const h = new URL(o).hostname; return h === '127.0.0.1' || h === 'localhost'; } catch { return false; }
};
const readBody = (req, max) => new Promise((resolve, reject) => {
  const chunks = []; let n = 0;
  req.on('data', d => { n += d.length; if (n > max) { reject(new Error('File is too large')); req.destroy(); return; } chunks.push(d); });
  req.on('end', () => resolve(Buffer.concat(chunks)));
  req.on('error', reject);
});

async function saveLogo(req, res, u) {
  const base = path.basename(u.searchParams.get('name') || 'logo.png').replace(/[^\w .()\-]/g, '_');
  const ext = path.extname(base).toLowerCase();
  if (!IMG_EXT.includes(ext)) return json(res, 400, { error: 'Not an image file' });
  const buf = await readBody(req, 20e6);
  const stem = base.slice(0, base.length - ext.length);
  let name = base, i = 2;
  for (;;) {
    const fp = path.join(ROOT, name);
    let existing = null;
    try { existing = await fs.readFile(fp); } catch { /* does not exist yet */ }
    if (existing === null) { await fs.writeFile(fp, buf); break; }
    if (existing.equals(buf)) break; // identical file already there
    name = `${stem}-${i++}${ext}`;
  }
  if (ext === '.svg') await writeSidecar(name);
  console.log(`Saved logo: ${name}`);
  json(res, 200, { name });
}

async function listPresets() {
  const out = [];
  for (const f of await fs.readdir(PRESETS).catch(() => [])) {
    if (!f.endsWith('.json')) continue;
    try { const j = JSON.parse(await fs.readFile(path.join(PRESETS, f), 'utf8')); out.push({ id: j.id, name: j.name, duration: j.duration }); } catch { /* skip bad file */ }
  }
  return out;
}
async function savePreset(req, res) {
  let body;
  try { body = JSON.parse((await readBody(req, 1e5)).toString() || '{}'); } catch { return json(res, 400, { error: 'Bad request' }); }
  const name = String(body.name || '').trim().slice(0, 40);
  const id = slug(name);
  if (!id) return json(res, 400, { error: 'Give the transition a name (letters or numbers)' });
  const q = String(body.query || '');
  if (!q.startsWith('?')) return json(res, 400, { error: 'Missing settings' });
  const params = new URLSearchParams(q.slice(1));
  const duration = Math.min(10000, Math.max(300, Math.round(+params.get('d') || 1400)));
  for (const k of ['d', 'mode', 'loop', 'render', 'ctl', '_r']) params.delete(k);
  if (!params.get('t')) return json(res, 400, { error: 'Missing transition type' });
  await fs.mkdir(PRESETS, { recursive: true });
  const preset = { id, name, page: pathToFileURL(path.join(ROOT, HTML)).href, query: '?' + params.toString(), duration, saved: new Date().toISOString() };
  const fp = path.join(PRESETS, id + '.json');
  await fs.writeFile(fp + '.tmp', JSON.stringify(preset, null, 2));
  await fs.rename(fp + '.tmp', fp);
  await syncSidecars();
  console.log(`Saved OBS preset "${name}" -> ${fp}`);
  json(res, 200, { id, name, file: fp });
}
async function deletePreset(res, u) {
  const id = slug(u.searchParams.get('id') || '');
  if (!id) return json(res, 400, { error: 'Bad id' });
  await fs.unlink(path.join(PRESETS, id + '.json')).catch(() => {});
  console.log(`Removed OBS preset ${id}`);
  json(res, 200, { ok: true });
}

async function render(req, res) {
  if (busy) return json(res, 409, { error: 'A render is already running.' });
  let body;
  try { body = JSON.parse((await readBody(req, 1e5)).toString() || '{}'); } catch { return json(res, 400, { error: 'Bad request' }); }
  const q = String(body.query || '');
  if (!q.startsWith('?')) return json(res, 400, { error: 'Missing settings' });
  const fps = [24, 30, 60].includes(+body.fps) ? +body.fps : 30;
  let out = path.basename(String(body.out || 'ltn-stinger.webm'));
  if (!/^[\w .()\-]+\.(webm|mov)$/i.test(out)) out = 'ltn-stinger.webm';
  const outPath = path.join(ROOT, out);
  await syncSidecars();
  const url = pathToFileURL(path.join(ROOT, HTML)).href + q;
  const args = [path.join(ROOT, 'render-stinger.mjs'), url, '--fps', String(fps), '--out', outPath];
  const ff = String(body.ffmpeg || '').trim();
  if (ff) args.push('--ffmpeg', ff);

  res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8', 'Cache-Control': 'no-cache' });
  console.log(`Rendering ${out} at ${fps} fps...`);
  const child = spawn(process.execPath, args, { cwd: ROOT, env: process.env });
  busy = child;
  child.stdout.on('data', d => res.write(d));
  child.stderr.on('data', d => res.write(d));
  child.on('error', e => { busy = null; res.write(`\nCould not start the renderer: ${e.message}\n__EXIT__:1\n`); res.end(); });
  child.on('close', code => { busy = null; console.log(`Render finished (exit ${code}).`); res.write(`\n__EXIT__:${code}\n__OUT__:${outPath}\n`); res.end(); });
  res.on('close', () => { if (busy === child) { child.kill(); busy = null; console.log('Render cancelled.'); } });
}

const server = http.createServer(async (req, res) => {
  try {
    const u = new URL(req.url, 'http://x');
    const p = decodeURIComponent(u.pathname);
    if (p.startsWith('/api/')) {
      if (req.headers['x-forge'] !== '1' || !sameOrigin(req)) return json(res, 403, { error: 'Forbidden' });
      if (p === '/api/ping') return json(res, 200, { ok: true, presetsDir: PRESETS, root: ROOT, fileUrl: pathToFileURL(path.join(ROOT, HTML)).href, busy: !!busy });
      if (p === '/api/logo' && req.method === 'POST') return await saveLogo(req, res, u);
      if (p === '/api/presets' && req.method === 'GET') return json(res, 200, { presets: await listPresets() });
      if (p === '/api/preset' && req.method === 'POST') return await savePreset(req, res);
      if (p === '/api/preset' && req.method === 'DELETE') return await deletePreset(res, u);
      if (p === '/api/render' && req.method === 'POST') return await render(req, res);
      return json(res, 404, { error: 'Not found' });
    }
    const rel = p === '/' ? HTML : p.slice(1);
    const full = path.normalize(path.join(ROOT, rel));
    const isSide = /\.svg\.js$/i.test(full);
    const type = isSide ? MIME['.js'] : MIME[path.extname(full).toLowerCase()];
    if (!full.startsWith(ROOT + path.sep) || !type) { res.writeHead(404); return res.end('Not found'); }
    const st = await fs.stat(full).catch(() => null);
    if (!st || !st.isFile()) { res.writeHead(404); return res.end('Not found'); }
    res.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-cache' });
    createReadStream(full).pipe(res);
  } catch (e) {
    if (!res.headersSent) json(res, 500, { error: e.message });
    else res.end();
  }
});

function openBrowser() {
  const url = `http://${HOST}:${PORT}/`;
  const cmd = process.platform === 'win32' ? `start "" "${url}"` : process.platform === 'darwin' ? `open "${url}"` : `xdg-open "${url}"`;
  exec(cmd, () => {});
}
server.on('error', e => {
  if (e.code === 'EADDRINUSE') {
    console.log(`Port ${PORT} is already in use. The Forge helper is probably already running, so opening it.`);
    if (!argv.includes('--no-open')) openBrowser();
    process.exit(0);
  }
  throw e;
});
await syncSidecars();
server.listen(PORT, HOST, () => {
  console.log(`Transition Forge helper running at http://${HOST}:${PORT}/`);
  console.log(`Folder: ${ROOT}`);
  console.log(`OBS presets: ${PRESETS}`);
  console.log('Leave this window open while you use the Forge. Press Ctrl+C to stop.');
  if (!argv.includes('--no-open')) openBrowser();
});
