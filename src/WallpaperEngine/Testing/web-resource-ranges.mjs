// Opt-in CEF integration regression. Start a wallpaper with
// WPE_CEF_EXTRA=--remote-debugging-port=9229, then pass an existing wallpaper asset:
// node src/WallpaperEngine/Testing/web-resource-ranges.mjs /path/to/asset.webm [port]
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

const asset = process.argv[2];
const expected = fs.readFileSync(asset);
const port = process.argv[3] ?? '9229';
const pages = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
const page = pages.find(page => page.type === 'page');
const socket = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(resolve => { socket.onopen = resolve; });
let sequence = 0;
const pending = new Map();
socket.onmessage = event => {
    const result = JSON.parse(event.data);
    if (pending.has(result.id)) { pending.get(result.id)(result); pending.delete(result.id); }
};
try {
    for (const [range, start, end] of [
        ['bytes=16-47', 16, 48],
        [`bytes=${expected.length - 32}-`, expected.length - 32, expected.length],
        ['bytes=-32', expected.length - 32, expected.length],
    ]) {
        const id = ++sequence;
        const result = new Promise((resolve, reject) => {
            pending.set(id, resolve);
            setTimeout(() => reject(new Error(`Timed out reading ${range}`)), 10000).unref();
        });
        socket.send(JSON.stringify({id, method: 'Runtime.evaluate', params: {
            expression: `(async()=>{const r=await fetch(${JSON.stringify(path.basename(asset))},{headers:{Range:${JSON.stringify(range)}}});const data=await r.arrayBuffer();return {status:r.status,length:data.byteLength,range:r.headers.get('Content-Range'),bytes:[...new Uint8Array(data).subarray(0,64)]}})()`,
            awaitPromise: true, returnByValue: true,
        }}));
        const response = await result;
        assert.equal(response.result?.exceptionDetails, undefined, JSON.stringify(response));
        assert.equal(response.result.result.value.status, 206);
        assert.equal(response.result.result.value.length, end - start, JSON.stringify(response.result.result.value));
        assert.deepEqual(response.result.result.value.bytes, [...expected.subarray(start, end)], range);
    }
    console.log('CEF explicit, open-ended and suffix byte ranges match the original asset');
} finally {
    socket.close();
}
