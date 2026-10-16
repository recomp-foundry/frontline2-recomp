// Run the web build in headless Chrome and record what it does, for machines
// without a display: console output to stdout, a PNG screenshot every few seconds.
//   node web_probe.mjs URL OUT_DIR [SECONDS] [EVERY_SECONDS]
// Needs Google Chrome in /Applications; uses only Node's built-in fetch/WebSocket.
import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const [url, outDir, seconds = '60', every = '5'] = process.argv.slice(2);
if (!url || !outDir) {
  console.error('usage: node web_probe.mjs URL OUT_DIR [SECONDS] [EVERY_SECONDS]');
  process.exit(2);
}
mkdirSync(outDir, { recursive: true });

const port = 9333;
const chrome = spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', [
  '--headless=new', `--remote-debugging-port=${port}`, `--user-data-dir=${join(outDir, 'profile')}`,
  '--enable-unsafe-webgpu', '--enable-features=Vulkan,WebAssemblyMemory64', '--no-first-run',
  '--no-default-browser-check', '--window-size=1280,720', 'about:blank',
], { stdio: ['ignore', 'ignore', 'pipe'] });
chrome.stderr.on('data', () => {});

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let targets;
for (let i = 0; i < 50; ++i) {
  try { targets = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); break; } catch { await sleep(200); }
}
const page = targets.find((t) => t.type === 'page');
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise((r) => ws.addEventListener('open', r, { once: true }));

let nextId = 1;
const pending = new Map();
ws.addEventListener('message', (event) => {
  const msg = JSON.parse(event.data);
  if (msg.id && pending.has(msg.id)) { pending.get(msg.id)(msg); pending.delete(msg.id); return; }
  const t = ((Date.now() - start) / 1000).toFixed(1);
  if (msg.method === 'Runtime.consoleAPICalled') {
    console.log(`[${t}s] ${msg.params.args.map((a) => a.value ?? a.description ?? '').join(' ')}`);
  } else if (msg.method === 'Runtime.exceptionThrown') {
    const d = msg.params.exceptionDetails;
    console.log(`[${t}s] EXCEPTION ${d.text} ${d.exception?.description ?? ''}`);
  } else if (msg.method === 'Inspector.targetCrashed') {
    console.log(`[${t}s] TARGET CRASHED`);
  }
});
const send = (method, params = {}) => new Promise((resolve) => {
  const id = nextId++;
  pending.set(id, resolve);
  ws.send(JSON.stringify({ id, method, params }));
});

const start = Date.now();
await send('Runtime.enable');
await send('Inspector.enable');
await send('Page.enable');
await send('Page.navigate', { url });
let shot = 0;
while ((Date.now() - start) / 1000 < Number(seconds)) {
  await sleep(Number(every) * 1000);
  const res = await Promise.race([send('Page.captureScreenshot', { format: 'png' }), sleep(10000)]);
  if (res?.result?.data) writeFileSync(join(outDir, `shot_${String(shot++).padStart(3, '0')}.png`), Buffer.from(res.result.data, 'base64'));
}
ws.close();
chrome.kill();
process.exit(0);
