"""Local web UI: python3 -m stadium_save serve [save] [--port 8642], then open http://127.0.0.1:8642/

The page is editor.html. Stateless JSON API over the library (binds to 127.0.0.1 only). "Edit" bodies are
{"base": <dump>, "edited": <dump>}: the save is rebuilt from base and every record that differs in edited is
committed as the game does (fresh footer in both copies); untouched records keep their bytes. A plain dump is also
accepted (SaveFile.from_dict, exact bytes).
  GET  /api/meta                                  -> species, moves, items, types, modes for the forms
  POST /api/decode  body: raw 128 KiB save        -> {"save": <dump JSON>, "check": [...], "info": "..."}
  POST /api/encode  body: edit                    -> raw save bytes
  POST /api/repair  body: edit                    -> decode of the edit after Save_LoadWithRepair (not written)
  POST /api/mon     body: {species, level, moves: [{move, pp_up}], nickname, ot_name, ot_id, dvs, stat_exp}
                                                  -> {"mon": consistent Pokémon (exp, HP, PP, types), "warnings"}
  GET  /api/initial                               -> decode of the file given on the command line (if any), with
                                                     "gb": the Game Boy saves given with --gb (gbsave.GBSave dicts)
  POST /api/save    body: edit + "gb": [{id, base, edited}]
                                                  -> write the files that changed (only with --write; previous file
                                                     kept as <file>.bak), returns /api/initial + "written"

--write and --exit-with-parent are used by the macOS launcher (tools/launcher), which embeds this page.
"""
import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from . import gamedata, gbsave, pokemon
from .cli import info_text
from .save import CUP_DIVISIONS, GLC_STAGES, MODES, TEAM_CATEGORIES, SaveFile
from .schema import raw_to_json

PAGE_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "editor.html")


def _page():
    with open(PAGE_FILE, encoding="utf-8") as f:
        return f.read()


def _decode(data):
    s = SaveFile(data)
    return {"save": s.to_dict(), "check": [[list(c[0]), c[1], c[2], c[3]] for c in s.check()],
            "info": info_text(s, verbose=True)}


def _encode(d):
    """An edit ({"base", "edited"}) or a plain dump -> save bytes."""
    if "edited" not in d:
        return bytes(SaveFile.from_dict(d).data)
    s = SaveFile.from_dict(d["base"])
    edited = d["edited"]
    # a Pokémon leaving a Lab box: the game leaves the old bytes behind the new count (as its Lab PC writes them)
    for i, (old, new) in enumerate(zip(d["base"]["boxes"], edited["boxes"])):
        if old.get("_footer") == "valid" and len(new["entries"]) < len(old["entries"]) and "_unused_entries" not in new:
            rec = s.record_bytes(s.record("boxes", i))
            unused = rec[0x10 + len(new["entries"]) * 0x37:0x10 + 20 * 0x37]
            if any(unused):
                new["_unused_entries"] = raw_to_json(unused)

    def fn(cur):
        cur.clear()
        cur.update(edited)

    s.edit(fn)
    return bytes(s.data)


def _repair(d):
    s = SaveFile(_encode(d))
    s.repair()
    return _decode(bytes(s.data))


def _gb_entry(i, g):
    with open(g["path"], "rb") as f:
        sav = gbsave.GBSave(f.read())
    info = gbsave.game_info(g.get("rom"), g["path"])
    return {"id": i, "label": g.get("label") or "Game Boy %d" % (i + 1), "path": g["path"],
            "name": os.path.basename(g["path"]), **info, "save": sav.to_dict()}


def _initial(h):
    with open(h.initial, "rb") as f:
        d = _decode(f.read())
    d.update(name=os.path.basename(h.initial), path=h.initial, writable=h.writable)
    d["gb"] = [_gb_entry(i, g) for i, g in enumerate(h.gb) if os.path.exists(g["path"])]
    return d


def _save_all(h, body):
    """Write every file whose contents changed. All edits are built first, so an error writes nothing; a file that
    changed on disk since the page loaded it (the game wrote it) is refused."""
    writes = []
    if "edited" in body:
        with open(h.initial, "rb") as f:
            disk = f.read()
        if bytes(SaveFile.from_dict(body["base"]).data) != disk:
            raise ValueError("%s changed on disk since it was opened. Reload the editor."
                             % os.path.basename(h.initial))
        data = _encode(body)
        if data != disk:
            writes.append((h.initial, data))
    else:  # a plain dump (older pages)
        writes.append((h.initial, _encode(body)))
    for e in body.get("gb", []):
        g = h.gb[int(e["id"])]
        with open(g["path"], "rb") as f:
            disk = f.read()
        if gbsave.GBSave(disk).to_dict() != e["base"]:
            raise ValueError("%s changed on disk since it was opened. Reload the editor." % os.path.basename(g["path"]))
        data = gbsave.apply_edit(disk, e["base"], e["edited"])
        if data != disk:
            writes.append((g["path"], data))
    for path, data in writes:
        write_with_backup(path, data)
    d = _initial(h)
    d["written"] = [os.path.basename(p) for p, _ in writes]
    return d


def _meta():
    moves = [{"id": i, "name": n, "type": gamedata.TYPES.get(gamedata.MOVE_DATA[i][2]),
              "power": gamedata.MOVE_DATA[i][1], "pp": gamedata.MOVE_DATA[i][4]}
             for i, n in enumerate(gamedata.MOVES) if i and i <= 165]
    species = [{"dex": dex, "name": n, "types": [gamedata.TYPES.get(gamedata.BASE[dex][5]),
                                                 gamedata.TYPES.get(gamedata.BASE[dex][6])],
                "moves": [gamedata.MOVES[m] for m in gamedata.BASE[dex][9] if m]}
               for dex, n in enumerate(gamedata.SPECIES) if dex]
    items = [{"id": i, "name": n} for i, n in enumerate(gamedata.ITEMS) if n]
    return {"species": species, "moves": moves, "items": items, "team_categories": list(TEAM_CATEGORIES),
            "modes": {str(k): v for k, v in MODES.items()}, "cup_divisions": list(CUP_DIVISIONS),
            "glc_stages": list(GLC_STAGES)}


def _make_mon(p):
    level = int(p["level"])
    if not 1 <= level <= 100:
        raise ValueError("level must be 1..100")
    dvs = {k: int(v) for k, v in (p.get("dvs") or {"atk": 15, "def": 15, "spd": 15, "spc": 15}).items()}
    se = {k: int(v) for k, v in (p.get("stat_exp") or {k: 0 for k in pokemon.STAT_KEYS}).items()}
    if any(not 0 <= v <= 15 for v in dvs.values()):
        raise ValueError("DVs must be 0..15")
    if any(not 0 <= v <= 65535 for v in se.values()):
        raise ValueError("stat experience must be 0..65535")
    moves = p.get("moves")
    if moves is not None:
        moves = [m for m in moves if m and m.get("move")]
    m = pokemon.make_mon(p["species"], level, moves=None if moves is None else [x["move"] for x in moves],
                         nickname=p.get("nickname") or None, ot_name=p.get("ot_name", "STADIUM"),
                         ot_id=int(p.get("ot_id", 0)), dvs=dvs, stat_exp=se)
    for i, x in enumerate(moves or []):
        if m["moves"][i] and x.get("pp_up"):
            up = max(0, min(3, int(x["pp_up"])))
            m["moves"][i].update(pp_up=up, pp=pokemon.max_pp(pokemon.move_lookup(x["move"]), up))
    mon = pokemon.MON.decode(pokemon.MON.encode(m))  # also checks the names fit the Game Boy charset
    return {"mon": mon, "warnings": pokemon.validate(mon)}


class Handler(BaseHTTPRequestHandler):
    initial = None
    writable = False
    gb = []  # [{"label", "path", "rom"}]: Game Boy saves edited alongside (the launcher's Transfer Paks)

    def _send(self, code, body, ctype):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        self.path = self.path.split("?")[0].split("#")[0]
        if self.path == "/":
            self._send(200, _page().encode(), "text/html; charset=utf-8")
        elif self.path == "/api/meta":
            self._send(200, json.dumps(_meta(), ensure_ascii=False).encode(), "application/json")
        elif self.path == "/api/initial" and self.initial:
            self._send(200, json.dumps(_initial(self), ensure_ascii=False).encode(), "application/json")
        else:
            self._send(404, b"not found", "text/plain")

    def do_POST(self):
        self.path = self.path.split("?")[0]
        body = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        try:
            if self.path == "/api/decode":
                self._send(200, json.dumps(_decode(body), ensure_ascii=False).encode(), "application/json")
            elif self.path == "/api/encode":
                self._send(200, _encode(json.loads(body)), "application/octet-stream")
            elif self.path == "/api/repair":
                self._send(200, json.dumps(_repair(json.loads(body)), ensure_ascii=False).encode(), "application/json")
            elif self.path == "/api/mon":
                self._send(200, json.dumps(_make_mon(json.loads(body)), ensure_ascii=False).encode(),
                           "application/json")
            elif self.path == "/api/save" and self.writable and self.initial:
                self._send(200, json.dumps(_save_all(self, json.loads(body)), ensure_ascii=False).encode(),
                           "application/json")
            else:
                self._send(404, b"not found", "text/plain")
        except Exception as e:  # report editing errors to the page
            self._send(400, ("%s: %s" % (type(e).__name__, e)).encode(), "text/plain")

    def log_message(self, *a):
        pass


def write_with_backup(path, data):
    """Replace path atomically; the previous contents go to path + ".bak"."""
    if os.path.exists(path):
        with open(path, "rb") as f, open(path + ".bak", "wb") as g:
            g.write(f.read())
    tmp = path + ".tmp"
    with open(tmp, "wb") as f:
        f.write(data)
    os.replace(tmp, path)


def _exit_with_parent():
    ppid = os.getppid()
    while True:
        time.sleep(1)
        if os.getppid() != ppid:  # reparented: the launcher is gone
            os._exit(0)


def parse_gb(spec):
    """'LABEL|SAVE[|ROM]' or just 'SAVE'."""
    parts = spec.split("|")
    if len(parts) == 1:
        parts = ["", parts[0]]
    return {"label": parts[0], "path": os.path.abspath(parts[1]),
            "rom": os.path.abspath(parts[2]) if len(parts) > 2 and parts[2] else None}


def serve(port, initial=None, writable=False, exit_with_parent=False, gb=()):
    Handler.initial = os.path.abspath(initial) if initial else None
    Handler.writable = writable
    Handler.gb = [parse_gb(g) for g in gb]
    if exit_with_parent:
        threading.Thread(target=_exit_with_parent, daemon=True).start()
    srv = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print("Stadium save editor on http://127.0.0.1:%d/  (Ctrl+C to stop)" % srv.server_address[1], flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
