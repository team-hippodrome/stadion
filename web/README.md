# Web build

The port as WebAssembly with WebGL 2, played in a browser page. The site holds no game data: the page asks for the
player's own ROM of *Pokemon Stadium (USA)*, checks it (SHA-1), and keeps it in the browser (IndexedDB), together with
the Stadium save and the Transfer Pak cartridge and save. Saves can be exported and imported on the page.

```bash
tools/build_web.sh          # build into build_web/, page in build_web/site/
tools/build_web.sh serve    # and serve it on http://localhost:8642
```

Needs the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) (in `~/emsdk`, or `EMSDK` set).
The build runs at low priority with 4 jobs (`PORT_WEB_JOBS`).

## Browser requirements

- 64-bit WebAssembly (memory64): current Chrome, Edge or Firefox. The port tells N64 addresses from host pointers by
  their value (host memory above 4 GB, see `src/port/port_platform.c`), so the program's memory starts at 4 GB; the 4 GB
  below are never touched and not backed by real memory.
- Threads (SharedArrayBuffer): the page must be served with `Cross-Origin-Opener-Policy: same-origin` and
  `Cross-Origin-Embedder-Policy: require-corp` (`web/serve.py` does; so must the production server), and over HTTPS
  (or localhost).
- `.wasm` served as `application/wasm`.

## How it differs from the desktop builds

- `src/port/main.cpp` runs the window loop once per animation frame (`emscripten_set_main_loop`) instead of blocking;
  game threads are Web Workers (`-sPTHREAD_POOL_SIZE`).
- The absolute symbols of `src/port/generated/abs_syms.S` are written as a WebAssembly object by
  `tools/gen_wasm_abs_syms.py` (wasm-ld has neither absolute symbols from assembly nor `--defsym`); `main.cpp` checks
  them at startup.
- The decompiled code calls functions through pointer types that differ from the functions (fragment entries,
  callbacks). WebAssembly traps on such calls, so the build uses `-sEMULATE_FUNCTION_POINTER_CASTS` (`PORT_WEB_FPCAST`).
  The fragment entries have exact-type thunks (`tools/gen_fragment_thunks.py`); the callbacks (geo layouts, particles,
  widgets) are the rest: without the emulation the first geo callback traps.
- Sound: an AudioWorklet (`web/audio-worklet.js`) plays a ring buffer that a thread fills from the SDL audio streams
  (`src/port/web_audio.cpp`); SDL's own browser output runs on the main thread and stutters when it is busy.
- Fragment data: on the N64 a fragment's data starts fresh each time it is loaded; the other builds reset it through
  per-fragment sections (`tools/gen_frag_sections.py`), whose bounds wasm-ld does not provide. The web build takes them
  from the linker map instead (`tools/gen_frag_ranges.py`, packed into `pokestadium.o2r`; without it the area-select
  screen stayed black, its cursor stuck, on the second visit).
- HiDPI: window sizes in points (as on macOS), the game drawn at `devicePixelRatio` (`gInternalResolution`, at most 2).
- Files: the game's files get versioned names (`name.<hash>.ext`) and Brotli copies; the server sends `.br` to browsers
  that take it and lets them keep the files for a year (`tools/deploy_web.sh`).
- Function pointers are small numbers (table indices), so `Util_ConvertAddrToVirtAddr` and `Port_ResolveCode` (geo
  layout callbacks) let numbers below the function table's size through (`Port_IsHostFunction`).
- Audio and JPEG tasks run on a thread of their own (`os_devices.cpp`): the submitting game thread waits for every task,
  and the window thread only runs once per animation frame. Display lists stay on the window thread (WebGL).
- Debugging: `cmake build_web -DPORT_WEB_EXTRA_LINK="-sASSERTIONS=1 -sSAFE_HEAP=1"` checks every memory access.
