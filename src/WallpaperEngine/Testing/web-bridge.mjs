// Run the production browser shim without CEF: node src/WallpaperEngine/Testing/web-bridge.mjs
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(new URL('../WebBrowser/CEF/SubprocessApp.cpp', import.meta.url), 'utf8');
const shim = source.match(/R"JS\(([\s\S]*?)\)JS"/)[1];
const callbacks = new Map();
let nextTimer = 0;
const page = vm.createContext({
    console,
    setInterval(callback) { callbacks.set(++nextTimer, callback); return nextTimer; },
    clearInterval(timer) { callbacks.delete(timer); },
});
vm.runInContext('window = globalThis', page);
vm.runInContext(shim, page);
vm.runInContext(`
    __wpApplyGeneral({fps: 30});
    __wpApplyProps({color: {value: 'red'}});
    __wpApplyProps({color: {value: 'blue'}});
    var received = [];
    wallpaperPropertyListener = {
        applyGeneralProperties(p) { received.push('fps:' + p.fps); },
        applyUserProperties(p) { received.push('color:' + p.color.value); }
    };
`, page);
for (const callback of callbacks.values()) callback();
assert.equal(vm.runInContext('JSON.stringify(received)', page), '["fps:30","color:blue"]');
assert.equal(callbacks.size, 0, 'delivery must stop its timer after the listener is ready');
vm.runInContext(`__wpApplyProps({color: {value: 'green'}})`, page);
assert.equal(vm.runInContext('JSON.stringify(received)', page), '["fps:30","color:blue","color:green"]');
vm.runInContext(`wallpaperPropertyListener = {}; __wpApplyGeneral({fps: 60});`, page);
assert.equal(callbacks.size, 0, 'optional missing callbacks must not leave a polling timer');
console.log('Web bridge late-listener and update delivery passed');
