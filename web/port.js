// Web build page logic (web/index.html): loads the game (pokestadium.js/.wasm, built by tools/build_web.sh), keeps the
// player's own ROM and the saves in the browser, and starts the game on the canvas.
//
// Nothing of the game is on the server: the ROM is picked here, checked (SHA-1) and kept in the browser's storage
// (IndexedDB, through Emscripten's IDBFS at /data), as are the Stadium save and the Transfer Pak files. The game runs
// with /data as its folder (the desktop build's app folder: config, saves/stadium.flash, logs).
"use strict";

const ROM_SHA1 = "ed7bef5a306f88c0a6e96b15e71fee2ef32058f3"; // Pokemon Stadium (USA), big-endian (.z64)
const DATA = "/data";
const ROM_PATH = DATA + "/rom/stadium.z64";
const SAVE_PATH = DATA + "/saves/stadium.flash";
const GB_DIR = DATA + "/gb";
// the game's files; tools/build_web.sh puts their versioned names (name.<hash>.ext) here, so browsers can cache them
const FILES = { js: "pokestadium.js", wasm: "pokestadium.wasm", o2r: "pokestadium.o2r" };

const $ = (id) => document.getElementById(id);
const statusLine = $("status");

function setStatus(text, error = false) {
  statusLine.textContent = text;
  statusLine.classList.toggle("error", error);
}

function unsupported() {
  if (!window.crossOriginIsolated) {
    return "This page must be served cross-origin isolated (COOP/COEP headers) for the game's threads.";
  }
  if (typeof SharedArrayBuffer === "undefined") {
    return "This browser has no SharedArrayBuffer.";
  }
  // memory64 (the game is 64-bit WebAssembly): a module with a 64-bit memory must validate
  const mem64 = new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0, 5, 3, 1, 4, 0]);
  if (!WebAssembly.validate(mem64)) {
    return "This browser does not support 64-bit WebAssembly (memory64). Use a current Chrome, Edge or Firefox.";
  }
  return null;
}

async function sha1Hex(bytes) {
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-1", bytes));
  return Array.from(digest, (b) => b.toString(16).padStart(2, "0")).join("");
}

// .v64 (16-bit byte-swapped) and .n64 (32-bit little-endian) dumps are turned into the .z64 byte order
function toZ64(bytes) {
  const magic = bytes[0];
  const out = new Uint8Array(bytes.length);
  if (magic === 0x37) {
    for (let i = 0; i + 1 < bytes.length; i += 2) { out[i] = bytes[i + 1]; out[i + 1] = bytes[i]; }
    return out;
  }
  if (magic === 0x40) {
    for (let i = 0; i + 3 < bytes.length; i += 4) {
      out[i] = bytes[i + 3]; out[i + 1] = bytes[i + 2]; out[i + 2] = bytes[i + 1]; out[i + 3] = bytes[i];
    }
    return out;
  }
  return bytes;
}

function exists(path) {
  try {
    Module.FS.stat(path);
    return true;
  } catch (e) {
    return false;
  }
}

function mkdirs(path) {
  let cur = "";
  for (const part of path.split("/").filter(Boolean)) {
    cur += "/" + part;
    if (!exists(cur)) Module.FS.mkdir(cur);
  }
}

// IDBFS keeps /data in IndexedDB; syncs are queued so that two never overlap
let syncChain = Promise.resolve();
function sync(load = false) {
  syncChain = syncChain.then(() => new Promise((resolve) => {
    Module.FS.syncfs(load, (err) => {
      if (err) console.warn("saving to browser storage failed", err);
      resolve();
    });
  }));
  return syncChain;
}

function gbFiles() {
  if (!exists(GB_DIR)) return { rom: null, save: null };
  const names = Module.FS.readdir(GB_DIR).filter((n) => n !== "." && n !== "..");
  const rom = names.find((n) => /\.gbc?$/i.test(n)) || null;
  const save = names.find((n) => /\.sav$/i.test(n)) || null;
  return { rom, save };
}

function showGbInfo() {
  const gb = gbFiles();
  $("gbInfo").textContent = gb.rom
    ? `Cartridge: ${gb.rom}${gb.save ? ", save: " + gb.save : " (no save yet)"}`
    : "No cartridge in the Transfer Pak.";
}

function showMenu() {
  const haveRom = exists(ROM_PATH);
  $("pick").hidden = haveRom;
  $("ready").hidden = !haveRom;
  setStatus(haveRom ? "Ready." : "No ROM yet.");
  if (haveRom) showGbInfo();
}

async function readFile(input) {
  const file = input.files[0];
  input.value = "";
  return file ? { name: file.name, bytes: new Uint8Array(await file.arrayBuffer()) } : null;
}

async function pickRom() {
  const f = await readFile($("romFile"));
  if (!f) return;
  setStatus("Checking the ROM…");
  const rom = toZ64(f.bytes);
  const hash = await sha1Hex(rom);
  if (hash !== ROM_SHA1) {
    setStatus(`This is not Pokémon Stadium (USA): SHA-1 ${hash}.`, true);
    return;
  }
  mkdirs(DATA + "/rom");
  Module.FS.writeFile(ROM_PATH, rom);
  await sync();
  showMenu();
}

function download(name, bytes) {
  const url = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
  const a = document.createElement("a");
  a.href = url;
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

function setEnv(name, value) {
  Module.ccall("setenv", "number", ["string", "string", "number"], [name, value, 1]);
}

// Sound: an AudioWorklet plays what the game writes into a ring buffer (src/port/web_audio.cpp). Browsers allow an
// AudioContext to start only from a click or key press: it is made when Play is pressed.
async function startAudio() {
  try {
    const ctx = new AudioContext({ latencyHint: "interactive" });
    await ctx.audioWorklet.addModule("audio-worklet.js");
    const node = new AudioWorkletNode(ctx, "port-output", { numberOfInputs: 0, outputChannelCount: [2] });
    node.connect(ctx.destination);
    Module.portAudio = { ctx, node, rate: ctx.sampleRate };
    document.addEventListener("visibilitychange", () => { document.hidden ? ctx.suspend() : ctx.resume(); });
  } catch (e) {
    console.warn("no sound:", e);
  }
}

// saves: copied to the browser's storage shortly after the game wrote one (src/port/port_platform.c), and when the page
// is hidden or closed
let saveTimer = 0;
const scheduleSaveSync = () => {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(() => sync(), 300);
};

let started = false;
async function play() {
  if (started) return;
  started = true;
  $("overlay").hidden = true;
  await startAudio();
  mkdirs(DATA + "/saves");
  Module.FS.chdir(DATA); // the game's folder (config, saves, logs)
  setEnv("SHIP_HOME", DATA);
  setEnv("PORT_FLASH_FILE", SAVE_PATH);
  setEnv("PORT_RECORD", ""); // no input recording (the desktop build's last_input.rec)
  // in-between frames: the picture at the display's rate instead of the game's 30 frames a second (src/port/interp.cpp)
  setEnv("PORT_INTERPOLATE", $("smooth").checked ? "1" : "0");
  const gb = gbFiles();
  if (gb.rom) {
    setEnv("PORT_GB_ROM", GB_DIR + "/" + gb.rom);
    setEnv("PORT_GB_SAVE", GB_DIR + "/" + (gb.save || gb.rom.replace(/\.gbc?$/i, ".sav")));
  }
  const canvas = $("canvas");
  canvas.focus();
  setInterval(() => sync(), 30000);
  document.addEventListener("visibilitychange", () => { if (document.hidden) sync(); });
  window.addEventListener("pagehide", () => sync());
  showFullscreenButton();
  Module.callMain([ROM_PATH]);
}

// a browser may still hold the sound back until the first input on the page
function resumeAudio() {
  const ctx = Module.portAudio && Module.portAudio.ctx;
  if (ctx && ctx.state === "suspended" && !document.hidden) ctx.resume();
}

// Fullscreen: a button in the corner, shown while the mouse moves (and always on touch screens)
function showFullscreenButton() {
  const button = $("fullscreen");
  if (!document.fullscreenEnabled) return;
  let timer = 0;
  const show = () => {
    button.hidden = !!document.fullscreenElement;
    clearTimeout(timer);
    if (!matchMedia("(pointer: coarse)").matches) timer = setTimeout(() => { button.hidden = true; }, 2000);
  };
  button.addEventListener("click", () => document.documentElement.requestFullscreen().then(() => $("canvas").focus()));
  document.addEventListener("mousemove", show);
  document.addEventListener("fullscreenchange", show);
  show();
}

var Module = {
  canvas: $("canvas"),
  onSaveWritten: () => scheduleSaveSync(),
  locateFile: (path) => (path === "pokestadium.wasm" ? FILES.wasm : path),
  print: (text) => console.log(text),
  printErr: (text) => console.warn(text),
  setStatus: (text) => { if (text && !started) setStatus(text); },
  onAbort: (what) => setStatus("The game stopped: " + what, true),
  preRun: [() => {
    // SDL's JavaScript turns a C pointer into a HEAP32/HEAPF32 index (the sound output); its 64-bit version expects a
    // BigInt, but Emscripten hands JavaScript code pointers as plain numbers. SDL keeps a version defined beforehand.
    Module.SDL3 = Module.SDL3 || {};
    Module.SDL3.CPtrToHeap32Index = (ptr) => Math.floor(Number(ptr) / 4);
    Module.FS.mkdir(DATA);
    Module.FS.mount(Module.IDBFS, {}, DATA);
  }],
  onRuntimeInitialized: async () => {
    setStatus("Reading browser storage…");
    await sync(true);
    // the port's own resources (shaders, fonts): fetched every time, next to the game
    const o2r = await fetch(FILES.o2r).then((r) => r.arrayBuffer());
    Module.FS.writeFile(DATA + "/pokestadium.o2r", new Uint8Array(o2r));
    showMenu();
  },
};

// the "smoother motion" choice is remembered in this browser
try {
  $("smooth").checked = localStorage.getItem("smooth") !== "0";
} catch (e) {}
$("smooth").addEventListener("change", () => {
  try {
    localStorage.setItem("smooth", $("smooth").checked ? "1" : "0");
  } catch (e) {}
});
$("romFile").addEventListener("change", pickRom);
$("play").addEventListener("click", play);
$("forget").addEventListener("click", async () => {
  if (!confirm("Remove the ROM from this browser? Saves are kept.")) return;
  Module.FS.unlink(ROM_PATH);
  await sync();
  showMenu();
});
$("exportSave").addEventListener("click", () => {
  if (!exists(SAVE_PATH)) { setStatus("No save yet.", true); return; }
  download("stadium.flash", Module.FS.readFile(SAVE_PATH));
});
$("saveFile").addEventListener("change", async () => {
  const f = await readFile($("saveFile"));
  if (!f) return;
  if (exists(SAVE_PATH) && !confirm("Replace the save in this browser?")) return;
  mkdirs(DATA + "/saves");
  Module.FS.writeFile(SAVE_PATH, f.bytes);
  await sync();
  setStatus("Save imported.");
});
$("gbRomFile").addEventListener("change", async () => {
  const f = await readFile($("gbRomFile"));
  if (!f) return;
  mkdirs(GB_DIR);
  const old = gbFiles().rom;
  if (old) Module.FS.unlink(GB_DIR + "/" + old);
  Module.FS.writeFile(GB_DIR + "/" + f.name, f.bytes);
  await sync();
  showGbInfo();
});
$("gbSaveFile").addEventListener("change", async () => {
  const f = await readFile($("gbSaveFile"));
  if (!f) return;
  const rom = gbFiles().rom;
  if (!rom) { setStatus("Choose the Game Boy ROM first.", true); return; }
  mkdirs(GB_DIR);
  const old = gbFiles().save;
  if (old) Module.FS.unlink(GB_DIR + "/" + old);
  Module.FS.writeFile(GB_DIR + "/" + rom.replace(/\.gbc?$/i, ".sav"), f.bytes);
  await sync();
  showGbInfo();
});
$("gbClear").addEventListener("click", async () => {
  if (!confirm("Remove the Game Boy cartridge and its save from this browser?")) return;
  for (const n of Object.values(gbFiles())) if (n) Module.FS.unlink(GB_DIR + "/" + n);
  await sync();
  showGbInfo();
});
for (const ev of ["keydown", "mousedown", "touchstart", "gamepadconnected"]) {
  window.addEventListener(ev, resumeAudio, { capture: true });
}

const problem = unsupported();
if (problem) {
  setStatus(problem, true);
} else {
  setStatus("Loading the game…");
  const s = document.createElement("script");
  s.src = FILES.js;
  s.onerror = () => setStatus("Could not load the game.", true);
  document.body.appendChild(s);
}
