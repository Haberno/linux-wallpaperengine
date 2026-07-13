



Review — useful but interacts with our design decisions

- faeb889 multiple audio sources per wallpaper — we deliberately built one-soundtrack-at-a-time rotation (0844fcd/a1693e7); reconcile before porting.
- 03cee44/4bc30e7 web wallpaperPropertyListener/audio-listener injection — check what our CEF rework already injects.
- 1990748 property dict bindings on all field types; d1e26cb/0ccc6c8 JSON coercion robustness — partial overlap with our 7dbce65/a15017e.
- Camera/ortho stack (e890a57, 0f869be, 3b3bec2, c2dc15b, 1b0dcc6, 1a8b7a9, 3c8859d --scaling no-op fix) — our camera diverged heavily via the parallax/3D work; port findings, not patches.
- CLI niceties: 538f146 --offset-x/y, 78086a1 --contrast/--saturation/--border-colour, 41f3182 --format json, 42637d5 --properties-file+SIGUSR1 (we already have the socket prop command).