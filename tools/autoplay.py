#!/usr/bin/env python3
"""Autoplay: a bot that plays Pokemon Stadium's cups with your team, deciding each turn with Laya.

Laya (https://huggingface.co/convaiinnovations/laya) is an open, local decision model: it gets the battle as a JSON
state plus a typed question (which move or switch) and returns the option it picks with probabilities. Install it once
into a virtual environment and run this script with that Python:

  python3 -m venv build/laya-venv && build/laya-venv/bin/pip install laya
  build/laya-venv/bin/python tools/autoplay.py [--pick 1,2,3] [--wins 8] [--speed 2] [--rom file.z64]

The script starts build/pokestadium with the bot channel open (PORT_BOT, src/port/bot.cpp). You choose everything up
to the first battle yourself: the mode, the cup, the division and your registered team (on the launcher or in the
game, as usual). From the first team-order screen on the bot plays: it picks your Pokemon in the order given by --pick
(1-6: positions in the registered team, as shown on the order screen), chooses moves and switches with Laya in every
battle, and presses through the screens between battles until --wins battles are won (8: a Stadium cup) or you close
the game. After a lost battle it continues the cup (the game asks whether to retry).

A status page (http://127.0.0.1:8777/, tools/autoplay_dashboard.html) shows every game: a live picture (also of hidden
games), the team with the game's own icons (from the ROM), both Pokemon in battle, the options with Laya's
probabilities and every decision. It does not open by itself (a page left open picks up the next run); --browser
opens it. --model picks another Laya checkpoint.
The bot holds controller 1 only when it acts; you can still press buttons yourself.
"""
import argparse, json, math, os, re, socket, struct, subprocess, sys, threading, time, webbrowser, zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCK = "/tmp/pokestadium_bot.sock"  # unix socket paths must be short

# N64 controller bits
A, B, START, L, R, CU, CD, CL, CR = 0x8000, 0x4000, 0x1000, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1
DUP, DDOWN, DLEFT, DRIGHT = 0x0800, 0x0400, 0x0200, 0x0100
STALL_FRAMES = 20000  # no decision, battle or team screen for this long (5.5 minutes of game time): stuck
BATTLE_STALL_FRAMES = 60000  # in a running battle: a trapped Pokemon (Wrap) gets no menu for many turns
MOVE_BUTTON = [CU, CR, CD, CL]          # move slots 1-4 in battle (BattleScene_SelectMoveFromButtons)
PARTY3_BUTTON = [CU, CR, CD]            # party slots with three Pokemon (BattleScene_HandlePartySlotCursorInput)
PARTY6_BUTTON = [CU, CL, B, CR, CD, A]  # party slots with more
ORDER_BUTTON = [B, CL, CU, A, CD, CR]   # team-order screen slots 1-6 (BattleMenu_HandleOrderSelectPanel_PartyInput)

# Laya decides together with a simple estimate of each option (Laya alone is close to chance on unfamiliar decisions:
# it repeated weak moves and switched back and forth). The final choice averages both, as probabilities.
# Laya's share is kept below the estimate's: at 0.5, Laya's vote alone carried a switch the estimate put 114 points
# below attacking (the newcomer took the hit it predicted), and at 0.3 it still switched a 12 HP Articuno to Zapdos
# against Raichu, 15 points below attacking
LAYA_WEIGHT = 0.2
PRIOR_TEMPERATURE = 6.0  # estimate units (about expected damage) per factor e in the prior's odds
SWITCH_COST = 60.0        # a switch gives the opponent a free hit
SWITCH_COOLDOWN = 3       # turns after a switch without another voluntary switch
SWITCH_WHEN_BELOW = 0.0   # switching is considered only when the current matchup (matchup()) is below this

TYPES = {0: "Normal", 1: "Fighting", 2: "Flying", 3: "Poison", 4: "Ground", 5: "Rock", 7: "Bug", 8: "Ghost",
         0x14: "Fire", 0x15: "Water", 0x16: "Grass", 0x17: "Electric", 0x18: "Psychic", 0x19: "Ice", 0x1A: "Dragon"}
# Generation 1 type chart (attacking type -> {defending type: multiplier}); Stadium uses the Game Boy rules
CHART = {
    "Normal": {"Rock": .5, "Ghost": 0},
    "Fire": {"Fire": .5, "Water": .5, "Grass": 2, "Ice": 2, "Bug": 2, "Rock": .5, "Dragon": .5},
    "Water": {"Fire": 2, "Water": .5, "Grass": .5, "Ground": 2, "Rock": 2, "Dragon": .5},
    "Electric": {"Water": 2, "Electric": .5, "Grass": .5, "Ground": 0, "Flying": 2, "Dragon": .5},
    "Grass": {"Fire": .5, "Water": 2, "Grass": .5, "Poison": .5, "Ground": 2, "Flying": .5, "Bug": .5, "Rock": 2,
              "Dragon": .5},
    "Ice": {"Water": .5, "Grass": 2, "Ice": .5, "Ground": 2, "Flying": 2, "Dragon": 2},
    "Fighting": {"Normal": 2, "Ice": 2, "Poison": .5, "Flying": .5, "Psychic": .5, "Bug": .5, "Rock": 2, "Ghost": 0},
    "Poison": {"Grass": 2, "Poison": .5, "Ground": .5, "Bug": 2, "Rock": .5, "Ghost": .5},
    "Ground": {"Fire": 2, "Electric": 2, "Grass": .5, "Poison": 2, "Flying": 0, "Bug": .5, "Rock": 2},
    "Flying": {"Electric": .5, "Grass": 2, "Fighting": 2, "Bug": 2, "Rock": .5},
    "Psychic": {"Fighting": 2, "Poison": 2, "Psychic": .5},
    "Bug": {"Fire": .5, "Grass": 2, "Fighting": .5, "Poison": 2, "Flying": .5, "Psychic": 2, "Ghost": .5},
    "Rock": {"Fire": 2, "Ice": 2, "Fighting": .5, "Ground": .5, "Flying": 2, "Bug": 2},
    "Ghost": {"Normal": 0, "Psychic": 0, "Ghost": 2},
    "Dragon": {"Dragon": 2},
}


def load_tables():
    """Species and move names from the decomp's headers, move data from its table (game/)."""
    species, moves, movedata = {}, {}, {}
    for line in open(os.path.join(ROOT, "game", "include", "pokemon.h")):
        m = re.match(r"#define (\w+)\s+(\d+)\s*$", line)
        if m and 1 <= int(m.group(2)) <= 151:
            species.setdefault(int(m.group(2)), m.group(1).replace("_", " ").title())
    for line in open(os.path.join(ROOT, "game", "include", "moves.h")):
        m = re.match(r"#define (\w+)\s+0x([0-9A-Fa-f]+)", line)
        if m:
            moves.setdefault(int(m.group(2), 16), m.group(1).replace("_", " ").title())
    src = open(os.path.join(ROOT, "game", "src", "gb_data.c")).read()
    table = src[src.index("gMoveDataWithZeroth"):]
    table = table[:table.index("};")]
    for m in re.finditer(r"\{(\d+), (\d+), (\d+), (\d+), (0x[0-9A-F]+|\d+), (\d+)\}", table):
        mid, effect, power, typ, acc, pp = m.groups()
        movedata[int(mid)] = {"power": int(power), "type": TYPES.get(int(typ), "?"), "effect": int(effect),
                              "accuracy": round(int(acc, 0) * 100 / 255), "pp": int(pp)}
    # base speed per species (gSpeciesData: id, hp, attack, defence, speed, special, ...): the critical hit chance
    table = src[src.index("SpeciesData gSpeciesData"):]
    table = table[:table.index("};")]
    base_speed = {int(m.group(1)): int(m.group(5)) for m in
                  re.finditer(r"\{ *(\d+), (\d+), (\d+), (\d+), (\d+), (\d+),", table)}
    return species, moves, movedata, base_speed


SPECIES, MOVES, MOVEDATA, BASE_SPEED = load_tables()
STADIUM_TM_EXTRA = {6: (6, 0x2E), 12: (6, 0x42), 48: (6, 0x42), 49: (6, 0x42), 50: (6, 0x06), 51: (6, 0x06),
                    141: (6, 0x16), 150: (1, 0x7F), 151: (6, 0x7F)}  # Battle_MarkIllegalMoves: extra TM/HM bits


def load_learnsets(gb_rom=None):
    """the moves each species may know (Pokedex number -> move ids), as the game's move check allows them
    (battle_engine Battle_MarkIllegalMoves): starting moves, TM/HM learnsets (gSpeciesData; TM numbers in
    D_80073230) with Stadium's extra bits, and the level-up moves of the species and of what it evolved from, which
    come from a Game Boy Pokemon ROM (its evolutions/moves table, found by Bulbasaur's entry). Without the ROM the
    level-up moves are missing. 98% of the castle trainers' moves are allowed (Raichu's Surf is not)."""
    import struct
    src = re.sub(r"/\*.*?\*/", "", open(os.path.join(ROOT, "game", "src", "pokemon_stats.c")).read(), flags=re.S)
    body = src[src.index("{", src.index("u8 D_80073230[0x420]")):]
    raw = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})\b", body)]
    tm_moves, dex_to_internal = raw[:55], raw[0x37:0x37 + 152]
    gb = open(os.path.join(ROOT, "game", "src", "gb_data.c")).read()
    table = gb[gb.index("SpeciesData gSpeciesData"):]
    table = table[:table.index("};")]
    species = {}
    for line in table.splitlines():
        n = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", line)] if line.strip().startswith("{") else []
        if len(n) >= 22:
            species[n[0]] = {"start": [x for x in n[10:14] if x], "tm": n[15:22]}
    levelup, pre = {}, {}
    if gb_rom and os.path.exists(gb_rom):
        rom = open(gb_rom, "rb").read()
        at = rom.find(bytes([1, 0x10, 0x09, 0, 0x07, 0x49, 0x0D, 0x16, 0x14, 0x4D, 0x1B, 0x4B, 0x22, 0x4A, 0x29, 0x4F,
                             0x30, 0x4C, 0]))
        if at > 0:
            bank = at // 0x4000
            ptr = struct.pack("<H", 0x4000 + at % 0x4000)
            tab = next((i - 2 * 0x98 for i in range(bank * 0x4000, bank * 0x4000 + 0x4000)
                        if rom[i:i + 2] == ptr and i - 2 * 0x98 >= bank * 0x4000), None)

            def entry(internal):  # -> (evolution targets, level-up moves)
                i = bank * 0x4000 + struct.unpack("<H", rom[tab + 2 * internal - 2:tab + 2 * internal])[0] - 0x4000
                evo, moves = [], []
                while rom[i]:
                    evo.append(rom[i + 3] if rom[i] == 2 else rom[i + 2])
                    i += 4 if rom[i] == 2 else 3
                i += 1
                while rom[i]:
                    moves.append(rom[i + 1])
                    i += 2
                return evo, moves
            internal_to_dex = {v: d for d, v in enumerate(dex_to_internal) if d}
            for d in range(1, 152):
                evo, levelup[d] = entry(dex_to_internal[d])
                for t in evo:
                    if t in internal_to_dex:
                        pre[internal_to_dex[t]] = d
    allowed = {}
    for d in range(1, 152):
        moves, e = set(species[d]["start"]) | set(levelup.get(d, ())), d
        while e in pre:  # what it learned before evolving
            e = pre[e]
            moves |= set(species[e]["start"]) | set(levelup.get(e, ()))
        for i in range(55):
            bits = species[d]["tm"][i // 8] | (STADIUM_TM_EXTRA[d][1] if STADIUM_TM_EXTRA.get(d, (9,))[0] == i // 8 else 0)
            if (bits >> (i % 8)) & 1:
                moves.add(tm_moves[i])
        allowed[d] = sorted(m for m in moves if m in MOVEDATA)
    return allowed


ALLOWED = {}  # filled in main() (load_learnsets); empty: the draft judges opponents by the rentals' own moves


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def effectiveness(move_type, defender_types):
    m = 1.0
    for t in set(defender_types):
        m *= CHART.get(move_type, {}).get(t, 1)
    return m


def runtime_mon(hexstr):
    """BattleMonRuntime (game/src/3D140.h): the Pokemon in battle"""
    b = bytes.fromhex(hexstr)
    return {"species": SPECIES.get(b[0x0B], "#%d" % b[0x0B]), "id": b[0x0B], "hp": u16(b, 0x0C), "max_hp": u16(b, 0x28),
            "level": b[0x26], "types": [TYPES.get(b[0x1C], "?"), TYPES.get(b[0x1D], "?")],
            "moves": [(b[0x1F + i], b[0x32 + i] & 0x3F) for i in range(4)], "status": b[0x15],
            "atk": u16(b, 0x2A), "def": u16(b, 0x2C), "spd": u16(b, 0x2E), "spc": u16(b, 0x30),
            "flags": (b[0x4C], b[0x4D]),  # the battle status bytes (unk_4C: 0x40 out of reach, as the accuracy check reads)
            "acc": b[0x60] or 7, "eva": b[0x61] or 7}  # accuracy and evasion stages (unk_5C[4], [5]; 7: none)


def party_mon(hexstr):
    """BattleMon (game/src/game_state.h): a party member"""
    b = bytes.fromhex(hexstr)
    # the species is the first byte (a registered team keeps a flag in the second)
    return {"species": SPECIES.get(b[0], "#%d" % b[0]), "id": b[0], "hp": u16(b, 0x02), "max_hp": u16(b, 0x26),
            "level": b[0x04], "types": [TYPES.get(b[0x06], "?"), TYPES.get(b[0x07], "?")],
            "moves": [(b[0x09 + i], b[0x20 + i] & 0x3F) for i in range(4)], "status": b[0x05],
            "atk": u16(b, 0x28), "def": u16(b, 0x2A), "spd": u16(b, 0x2C), "spc": u16(b, 0x2E)}


def types_text(t):
    return t[0] if t[0] == t[1] else "%s/%s" % (t[0], t[1])


class Game:
    def __init__(self, path=SOCK):
        self.s = socket.socket(socket.AF_UNIX)
        for _ in range(600):
            try:
                self.s.connect(path)
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise RuntimeError("the game did not open %s" % path)
        self.f = self.s.makefile("rw")

    def req(self, line):
        self.f.write(line + "\n")
        self.f.flush()
        reply = self.f.readline()
        if not reply:
            raise EOFError
        return reply

    def state(self):
        return json.loads(self.req("state"))

    def press(self, buttons, frames=4):
        self.req("press %d %d" % (buttons, frames))

    def wait(self, frames):
        self.req("wait %d" % frames)

    def roster(self):
        return self.req("roster").split()

    def thumb(self, path):
        """asks for a small picture of the next frame (written to path by the game, for the status page)"""
        self.req("thumb " + path)


class Status:
    """What the status window shows: one entry per game (served as JSON to the page)"""

    def __init__(self):
        self.lock = threading.Lock()
        self.games = []

    def add_game(self, name):
        g = GameStatus(self, len(self.games), name)
        with self.lock:
            self.games.append(g.data)
        return g

    def json(self):
        with self.lock:
            return json.dumps({"games": self.games})


class GameStatus:
    def __init__(self, status, index, name):
        self.status, self.index = status, index
        self.data = {"name": name, "phase": "starting", "own": None, "opp": None, "party": [], "options": [],
                     "log": [], "wins": 0, "losses": 0, "battles": 0, "done": False, "team": [], "trio": [],
                     "opp_team": [], "opp_seen": {}, "history": [], "thumb": None, "goal": 0}

    def set(self, **kw):
        with self.status.lock:
            self.data.update(kw)

    def get(self, key):
        with self.status.lock:
            return self.data[key]

    def log(self, text):
        with self.status.lock:
            self.data["log"] = ([time.strftime("%H:%M:%S ") + text] + self.data["log"])[:300]
        print("[%s] %s" % (self.data["name"], text), flush=True)

    def trace(self, text):
        """battle detail for the console log only (what each turn looked like, for reviewing lost battles)"""
        print("[%s]     %s" % (self.data["name"], text), flush=True)


PAGE = os.path.join(ROOT, "tools", "autoplay_dashboard.html")
ICONS_AT = 0x820000  # the ROM's Pokemon icons (game/yamls/us/rom.yaml pokemon_icons): 40x40 RGBA5551, one per species


def png_bytes(w, h, rgba):
    rows = b"".join(b"\0" + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows, 1)) + chunk(b"IEND", b""))


def rom_icon(rom, species):
    """the game's own icon of a species (1-151) as PNG, or None"""
    if not rom or not 1 <= species <= 151:
        return None
    with open(rom, "rb") as f:
        f.seek(ICONS_AT + 16 * species)
        off, size = struct.unpack(">II", f.read(8))
        if size != 40 * 40 * 2:
            return None
        f.seek(ICONS_AT + off)
        data = f.read(size)
    out = bytearray()
    for k in range(0, len(data), 2):
        v = (data[k] << 8) | data[k + 1]
        out += bytes((((v >> 11) & 31) * 255 // 31, ((v >> 6) & 31) * 255 // 31, ((v >> 1) & 31) * 255 // 31,
                      255 if v & 1 else 0))
    return png_bytes(40, 40, bytes(out))


def ppm_png(path):
    """a game's thumbnail (PPM from the game, Port_RequestThumb) as PNG, or None"""
    try:
        data = open(path, "rb").read()
    except OSError:
        return None
    parts = data.split(b"\n", 3)
    if len(parts) < 4 or parts[0] != b"P6":
        return None
    w, h = (int(x) for x in parts[1].split())
    rgb = parts[3][:w * h * 3]
    if len(rgb) < w * h * 3:
        return None
    rgba = bytearray(w * h * 4)
    rgba[0::4], rgba[1::4], rgba[2::4] = rgb[0::3], rgb[1::3], rgb[2::3]
    rgba[3::4] = b"\xff" * (w * h)
    return png_bytes(w, h, bytes(rgba))


def serve_status(status, port, rom=None):
    """the status page: / (tools/autoplay_dashboard.html), /state.json, /icon/<species>.png, /thumb/<game>.png"""
    icons = {}

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            path = self.path.split("?")[0]
            body, kind = None, "text/html"
            if path == "/state.json":
                body, kind = status.json().encode(), "application/json"
            elif path.startswith("/icon/") and path.endswith(".png"):
                n = int(path[6:-4]) if path[6:-4].isdigit() else 0
                if n not in icons:
                    icons[n] = rom_icon(rom, n)
                body, kind = icons[n], "image/png"
            elif path.startswith("/thumb/") and path.endswith(".png"):
                i = int(path[7:-4]) if path[7:-4].isdigit() else -1
                with status.lock:
                    p = status.games[i].get("thumb") if 0 <= i < len(status.games) else None
                body, kind = (ppm_png(p) if p else None), "image/png"
            elif path in ("/", "/index.html"):
                body = open(PAGE, "rb").read()
            if body is None:
                self.send_response(404)
                self.end_headers()
                return
            self.send_response(200)
            self.send_header("Content-Type", kind)
            self.send_header("Cache-Control", "no-store" if kind != "image/png" or "/thumb/" in path else "max-age=3600")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, *a):
            pass

    srv = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return "http://127.0.0.1:%d/" % port


class Brain:
    """Turns the battle into a Laya question and returns the chosen option"""

    def __init__(self, model):
        import laya  # in the virtual environment (see the module docstring)
        print("loading Laya (%s)..." % model, flush=True)
        self.agent = laya.load(model)
        self.lock = threading.Lock()  # one model, shared by the games' threads
        print("Laya ready", flush=True)

    def decide(self, status, question, state, options, values):
        """options: [(name, description)], values: the battle-sense estimate per option (expected damage, matchup).
        Laya's probabilities and the estimate (as probabilities) are averaged (LAYA_WEIGHT); -> (index, laya p, final p)"""
        names = [n for n, _ in options]
        if len(options) == 1:  # nothing to decide (Laya needs two options)
            status.set(options=[{"name": names[0], "laya": 1.0, "prior": 1.0, "p": 1.0, "picked": True}])
            return 0, 1.0, 1.0
        criteria = {name: desc for name, desc in options}
        with self.lock:
            result = self.agent.system_one(state, {"q": {"type": "choice", "instructions": question,
                                                         "criteria": criteria}})
        probs = result["answers"]["q"].get("probabilities", {})
        laya_p = [probs.get(n, 0.0) for n in names]
        total = sum(laya_p) or 1.0
        laya_p = [v / total for v in laya_p]
        top = max(values)
        exp = [math.exp((v - top) / PRIOR_TEMPERATURE) for v in values]
        prior_p = [e / sum(exp) for e in exp]
        final = [LAYA_WEIGHT * a + (1 - LAYA_WEIGHT) * b for a, b in zip(laya_p, prior_p)]
        k = max(range(len(names)), key=lambda i: final[i])
        status.set(options=[{"name": n, "laya": laya_p[i], "prior": prior_p[i], "p": final[i], "picked": i == k}
                                 for i, n in enumerate(names)])
        return k, laya_p[k], final[k]


# ---- battle sense: what usually decides a Game Boy-era battle --------------------------------------------------------
# damage from the real stats (level, attack/defense or special by type, same-type bonus, effectiveness, accuracy),
# knock-outs, who moves first, the opponent's threat, status (sleep > paralysis > poison), costly moves (two-turn,
# Hyper Beam's recharge, Explosion), healing when low, and switching only out of a losing matchup.
PHYSICAL = {"Normal", "Fighting", "Flying", "Poison", "Ground", "Rock", "Bug", "Ghost"}
SLEEP, PARALYZE, POISON, HEAL, EXPLODE, CHARGE, FLY, HYPER_BEAM, TRAP, OHKO = 32, 67, 66, 56, 7, 39, 43, 80, 42, 38
DREAM_EATER = 8
MULTI_HIT = {29: 3.0, 44: 2.0, 77: 2.0}  # Fury Attack and the like (2-5 hits, 3 on average), Double Kick, Twineedle
ASLEEP = 0x07  # the status byte's sleep counter (Game Boy encoding)
PRIORITY = {98}  # Quick Attack


HIGH_CRIT = {2, 75, 152, 163}  # Karate Chop, Razor Leaf, Crabhammer, Slash (battle_engine gHighCritMoves)
F2_FOCUS = 0x04                 # Focus Energy (second battle status byte)


def crit_chance(att, mid):
    """the chance of a critical hit as the game rolls it (Battle_CheckCriticalHit, the Stadium rules): from the
    attacker's base speed, four times for Focus Energy, eight times for the high-critical moves (Slash from a fast
    Pokemon almost always)"""
    base = BASE_SPEED.get(att.get("id") or SPECIES_ID_OF.get(att.get("species"), 0), 70)
    t = (base + 76) >> 2
    t = t * 4 + 160 if att.get("flags", (0, 0))[1] & F2_FOCUS else t * 2
    t = t * 4 if mid in HIGH_CRIT else t >> 1
    return max(1, min(255, t)) / 256.0


def crit_factor(att, mid):
    """expected damage over a hit without a critical hit: a critical hit counts the attacker's level twice (about
    1.9 at level 50)"""
    level = att.get("level", 50)
    return 1.0 + crit_chance(att, mid) * ((4 * level / 5 + 2) / (2 * level / 5 + 2) - 1.0)


def damage(att, dfn, mid):
    """Expected damage of one hit (before accuracy) with the Generation 1 formula (average random factor)"""
    d = MOVEDATA.get(mid)
    if d is None or d["power"] == 0:
        return 0.0
    eff = effectiveness(d["type"], dfn["types"])
    if eff == 0:
        return 0.0
    if d["effect"] == DREAM_EATER and not dfn.get("status", 0) & ASLEEP:  # fails on a Pokemon that is awake
        return 0.0
    name = MOVES.get(mid, "")
    if name in ("Seismic Toss", "Night Shade"):
        return float(att["level"])
    if name == "Dragon Rage":
        return 40.0
    if name == "Sonicboom":
        return 20.0
    if name == "Super Fang":
        return dfn["hp"] / 2.0
    if name == "Psywave":
        return att["level"] * 0.75
    if d["effect"] == OHKO:
        return float(dfn["hp"]) if att["spd"] >= dfn["spd"] else 0.0
    a, b = (att["atk"], dfn["def"]) if d["type"] in PHYSICAL else (att["spc"], dfn["spc"])
    if d["effect"] == EXPLODE:  # Explosion and Selfdestruct halve the target's defence (Battle_CalcDamage, effect 7)
        b = max(1, b // 2)
    base = ((2 * att["level"] / 5 + 2) * d["power"] * max(1, a) / max(1, b)) / 50 + 2
    stab = 1.5 if d["type"] in att["types"] else 1.0
    return base * stab * eff * 0.925 * MULTI_HIT.get(d["effect"], 1.0) * crit_factor(att, mid)


STAGE_MULTIPLIER = [None, (25, 100), (28, 100), (33, 100), (40, 100), (50, 100), (66, 100), (1, 1), (15, 10), (2, 1),
                    (25, 10), (3, 1), (35, 10), (4, 1)]  # by stat stage 1-13 (battle_engine gStatStageMultipliers)


def hit_chance(att, dfn, mid):
    """the chance that att's move hits dfn, with att's accuracy stage and dfn's evasion stage as the game counts them
    (func_84370790: Smokescreen and Kinesis made 100% moves miss half the time, unseen by the bot); Swift never misses"""
    d = MOVEDATA.get(mid)
    if d is None or MOVES.get(mid) == "Swift":
        return 1.0
    a = STAGE_MULTIPLIER[min(13, max(1, att.get("acc", 7)))]
    e = STAGE_MULTIPLIER[min(13, max(1, 14 - dfn.get("eva", 7)))]
    return min(255 / 256.0, d["accuracy"] / 100.0 * a[0] / a[1] * e[0] / e[1])


def threat(att, dfn):
    """The share of dfn's current HP att's best move takes (1.0: a knock-out)"""
    best = 0.0
    for mid, pp in att["moves"]:
        if mid and pp > 0:
            best = max(best, damage(att, dfn, mid) * hit_chance(att, dfn, mid))
    return min(1.5, best / max(1, dfn["hp"]))


def faster(a, b):
    return a["spd"] > b["spd"] or (a["spd"] == b["spd"] and False)


# Building and choosing teams counts a miss harder than its average (accuracy ** TEAM_RISK: 70% counts as 59%,
# 85% as 78%): over three Pokemon a miss often costs one, and a Pokemon whose damage hangs on Thunder (the rental
# Zapdos has no sure attack) is less dependable than its average says. Battle decisions use plain accuracy (the
# lookahead plays hits and misses out).
TEAM_RISK = 1.5


def move_value(own, opp, mid, risk=1.0):
    """How much a move does for winning, about 0-130 (100: takes the opponent's remaining HP); risk: TEAM_RISK when
    judging teams"""
    d = MOVEDATA.get(mid)
    if d is None:
        return 0.0
    acc = hit_chance(own, opp, mid) ** risk
    hp_frac = own["hp"] / max(1, own["max_hp"])
    opp_status = opp.get("status", 0)
    if d["power"] == 0 and MOVES.get(mid) not in ("Night Shade",):  # status moves
        e = d["effect"]
        if e in (SLEEP, PARALYZE, POISON) and threat(opp, own) >= 1.0:
            # the next hit knocks us out: no time to profit from it (Drowzee at 22 HP chose Hypnosis)
            return 5.0
        # sleep and paralysis pay off through the hits that follow: worth more with a strong attack (Gengar slept a
        # Porygon again and again that its attacks barely scratched)
        best = max([move_value(own, opp, m, risk) for m, pp in own["moves"]
                    if m and pp > 0 and MOVEDATA.get(m, {}).get("power", 0) > 0] or [0.0])
        if e == SLEEP:
            return (30 + 0.6 * best) * acc if opp_status == 0 else 0.0
        if e == PARALYZE:
            if opp_status != 0 or (d["type"] == "Electric" and "Ground" in opp["types"]):
                return 0.0
            return ((25 if faster(opp, own) else 15) + 0.4 * best) * acc
        if e == POISON:
            return 30 * acc if opp_status == 0 and "Poison" not in opp["types"] else 0.0
        if e == HEAL:
            return 90 * (1 - hp_frac) if hp_frac < 0.5 and threat(opp, own) < 1.0 else 0.0
        return 6.0  # stat changes and the rest
    dmg = damage(own, opp, mid)
    frac = dmg / max(1, opp["hp"])
    if frac <= 0:
        return 0.0
    # the turns it takes to knock the opponent out with this move: hits needed (55% and 95% both need two), two turns
    # per hit for a move that charges first (Sky Attack, Dig, Fly: the opponent acts meanwhile, even before a
    # knock-out), a recharge turn after each Hyper Beam that does not knock out, and the misses
    hits = max(1, math.ceil(1 / frac - 1e-9))
    two_turn = d["effect"] in (CHARGE, FLY)
    turns = 2 * hits if two_turn else (2 * hits - 1 if d["effect"] == HYPER_BEAM else hits)
    v = 100 * acc / turns + 10 * min(1.0, frac) * acc
    if hits == 1 and not two_turn:  # knocks out right now
        v += 20 + (15 if faster(own, opp) or mid in PRIORITY else 0)
    if d["effect"] == EXPLODE:
        v *= 1.0 if hp_frac < 0.35 else 0.15
    # the opponent moves first and knocks us out: only a priority move still lands
    if faster(opp, own) and threat(opp, own) >= 1.0 and mid not in PRIORITY:
        v *= 0.35
    return v


# a faster opponent with Wrap, Bind, Clamp or Fire Spin can keep a Pokemon from acting at all: as bad as being knocked
# out (a Ninetales at 2 HP held Moltres with Fire Spin for 20 turns until it fainted)
TRAP_PENALTY = 100.0


def trapped_by(opp, mon):
    return faster(opp, mon) and any(MOVEDATA.get(mid, {}).get("effect") == TRAP for mid, pp in opp["moves"] if mid and pp > 0)


def offense(mon, opp, risk=1.0):
    return max([move_value(mon, opp, mid, risk) for mid, pp in mon["moves"] if mid and pp > 0] or [0.0])


def matchup(mon, opp, risk=1.0):
    """How well a Pokemon stands against the opponent: its best move's value minus the opponent's threat to it"""
    return (offense(mon, opp, risk) - 100 * min(1.0, threat(opp, mon)) * (1.0 if faster(opp, mon) else 0.7) -
            (TRAP_PENALTY if trapped_by(opp, mon) else 0.0))


def describe_move(own, opp, mid, pp):
    d = MOVEDATA.get(mid, {"power": 0, "type": "?", "accuracy": 100})
    name = MOVES.get(mid, "move %d" % mid)
    if d["power"] == 0:
        what = "%s status move, accuracy %d%%, PP %d" % (d["type"], d["accuracy"], pp)
    else:
        eff = effectiveness(d["type"], opp["types"])
        word = {0: "no effect", 0.25: "barely effective", 0.5: "not very effective", 1: "normal effect",
                2: "super effective", 4: "extremely effective"}.get(eff, "")
        share = 100 * damage(own, opp, mid) / max(1, opp["hp"])
        what = "%s, %s, about %d%% of the opponent's remaining HP%s, accuracy %d%%, PP %d" % (
            d["type"], word, min(share, 999), " (knocks it out)" if share >= 100 else "", d["accuracy"], pp)
    return name, what


STATUS_TEXT = lambda st: ("SLP" if st & 7 else "PSN" if st & 0x08 else "BRN" if st & 0x10 else "FRZ" if st & 0x20 else
                          "PAR" if st & 0x40 else "")


def mon_view(m):
    """a Pokemon for the status page"""
    return {"species": m["species"], "id": m.get("id", 0), "level": m["level"], "hp": m["hp"], "max_hp": m["max_hp"],
            "types": types_text(m["types"]), "status": STATUS_TEXT(m.get("status", 0)), "spd": m.get("spd", 0),
            "moves": [{"name": MOVES.get(mid, "?"), "pp": pp, "type": MOVEDATA.get(mid, {}).get("type", "?"),
                       "power": MOVEDATA.get(mid, {}).get("power", 0), "acc": MOVEDATA.get(mid, {}).get("accuracy", 0)}
                      for mid, pp in m["moves"] if mid]}


def battle_state(own, opp, party, active):
    return {"your_pokemon": {"name": own["species"], "level": own["level"], "hp": "%d/%d" % (own["hp"], own["max_hp"]),
                             "types": types_text(own["types"]), "moves_first": faster(own, opp),
                             "status": "none" if own["status"] == 0 else "affected"},
            "opponent": {"name": opp["species"], "level": opp["level"],
                         "hp_percent": round(100 * opp["hp"] / max(1, opp["max_hp"])), "types": types_text(opp["types"]),
                         "status": "none" if opp["status"] == 0 else "affected",
                         "its_best_hit_takes_percent_of_your_hp": round(100 * threat(opp, own))},
            "your_bench": [{"name": p["species"], "hp": "%d/%d" % (p["hp"], p["max_hp"]), "types": types_text(p["types"])}
                           for i, p in enumerate(party) if i != active and p["hp"] > 0]}


# ---- lookahead: the next turns played out ------------------------------------------------------------------------
# A small Game Boy-rules simulation of the two Pokemon in battle: speed order, accuracy (hit and miss as separate
# outcomes), trapping moves, two-turn moves and Hyper Beam's recharge, sleep, paralysis, poison, healing, draining,
# recoil, Explosion and stat changes. Each of our options is played against each of the opponent's moves for
# LOOKAHEAD_DEPTH turns; the opponent answers as the computer would, with some weight on the worst case for us
# (opp_answer). Damage is the average roll; critical hits, confusion and secondary effects are left out.
LOOKAHEAD_DEPTH = 3  # turns (--lookahead; 0: the one-turn estimate alone)
# gym leaders and the Elite Four decide a castle run (trainers 87% won, leaders 74%; Lt. Surge 17%): their battles
# get a deeper lookahead (--leader-lookahead; up to about 2 s a decision instead of 0.1-0.5; 5 turns took up to 7 s,
# too slow with five games sharing the bot)
LEADER_DEPTH = 4
OPP_BEST = 0.6
# The castle's computer opponent scores each move one turn ahead and takes the best, now and then the second best
# (battle_engine_361050.c: BattleAI_BuildMoveCandidateScores, BattleAI_SelectBestScoredMove): mainly the damage as a
# share of our HP, a knock-out counting most, times its hit chance, and a lesser value for a status move. The rest
# of the weight (1 - AI_SHARE) is on the worse for us of its two best. An even average over its moves had counted Slowbro's
# Amnesia like its Surf: Articuno at 32 HP charged Sky Attack against the turn Slowbro might waste, and fainted to
# the Surf without having hit.
AI_SHARE = 0.7      # of the weight on the move the computer would pick (the rest: the worse of its two best)
AI_SECOND = 0.2     # of that, on its second best


def ai_scores(opp, own, moves):
    """the computer's rough score of each of its moves against own (BattleAI_* above): damage share of own's HP,
    a knock-out 1, times the hit chance; a status move that can still take hold 0.35, a stat boost 0.2"""
    out = []
    for mid in moves:
        d = MOVEDATA.get(mid)
        if mid is None or d is None:
            out.append(0.0)
            continue
        if d["power"] > 0 or MOVES.get(mid) in ("Night Shade", "Seismic Toss"):
            share = min(1.0, damage(opp, own, mid) / max(1.0, own["hp"]))
            if d["effect"] in (CHARGE, FLY, HYPER_BEAM):
                share *= 0.5
            out.append(share * hit_chance(opp, own, mid))
        elif d["effect"] in (SLEEP, PARALYZE, POISON) and not (own["sleep"] or own["para"] or own["psn"]):
            out.append(0.35 * hit_chance(opp, own, mid))
        elif d["effect"] == HEAL and opp["hp"] < opp["max_hp"] / 2:
            out.append(0.5)
        else:
            out.append(0.2 if MOVES.get(mid) in STAT_CHANGE else 0.05)
    return out


def opp_answer(results, scores=None):
    """our value when the opponent answers (results: our value after each of its moves; scores: the computer's
    score of each): AI_SHARE on the move it would pick (its second best now and then), the rest the worst for us"""
    if not scores or len(results) == 1:
        return OPP_BEST * min(results) + (1 - OPP_BEST) * sum(results) / len(results)
    order = sorted(range(len(results)), key=lambda i: -scores[i])
    picked = (1 - AI_SECOND) * results[order[0]] + AI_SECOND * results[order[1]]
    # the caution: the worse of the two it would pick, not of all its moves (Slowbro's Amnesia, which it scores far
    # below the Surf that knocks Articuno out, kept a Sky Attack nearly level with Ice Beam)
    return AI_SHARE * picked + (1 - AI_SHARE) * min(results[order[0]], results[order[1]])
DRAIN, RECOIL = 3, 48
STAT_CHANGE = {"Agility": (0, "spd", 2.0), "Amnesia": (0, "spc", 2.0), "Swords Dance": (0, "atk", 2.0),
               "Barrier": (0, "def", 2.0), "Acid Armor": (0, "def", 2.0), "Reflect": (0, "def", 2.0),
               "Growth": (0, "spc", 1.5), "Meditate": (0, "atk", 1.5), "Sharpen": (0, "atk", 1.5),
               "Harden": (0, "def", 1.5), "Withdraw": (0, "def", 1.5), "Defense Curl": (0, "def", 1.5),
               "Leer": (1, "def", 2 / 3.0), "Tail Whip": (1, "def", 2 / 3.0), "Growl": (1, "atk", 2 / 3.0),
               "Screech": (1, "def", 0.5), "String Shot": (1, "spd", 2 / 3.0)}  # (0: user, 1: target, stat, factor)
PARA, PSN, FRZ = 0x40, 0x08, 0x20


# the battle status bytes of a Pokemon in battle (runtime_mon "flags"; Game Boy layout, checked against 310 traced
# turns): first byte 0x01 Bide, 0x10 charging a two-turn move, 0x20 using a trapping move, 0x40 out of reach (Dig,
# Fly), 0x80 confused; second byte 0x80 seeded (Leech Seed)
F_CHARGING, F_TRAPPING, F_CONFUSED, F2_SEEDED = 0x10, 0x20, 0x80, 0x80
CONFUSE, LEECH_SEED, ACC_DOWN, EVA_UP = 49, 84, 22, 15


def sim_mon(m):
    """a Pokemon for the simulation: its stats, HP and status, plus the turn-to-turn state (from its battle status
    bytes when it is in battle: a two-turn move under way, confusion, Leech Seed)"""
    s = dict(m)
    st = m.get("status", 0)
    f1, f2 = m.get("flags", (0, 0))
    charge = 0
    if f1 & F_CHARGING:  # its next action is the move it is charging (underground with Dig: out of reach)
        charge = next((mid for mid, pp in m["moves"] if MOVEDATA.get(mid, {}).get("effect") in (CHARGE, FLY)), 0)
    s.update(sleep=(st & 7) or (3 if st & FRZ else 0), para=bool(st & PARA), psn=bool(st & PSN), trapped=0,
             charge=charge, recharge=False, conf=bool(f1 & F_CONFUSED), seed=bool(f2 & F2_SEEDED),
             acc=m.get("acc", 7), eva=m.get("eva", 7),
             moves=[(mid, pp) for mid, pp in m["moves"] if mid and pp > 0])
    return s


def sim_pair(own, opp):
    """both Pokemon for the simulation; one using a trapping move keeps the other from acting (the bot chose Surf
    three times against Onix while Bind held Vaporeon), also when it is the slower one (Moltres's Fire Spin held a
    faster Zapdos five turns of seven)"""
    o, x = sim_mon(own), sim_mon(opp)
    if opp.get("flags", (0, 0))[0] & F_TRAPPING:
        o["trapped"] = 2
    if own.get("flags", (0, 0))[0] & F_TRAPPING:
        x["trapped"] = 2
    return o, x


def sim_attack(att, dfn, mid, p, out):
    """att uses move mid on dfn (both copies of their own): appends (probability, att, dfn) outcomes to out"""
    if att["hp"] <= 0:
        out.append((p, att, dfn))
        return
    if att["recharge"]:
        att["recharge"] = False
        out.append((p, att, dfn))
        return
    if att["sleep"] > 0:
        att["sleep"] -= 1
        out.append((p, att, dfn))
        return
    if att["trapped"] > 0:
        att["trapped"] -= 1
        out.append((p, att, dfn))
        return
    if att["para"]:  # fully paralysed a quarter of the time
        out.append((p * 0.25, dict(att), dict(dfn)))
        p *= 0.75
    if att["conf"]:  # confused: hurts itself half the time (a typeless 40-power hit against its own defence)
        hurt = dict(att)
        hurt["hp"] = max(0.0, hurt["hp"] - (((2 * att["level"] / 5 + 2) * 40 * att["atk"] / max(1, att["def"])) / 50 + 2))
        out.append((p * 0.5, hurt, dict(dfn)))
        p *= 0.5
    d = MOVEDATA.get(mid)
    if d is None:
        out.append((p, att, dfn))
        return
    if att["charge"]:
        mid, att["charge"] = att["charge"], 0
        d = MOVEDATA[mid]
    elif d["effect"] in (CHARGE, FLY):
        att["charge"] = mid
        out.append((p, att, dfn))
        return
    name, e = MOVES.get(mid, ""), d["effect"]
    if dfn["charge"] and MOVES.get(dfn["charge"]) in ("Dig", "Fly") and name != "Swift":
        out.append((p, att, dfn))  # underground or in the air: out of reach (Tentacruel's Hydro Pumps missed Dugtrio)
        return
    acc = hit_chance(att, dfn, mid)
    if acc < 1.0:
        out.append((p * (1 - acc), dict(att), dict(dfn)))
        p *= acc
    if d["power"] == 0 and name != "Night Shade":
        if e == SLEEP and not dfn["sleep"] and not dfn["para"] and not dfn["psn"]:
            dfn["sleep"] = 3
        elif e == PARALYZE and not dfn["sleep"] and not dfn["para"] and not dfn["psn"] and not (
                d["type"] == "Electric" and "Ground" in dfn["types"]):
            dfn["para"] = True
            dfn["spd"] = max(1, dfn["spd"] // 4)
        elif e == POISON and not dfn["sleep"] and not dfn["para"] and "Poison" not in dfn["types"]:
            dfn["psn"] = True
        elif e == HEAL:
            if name == "Rest":
                att["hp"], att["sleep"], att["para"], att["psn"] = att["max_hp"], 2, False, False
            else:
                att["hp"] = min(att["max_hp"], att["hp"] + att["max_hp"] // 2)
        elif e == CONFUSE:
            dfn["conf"] = True
        elif e == ACC_DOWN:
            dfn["acc"] = max(1, dfn["acc"] - 1)
        elif e == EVA_UP:
            att["eva"] = min(13, att["eva"] + 1)
        elif e == LEECH_SEED and "Grass" not in dfn["types"]:
            dfn["seed"] = True
        elif name in STAT_CHANGE:
            who, stat, f = STAT_CHANGE[name]
            m = att if who == 0 else dfn
            m[stat] = max(1, int(m[stat] * f))
        out.append((p, att, dfn))
        return
    dmg = min(dfn["hp"], damage(att, dfn, mid))
    dfn["hp"] -= dmg
    if e == DRAIN or e == DREAM_EATER:
        att["hp"] = min(att["max_hp"], att["hp"] + dmg / 2)
    elif e == RECOIL:
        att["hp"] = max(0.0, att["hp"] - dmg / 4)
    elif e == EXPLODE:
        att["hp"] = 0.0
    elif e == HYPER_BEAM and dfn["hp"] > 0:
        att["recharge"] = True
    elif e == TRAP and dfn["hp"] > 0 and dfn["trapped"] <= 0:
        dfn["trapped"] = 2  # it cannot act while the trapper keeps at it (a faster one has acted this turn already)
    out.append((p, att, dfn))


def sim_turn(own, opp, own_mid, opp_mid):
    """one turn with both moving (own_mid None: our side does not act, e.g. it just switched in) -> [(p, own, opp)]"""
    own, opp = dict(own), dict(opp)
    first_own = own_mid is not None and (own_mid in PRIORITY or (opp_mid not in PRIORITY and own["spd"] > opp["spd"]))
    order = [("own", own_mid), ("opp", opp_mid)] if first_own else [("opp", opp_mid), ("own", own_mid)]
    states = [(1.0, own, opp)]
    for who, mid in order:
        nxt = []
        for p, o, x in states:
            if p < 0.05:  # too unlikely to matter
                continue
            if mid is None:
                nxt.append((p, o, x))
            elif who == "own":
                sim_attack(dict(o), dict(x), mid, p, nxt)
            else:
                part = []
                sim_attack(dict(x), dict(o), mid, p, part)
                nxt.extend((q, b, a) for q, a, b in part)
        states = nxt
    for _, o, x in states:  # poison and Leech Seed at the end of the turn
        for m, other in ((o, x), (x, o)):
            if m["psn"] and m["hp"] > 0:
                m["hp"] = max(0.0, m["hp"] - m["max_hp"] / 16.0)
            if m["seed"] and m["hp"] > 0 and other["hp"] > 0:
                drain = min(m["hp"], m["max_hp"] / 16.0)
                m["hp"] -= drain
                other["hp"] = min(other["max_hp"], other["hp"] + drain)
    return states


SOONER = 20.0  # per turn a knock-out comes earlier (a wasted turn cost nothing when the knock-out still came in time)


def worth(m):
    """what a Pokemon's HP is worth for later: a paralysed one is hit first whenever it comes back (and sometimes
    cannot move), a sleeping one loses turns, a poisoned one keeps losing HP; saving such a one by switching gains
    little"""
    return 0.6 if m.get("para") else 0.7 if m.get("sleep") else 0.8 if m.get("psn") else 1.0


def sim_score(own0, opp0, own, opp, left=0):
    """how much better the battle stands for us than at the start (about 100 per knock-out); left: turns of the
    lookahead still unplayed when it ended"""
    v = (100.0 * (opp0["hp"] - opp["hp"]) / max(1, opp0["max_hp"]) -
         100.0 * worth(own0) * (own0["hp"] - own["hp"]) / max(1, own0["max_hp"]))
    if opp["hp"] <= 0:
        return v + 40.0 + SOONER * left
    if own["hp"] <= 0:  # the loss is what it had left (its HP is counted above; a nearly spent one costs little)
        return v - (40.0 + SOONER * left) * worth(own0) * own0["hp"] / max(1, own0["max_hp"])
    for m, sign in ((opp, 1), (own, -1)):  # what a status still does in the turns after
        v += sign * (12.0 * min(m["sleep"], 3) + (15.0 if m["para"] else 0) + (8.0 if m["psn"] else 0) +
                     (25.0 if m["trapped"] else 0) + (10.0 if m["conf"] else 0) + (6.0 if m["seed"] else 0))
    return v + 0.3 * leaf_duel(own, opp)


# How the two on the field fare from a line's end on: the duel (DUEL_TURNS), not the one-turn estimate, which
# could not see that a faster trapper never lets ours act (Lapras chose Ice Beam fifteen times under Rapidash's Fire
# Spin) or that a healer recovers more than ours takes off (Starmie against Chansey's Softboiled)
LEAF_DUELS = {}


def leaf_duel(own, opp):
    key = (own["species"], int(own["hp"]), own["spd"], bool(own["para"]), own["sleep"] > 0, own["atk"], own["spc"],
           opp["species"], int(opp["hp"]), opp["spd"], bool(opp["para"]), opp["sleep"] > 0, opp["atk"], opp["spc"])
    v = LEAF_DUELS.get(key)
    if v is None:
        if len(LEAF_DUELS) > 200000:
            LEAF_DUELS.clear()
        v = LEAF_DUELS[key] = duel(own, opp)
    return v


def opp_moves(opp, own=None):
    """the moves the opponent may use against own: all of them, but a two-turn move under way, and a faster trapper
    traps (Rapidash used Fire Spin every turn it could, fifteen in a row: the bot had counted on the turns it might
    not, and kept Lapras in)"""
    if opp["charge"]:
        return [opp["charge"]]
    if own is not None and opp["spd"] > own["spd"]:
        trap = next((mid for mid, pp in opp["moves"] if MOVEDATA.get(mid, {}).get("effect") == TRAP), None)
        if trap:
            return [trap]
    return [mid for mid, _ in opp["moves"]] or [None]


def sim_key(m):
    return (int(m["hp"]), m["sleep"], m["para"], m["psn"], m["trapped"], m["charge"], m["recharge"], m["conf"], m["seed"],
            m["acc"], m["eva"], m["atk"], m["def"], m["spd"], m["spc"])


# The three-on-three around a decision: when the opponent's Pokemon faints, its best remaining one comes in (the
# game shows the bot their three: Port_BotBattleOpponents); and every line of play is scored by the change in the
# team outlook too: for each opponent still able to fight, how our best remaining Pokemon fares against it (the duel).
# Losing the only answer to a Pokemon still to come (Nidoking against Raichu) costs more than its HP.
OUTLOOK_WEIGHT = 0.5


def outlook(duels, ours, theirs, total=None):
    """the average over their Pokemon of our best duel against each (species names); total: how many they had at
    the decision, the ones knocked out since counting as won (as an average over the remaining ones, knocking out a
    Pokemon we beat lowered it: Jynx, our last, put a Machamp at 3 HP to sleep eight times rather than knock it out
    and face the two fresh ones after it)"""
    total = max(total or len(theirs), len(theirs))
    if not total:
        return 130.0
    alive = sum(max(duels.get((m, o), 0.0) for m in ours) if ours else -130.0 for o in theirs)
    return (alive + 130.0 * (total - len(theirs))) / total


def plan(cache, ours, theirs):
    """sets up the outlook for a decision: the duels of our Pokemon with theirs, and the outlook now"""
    duels = {(m["species"], o["species"]): duel(m, o) for m in ours for o in theirs}
    cache["plan"] = {"duels": duels, "root": outlook(duels, [m["species"] for m in ours], [o["species"] for o in theirs]),
                     "total": len(theirs)}


def leaf_outlook(cache, own, opp, bench, opp_bench):
    """OUTLOOK_WEIGHT times the change of the team outlook from the decision to this line's end"""
    p = cache.get("plan")
    if not p:
        return 0.0
    ours = [m["species"] for m in (own,) + tuple(bench) if m["hp"] > 0]
    theirs = [m["species"] for m in (opp,) + tuple(opp_bench) if m["hp"] > 0]
    return OUTLOOK_WEIGHT * (outlook(p["duels"], ours, theirs, p["total"]) - p["root"])


def sim_bench_in(opp, bench, depth, cache, opp_bench=()):
    """after our Pokemon faints, the best of the bench comes in without taking a hit and plays the turns left"""
    best = None
    for i, b in enumerate(bench):
        rest = bench[:i] + bench[i + 1:]
        v = (max(sim_value(b, opp, b, opp, depth, mid, cache, rest, opp_bench) for mid, _ in b["moves"])
             if b["moves"] else 0.0)
        best = v if best is None else max(best, v)
    return best or 0.0


def sim_opp_in(own, opp_bench, depth, cache, bench=()):
    """after the opponent's Pokemon faints, its best remaining one against ours comes in and plays the turns left"""
    worst = None
    for i, n in enumerate(opp_bench):
        rest = opp_bench[:i] + opp_bench[i + 1:]
        v = (max(sim_value(own, n, own, n, depth, mid, cache, bench, rest) for mid, _ in own["moves"])
             if own["moves"] else 0.0)
        worst = v if worst is None else min(worst, v)
    return worst or 0.0


def sim_value(own0, opp0, own, opp, depth, own_mid, cache=None, bench=(), opp_bench=()):
    """our expected standing after playing own_mid now and our best moves after it, for depth turns (cache: the
    positions already valued in this decision; misses and the like lead to the same ones again). bench: the rest of
    our team (sim_mon), which comes in when ours faints; opp_bench: the opponent's"""
    if cache is None:
        cache = {}
    key = (sim_key(own), sim_key(opp), depth, own_mid, tuple(b["species"] for b in bench),
           tuple(b["species"] for b in opp_bench))
    if key in cache:
        return cache[key]
    results = []
    ks = opp_moves(opp, own)
    for k in ks:
        total = 0.0
        for p, o, x in sim_turn(own, opp, own_mid, k):
            if o["hp"] <= 0 and x["hp"] > 0 and depth > 1 and bench:
                # a faint brings the next one in free (no hit on the way in, unlike a switch)
                total += p * (sim_score(own0, opp0, o, x, depth - 1) + sim_bench_in(x, bench, depth - 1, cache, opp_bench))
            elif x["hp"] <= 0 and o["hp"] > 0 and depth > 1 and opp_bench:
                total += p * (sim_score(own0, opp0, o, x, depth - 1) + sim_opp_in(o, opp_bench, depth - 1, cache, bench))
            elif depth <= 1 or o["hp"] <= 0 or x["hp"] <= 0:
                total += p * (sim_score(own0, opp0, o, x, depth - 1) + leaf_outlook(cache, o, x, bench, opp_bench))
            else:
                nxt = [o["charge"]] if o["charge"] else [mid for mid, _ in o["moves"]]
                total += p * max(sim_value(own0, opp0, o, x, depth - 1, mid, cache, bench, opp_bench) for mid in nxt)
        results.append(total)
    cache[key] = opp_answer(results, ai_scores(opp, own, ks))
    return cache[key]


def lookahead_values(own, opp, actions, bench, depth=None, opp_bench=()):
    """the lookahead's value of each action: ("move", mid) or ("switch", party Pokemon); bench: our other Pokemon
    still able to fight; opp_bench: the opponent's"""
    depth = depth or LOOKAHEAD_DEPTH
    o, x = sim_pair(own, opp)
    rest = tuple(sim_mon(b) for b in bench)
    their = tuple(sim_mon(b) for b in opp_bench)
    values, cache = [], {}
    plan(cache, [own] + list(bench), [opp] + list(opp_bench))
    for kind, what in actions:
        if kind == "move":
            values.append(sim_value(o, x, o, x, depth, what, cache, rest, their))
        else:  # switching: the newcomer takes the opponent's move this turn, then plays on; what the current one
            # keeps (its HP, out of harm's way) is not lost
            n = sim_pair(what, opp)[0]  # a trap under way catches the newcomer too, unless it is faster
            others = tuple(b for b in rest if b["species"] != what["species"]) + (o,)
            results, sub = [], {"plan": cache["plan"]}
            d = min(depth, 2 if depth <= 3 else 3)  # a switch is looked at less far: there are many of them
            ks = opp_moves(x, n)
            for k in ks:
                total = 0.0
                for p, a, b in sim_turn(n, x, None, k):
                    if a["hp"] <= 0 and b["hp"] > 0 and d > 1:
                        total += p * (sim_score(n, x, a, b, d - 1 + (depth - d)) + sim_bench_in(b, others, d - 1, sub, their))
                    elif d <= 1 or a["hp"] <= 0 or b["hp"] <= 0:
                        total += p * (sim_score(n, x, a, b, d - 1 + (depth - d)) + leaf_outlook(sub, a, b, others, their))
                    else:
                        total += p * max(sim_value(n, x, a, b, d - 1, mid, sub, others, their) for mid, _ in a["moves"])
                results.append(total)
            values.append(opp_answer(results, ai_scores(x, n, ks)))
    return values


# The searches run in worker processes: one Python process thinks for one game at a time (the interpreter lock), so
# five games in leader battles waited for each other's searches on one core (five decisions at once: 27 s, in the
# workers 9 s). A decision stays one search: its actions share what the first one worked out (split up, 16 s).
SEARCH_POOL = None


def search_init(depth, leader_depth):
    global LOOKAHEAD_DEPTH, LEADER_DEPTH
    LOOKAHEAD_DEPTH, LEADER_DEPTH = depth, leader_depth


def searches(calls):
    """[(function, args)] -> their results, in the worker processes when there are some"""
    if SEARCH_POOL is None:
        return [f(*a) for f, a in calls]
    return [job.result() for job in [SEARCH_POOL.submit(f, *a) for f, a in calls]]


def lookahead_sendout(cand, opp, bench=(), depth=None, opp_bench=()):
    """how well a Pokemon sent in after a knock-out does (it acts this turn: no free hit); bench: the others left"""
    n, x = sim_pair(cand, opp)
    rest = tuple(sim_mon(b) for b in bench)
    their = tuple(sim_mon(b) for b in opp_bench)
    cache = {}
    plan(cache, [cand] + list(bench), [opp] + list(opp_bench))
    return max(sim_value(n, x, n, x, depth or LOOKAHEAD_DEPTH, mid, cache, rest, their) for mid, _ in n["moves"])


def play_turn(game, brain, status, choice):
    LAST_SWITCH = status.data.setdefault("last_switch", {})
    own, opp = runtime_mon(choice["own"]), runtime_mon(choice["opp"])
    party = [party_mon(p) for p in choice["party"]]
    active = choice["active"]
    # the opponent's three (Port_BotBattleOpponents): the ones still able to fight besides the active one
    opp_party = [party_mon(p) for p in choice.get("opp_party", [])]
    opp_bench = [m for i, m in enumerate(opp_party) if i != choice.get("opp_active", -1) and m["hp"] > 0
                 and not m["species"].startswith("#")]
    seen = status.data.setdefault("opp_seen", {})  # the opponent's Pokemon met in this battle: HP share left
    seen[str(opp["id"])] = round(opp["hp"] / max(1, opp["max_hp"]), 3)
    for m in opp_party:  # their three, with the HP the game reports
        seen[str(m["id"])] = round(m["hp"] / max(1, m["max_hp"]), 3)
    status.set(own=mon_view(own), opp=mon_view(opp), opp_seen=dict(seen),
               party=[dict(mon_view(p), active=i == active) for i, p in enumerate(party)])
    state = battle_state(own, opp, party, active)
    slot_buttons = PARTY3_BUTTON if len(party) <= 3 else PARTY6_BUTTON
    bench = [i for i, p in enumerate(party) if i != active and p["hp"] > 0]

    if choice["forced"]:
        if not bench:
            return
        opts = [(party[i]["species"], "%s, %s, %d/%d HP" % (party[i]["species"], types_text(party[i]["types"]),
                                                             party[i]["hp"], party[i]["max_hp"])) for i in bench]
        depth = LEADER_DEPTH if status.data.get("leader") else LOOKAHEAD_DEPTH
        values = (searches([(lookahead_sendout, (party[i], opp, [party[j] for j in bench if j != i], depth, opp_bench))
                            for i in bench])
                  if LOOKAHEAD_DEPTH else
                  [matchup(party[i], opp) for i in bench])
        status.trace("send out against %s %d/%d spd %d (moves %s) | %s" % (
            opp["species"], opp["hp"], opp["max_hp"], opp["spd"],
            ", ".join(MOVES.get(mid, "?") for mid, _ in opp["moves"] if mid),
            ", ".join("%s %d/%d %.0f" % (party[i]["species"], party[i]["hp"], party[i]["max_hp"], v)
                      for i, v in zip(bench, values))))
        k, lp, fp = brain.decide(status, "Your Pokemon fainted. Which Pokemon should you send out next to beat %s?" %
                                 opp["species"], state, opts, values)
        status.log("send out %s (Laya %.0f%%, final %.0f%%)" % (party[bench[k]]["species"], lp * 100, fp * 100))
        game.press(slot_buttons[bench[k]])
        return

    opts, actions, values = [], [], []
    for i, (mid, pp) in enumerate(own["moves"]):
        if mid and pp > 0:
            name, what = describe_move(own, opp, mid, pp)
            opts.append((name, "use %s: %s" % (name, what)))
            actions.append(("move", i))
            values.append(move_value(own, opp, mid))
    here = matchup(own, opp)
    # voluntary switches only from a losing matchup (the lookahead weighs every switch itself), and not again soon
    # after one (no switching back and forth)
    may_switch = (LOOKAHEAD_DEPTH or here < SWITCH_WHEN_BELOW) and LAST_SWITCH.get("age", 99) >= SWITCH_COOLDOWN
    for i in (bench if may_switch else []):
        if party[i]["species"] == LAST_SWITCH.get("from"):
            continue  # never straight back to the one just switched out
        name = "switch to " + party[i]["species"]
        opts.append((name, "%s: %s, %d/%d HP" % (name, types_text(party[i]["types"]), party[i]["hp"], party[i]["max_hp"])))
        actions.append(("switch", i))
        # the newcomer takes the opponent's next hit before it acts
        incoming = 100 * min(1.0, threat(opp, party[i]))
        # escaping a losing matchup is worth what the current Pokemon has left (at 3 HP Zapdos was switched out and
        # Tauros took the hit instead)
        keep = own["hp"] / max(1, own["max_hp"])
        values.append(offense(party[i], opp) - incoming - SWITCH_COST + max(0.0, -here) * keep -
                      (TRAP_PENALTY if trapped_by(opp, party[i]) else 0.0))
    LAST_SWITCH["age"] = LAST_SWITCH.get("age", 99) + 1
    if LOOKAHEAD_DEPTH and actions:  # the next turns played out replace the one-turn estimate
        depth = LEADER_DEPTH if status.data.get("leader") else LOOKAHEAD_DEPTH
        values = searches([(lookahead_values, (own, opp, [("move", own["moves"][i][0]) if kind == "move" else
                                                          ("switch", party[i]) for kind, i in actions],
                                               [party[j] for j in bench], depth, opp_bench))])[0]
    st = lambda m: "" if m["status"] == 0 else " status %#x" % m["status"]
    status.trace("%s %d/%d%s spd %d flags %02x/%02x vs %s %d/%d%s spd %d flags %02x/%02x (moves %s) | %s" % (
        own["species"], own["hp"], own["max_hp"], st(own), own["spd"], own["flags"][0], own["flags"][1], opp["species"],
        opp["hp"], opp["max_hp"], st(opp), opp["spd"], opp["flags"][0], opp["flags"][1], ", ".join(MOVES.get(mid, "?") for mid, _ in opp["moves"] if mid),
        ", ".join("%s %.0f" % (o[0], v) for o, v in zip(opts, values))))
    if not opts:  # no PP left anywhere: any move button makes the game use Struggle (A alone left the move menu open)
        status.log("%s vs %s: no PP left, Struggle" % (own["species"], opp["species"]))
        game.press(A)
        game.wait(6)
        game.press(MOVE_BUTTON[0])
        return
    k, lp, fp = brain.decide(status, "Which action gives %s the best chance to defeat the opponent's %s and win the battle?" %
                             (own["species"], opp["species"]), state, opts, values)
    kind, i = actions[k]
    status.log("%s vs %s: %s (Laya %.0f%%, final %.0f%%)" % (own["species"], opp["species"], opts[k][0], lp * 100,
                                                          fp * 100))
    if kind == "switch":
        LAST_SWITCH.update({"from": own["species"], "age": 0})
    if kind == "move":
        game.press(A)          # BATTLE
        game.wait(6)
        game.press(MOVE_BUTTON[i])
    else:
        game.press(B)          # POKEMON
        game.wait(6)
        game.press(slot_buttons[i])


SPECIES_ID = {name.lower().replace(" ", ""): i for i, name in SPECIES.items()}
SPECIES_ID_OF = {name: i for i, name in SPECIES.items()}  # for Pokemon without an id (the crit chance)


def species_id(name):
    key = name.lower().replace(" ", "").replace("-", "").replace(".", "").replace("'", "")
    key = {"nidoran♀": "nidoranf", "nidoran♂": "nidoranm", "mrmime": "mrmime"}.get(key, key)
    if key not in SPECIES_ID:
        sys.exit("unknown Pokemon '%s'" % name)
    return SPECIES_ID[key]


# Gym Leader Castle's map (gym_leader_castle.c castle_map_nodes): the way from each room's node to a room, then A.
# The gyms run 0 (Pewter) to 7 (Viridian); 8 is the passage to the Elite Four's map, which opens on their room (10).
CASTLE_WAY = {"elite4": {0: DRIGHT, 1: DUP, 2: DUP, 3: DUP, 4: DUP, 5: DUP, 6: DRIGHT, 7: DRIGHT, 8: None, 10: A}}


def castle_route(path):
    """An input recording from boot to Gym Leader Castle on the island map: A+Start through the title and the menus
    into the Stadium area, then Left and Up (Pokemon Lab, then the castle). The bot presses A from there."""
    lines = ["# pokestadium input recording v1: frame, then button stick_x stick_y for pads 1-4\n", "# game audio\n",
             "0 0 0 0 0 0 0 0 0 0 0 0 0\n"]
    for f in range(40, 361, 20):
        lines.append("%d %d 0 0 0 0 0 0 0 0 0 0 0\n%d 0 0 0 0 0 0 0 0 0 0 0 0\n" % (f, A | START, f + 6))
    for f, b in ((450, DLEFT), (500, DUP)):
        lines.append("%d %d 0 0 0 0 0 0 0 0 0 0 0\n%d 0 0 0 0 0 0 0 0 0 0 0 0\n" % (f, b, f + 4))
    lines.append("# end 540\n")
    with open(path, "w") as f:
        f.writelines(lines)


def pick_rental(game, want, status):
    """Takes species `want` from the rental carousel like a player would: the boxes list the Pokemon in Pokedex order,
    so it goes to the box that holds it (R/L), then straight to its row and column, checking the species under the
    cursor, with short pauses between moves. Once per game it learns where each box starts (status data
    "rental_boxes"). Falls back to searching the box when the box has gaps. False when the Pokemon is not there."""
    def cur():
        return game.state().get("rental_species", 0)

    def move(button, pause=10):
        before = cur()
        game.press(button, 2)
        for _ in range(8):  # the carousel animates: wait until the cursor has really moved
            game.wait(pause)
            now = cur()
            if now != before:
                return now
        return None  # it did not move: an edge

    def to_corner():
        while move(DUP, 6) is not None:
            pass
        while move(DLEFT, 6) is not None:
            pass

    boxes = status.data.get("rental_boxes")
    if boxes is None:  # learn the boxes: first species of each, from the first box on
        while move(L, 12) is not None:
            pass
        boxes = []
        for _ in range(16):
            to_corner()
            boxes.append(cur())
            if move(R, 12) is None:
                break
        while move(L, 12) is not None:  # back to the first box
            pass
        status.data["rental_boxes"] = boxes
        status.data["rental_box"] = 0
        status.log("rental boxes start with #%s" % ", #".join(str(x) for x in boxes))
    # the box that holds `want`: the last one starting at or before it
    target = max([i for i, first in enumerate(boxes) if first <= want] or [0])
    here = status.data.get("rental_box", 0)
    moved_box = here != target
    while here < target and move(R, 12) is not None:
        here += 1
    while here > target and move(L, 12) is not None:
        here -= 1
    status.data["rental_box"] = here
    # from where the cursor is to the Pokemon's place: three per row in Pokedex order, so rows and columns follow
    # from the two numbers (no going back to the top of the box between picks)
    now = cur()
    if not (boxes[here] <= now) or moved_box:
        to_corner()
        now = cur()
    pos_now, pos_want = now - boxes[here], want - boxes[here]
    rows = pos_want // 3 - pos_now // 3
    for _ in range(abs(rows)):
        move(DDOWN if rows > 0 else DUP, 8)
    cols = pos_want % 3 - pos_now % 3
    for _ in range(abs(cols)):
        move(DRIGHT if cols > 0 else DLEFT, 8)
    if cur() != want:  # a gap in the box (a Pokemon not offered): look around in this box
        to_corner()
        direction = DRIGHT
        while cur() != want:
            if move(direction, 8) is not None:
                continue
            if move(DDOWN, 8) is None:
                break
            direction = DLEFT if direction == DRIGHT else DRIGHT
    if cur() != want:
        status.log("could not find %s on the rental carousel" % SPECIES.get(want, want))
        return False
    game.wait(12)  # a short look before taking it
    game.press(A)
    game.wait(30)
    return True


def pick_random_rental(game, rng, status):
    """On the rental carousel: a random box, row and column, then A (the game refuses duplicates and Pokemon over the
    cup's limits; the carousel then simply waits again)"""
    for _ in range(rng.randint(0, 4)):
        game.press(R, 2)
        game.wait(12)
    for button, most in ((DDOWN, 5), (DRIGHT, 2)):
        for _ in range(rng.randint(0, most)):
            game.press(button, 2)
            game.wait(12)
    game.press(A)
    game.wait(30)


def weaknesses(mon):
    """the attacking types that hit this Pokemon super effectively"""
    return {t for t in CHART if effectiveness(t, mon["types"]) > 1}


# Team decisions put defence first: the estimate's one-on-one favours raw power (the legendary birds), but a team
# loses to the opponents that endanger several of its Pokemon at once (one Rock Slide user threatens all three birds).
DEFENCE_WEIGHT = 0.3  # extra weight on the opponent's danger in each matchup of a team decision
SHARED_WEIGHT = 40.0  # per opponent: the danger it poses to our Pokemon beyond the most endangered one


def danger(o, m):
    """how dangerous opponent o is to our m, about 0-2: its best hit's share of m's HP (a knock-out: 1), less when m
    moves first, plus 1 for a faster trapper (m may never act)"""
    return min(1.0, threat(o, m)) * (1.0 if faster(o, m) else 0.7) + (1.0 if trapped_by(o, m) else 0.0)


def duel_turns(att, dfn, risk=1.0):
    """the turns att needs to knock dfn out with its best attack, on average (misses, charging and recharge turns
    included; accuracy ** risk); inf when it cannot hurt dfn"""
    best = float("inf")
    for mid, pp in att["moves"]:
        d = MOVEDATA.get(mid)
        if not mid or pp <= 0 or d is None or d["effect"] == EXPLODE:
            continue  # (Explosion and Selfdestruct end the duel with the user: counted as a repeatable attack, they
            # made Cloyster the lead against Misty, where it could do nothing and was switched out at once)
        dmg = damage(att, dfn, mid)
        if dmg <= 0:
            continue
        hits = max(1, math.ceil(dfn["hp"] / dmg - 1e-9))
        turns = 2 * hits if d["effect"] in (CHARGE, FLY) else (2 * hits - 1 if d["effect"] == HYPER_BEAM else hits)
        best = min(best, turns / ((d["accuracy"] / 100.0) ** risk))
    return best


# The duel plays the one-on-one out turn by turn on expected values: an attack lands a whole hit once its accuracy
# has added up to one (a 70% move: a hit every 1.4 turns), at that point within the turn: of two hits in one turn the
# earlier lands first (the faster on a tie) Each
# side may open with one move that is not an attack (sleep, paralysis, poison, Leech Seed, confusion, Reflect,
# Light Screen, a stat boost), and heals when low (Recover, Softboiled, Rest); draining and recoil count. Damage
# alone made the fast special attackers the whole draft: Exeggutor's Hypnosis and Leech Seed, Snorlax's Rest and
# Chansey's Reflect counted for nothing.
SLEEP_TURNS = 3.0      # turns a sleeping Pokemon loses (Game Boy sleep: 1-7)
SLEEPS_PER_DUEL = 2    # put to sleep again once it wakes, at most this often (the castle's trainers switch sleepers
# out; re-sleeping without end made Jynx the best answer to 68 of 89 opponents and every draft's first pick)
CONFUSED_TURNS = 3.0   # turns of confusion (2-5), half of them spent hitting itself
DUEL_TURNS = 40        # longer duels count as a draw by the HP left
LIGHT_SCREEN = 64
STATUS_PLANS = {SLEEP, PARALYZE, POISON, LEECH_SEED, CONFUSE, LIGHT_SCREEN, EVA_UP, ACC_DOWN}
TRAP_HOLD = 2.0        # turns a trapped Pokemon loses after each hit of Wrap, Fire Spin and the like (2-5 in all)
BOOSTS = {"Amnesia": ("spc", 2.0), "Swords Dance": ("atk", 2.0), "Agility": ("spd", 2.0), "Barrier": ("def", 2.0),
          "Acid Armor": ("def", 2.0), "Reflect": ("def", 2.0), "Growth": ("spc", 1.5), "Meditate": ("atk", 1.5),
          "Sharpen": ("atk", 1.5), "Harden": ("def", 1.5), "Withdraw": ("def", 1.5), "Defense Curl": ("def", 1.5)}
HEALS = {"Recover": 0.5, "Softboiled": 0.5, "Rest": 1.0}
# A game's style weighs its team decisions (draft, the three, the lead) differently; battles are played alike.
# risk: accuracy ** risk for our attacks (TEAM_RISK); status: how much our sleep, paralysis, poison and confusion
# count; bulk: how much more a win counts the more HP it keeps.
STYLES = {
    "balanced": {"risk": TEAM_RISK, "status": 1.0, "bulk": 0.0, "text": "a balanced team"},
    "safe": {"risk": 2.5, "status": 1.0, "bulk": 0.3,
             "text": "a safe team: attacks that hit (90% or more) and few two-turn moves"},
    "aggressive": {"risk": 0.8, "status": 0.6, "bulk": -0.3,
                   "text": "an aggressive team: the hardest hitters, strong moves even when they miss at times"},
    "control": {"risk": TEAM_RISK, "status": 1.5, "bulk": 0.0,
                "text": "a control team: sleep, paralysis, poison and confusion before the attack"},
    "bulky": {"risk": TEAM_RISK, "status": 1.0, "bulk": 1.0,
              "text": "a bulky team: Pokemon that take hits and win with HP to spare, healing and Reflect"},
}
STYLE_ORDER = ("balanced", "safe", "aggressive", "control", "bulky")
# the games of a run, in turn (--styles): over four batches of all five, balanced games got furthest (on average to
# the 12th of 13 leaders, three of the four full clears), aggressive 9.8, safe 8.0, bulky 7.8, control 6.0
DEFAULT_STYLES = ("balanced", "aggressive", "balanced", "safe", "balanced")


def duel_status_moves(mids):
    """of the moves that are not attacks, the ones the duel knows, one of each kind: the surest sleep or paralysis,
    Toxic before the other poisons, the strongest boost of each stat, Recover before Rest"""
    best = {}
    for mid in mids:
        d, name = MOVEDATA.get(mid), MOVES.get(mid, "")
        if d is None or d["power"] > 0:
            continue
        if name in BOOSTS:
            kind, rank = ("boost", BOOSTS[name][0]), BOOSTS[name][1]
        elif name in HEALS:
            kind, rank = ("heal",), 1 if name == "Rest" else 2
        elif d["effect"] in STATUS_PLANS:  # (Minimize and Double Team, Sand-Attack and Flash among them)
            kind, rank = ("status", d["effect"]), d["accuracy"] + (50 if name == "Toxic" else 0)
        else:
            continue
        if kind not in best or rank > best[kind][0]:
            best[kind] = (rank, mid)
    return [mid for _, mid in best.values()]


def duel_plans(att, dfn):
    """the moves att may open a duel with: none, or one status move or boost that can work on dfn"""
    plans = [None]
    for mid, pp in att["moves"]:
        d = MOVEDATA.get(mid)
        if mid and pp > 0 and d is not None and d["effect"] == TRAP and damage(att, dfn, mid) > 0:
            plans.append(mid)  # wrap it again and again
        if not mid or pp <= 0 or d is None or d["power"] > 0:
            continue
        name, e = MOVES.get(mid, ""), d["effect"]
        if name in BOOSTS or e == LIGHT_SCREEN:
            plans.append(mid)
        elif e in STATUS_PLANS:
            if e == PARALYZE and name == "Thunder Wave" and "Ground" in dfn["types"]:
                continue
            if e == POISON and "Poison" in dfn["types"]:
                continue
            if e == LEECH_SEED and "Grass" in dfn["types"]:
                continue
            plans.append(mid)
    return plans


def duel_side(m, plan, risk, status=1.0):
    return {"m": m, "hp": float(m["hp"]), "plan": plan, "risk": risk, "progress": 0.0, "setup": 0.0,
            "boost": {}, "screen": None, "sleep": 0.0, "para": 0.0, "conf": 0.0, "toxic": 0, "psn": 0.0,
            "seeded": False, "best": None, "status": status, "held": 0.0, "acc": m.get("acc", 7), "eva": m.get("eva", 7),
            # as often as the move has PP (two at most made Chansey's Softboiled look beatable)
            "heals": min(8, max([pp for mid, pp in m["moves"] if mid and MOVES.get(mid) in HEALS] or [0]))}


def duel_attack(s, t):
    """s's best attack on t as it stands: (move, damage per hit, hit chance per turn ** risk, turns per hit); a
    trapping plan keeps to its move"""
    if s["plan"] and MOVEDATA[s["plan"]]["effect"] == TRAP:
        mid = s["plan"]
        return (mid, damage(s["m"], dict(t["m"], hp=t["hp"]), mid),
                hit_chance(dict(s["m"], acc=s["acc"]), dict(t["m"], eva=t["eva"]), mid) ** s["risk"], 1.0)
    if s["best"] is None or s.get("best_hp") != t["hp"]:
        att = dict(s["m"], acc=s["acc"], **{k: s["m"][k] * f for k, f in s["boost"].items() if k != "spd"})
        dfn = dict(t["m"], hp=t["hp"], status=ASLEEP if t["sleep"] > 0 else 0, eva=t["eva"],
                   **{k: t["m"][k] * f for k, f in t["boost"].items() if k != "spd"})
        best = (float("inf"), None, 0.0, 0.0, 1.0)
        for mid, pp in s["m"]["moves"]:
            d = MOVEDATA.get(mid)
            if not mid or pp <= 0 or d is None or d["effect"] == EXPLODE:
                continue
            dmg = damage(att, dfn, mid)
            if t["screen"] and (d["type"] in PHYSICAL) == (t["screen"] == "def"):
                dmg /= 2.0
            if dmg <= 0:
                continue
            per = 2.0 if d["effect"] in (CHARGE, FLY, HYPER_BEAM) else 1.0
            acc = hit_chance(att, dfn, mid) ** s["risk"]  # (with the accuracy and evasion stages)
            turns = math.ceil(t["hp"] / dmg - 1e-9) * per / acc  # the turns it needs for the knock-out
            if turns < best[0] or (turns == best[0] and dmg > best[2]):
                best = (turns, mid, dmg, acc, per)
        s["best"] = best[1:] if best[1] is not None else (None, 0.0, 0.0, 1.0)
        s["best_hp"] = t["hp"]
    return s["best"]


def duel_act(s, t):
    """one turn of s against t"""
    if s["sleep"] > 0:
        s["sleep"] -= 1
        return
    if s["held"] > 0:
        s["held"] -= 1
        return
    act = 1.0 - 0.25 * s["para"]
    if s["conf"] > 0:
        s["conf"] -= 1
        act *= 0.5
    heal = next((HEALS[MOVES[mid]] for mid, pp in s["m"]["moves"] if mid and pp > 0 and MOVES.get(mid) in HEALS), 0)
    mx = s["m"]["max_hp"]
    if heal and s["heals"] > 0 and s["hp"] < 0.4 * mx:  # heal when low (Rest: full, then two turns asleep)
        s["heals"] -= 1
        s["hp"] = min(mx, s["hp"] + heal * mx * act)
        if heal >= 1.0:
            s.update(sleep=2.0, para=0.0, toxic=0, psn=0.0)
        return
    if s["setup"] < 0:  # the sleep it put the opponent in still runs: attack meanwhile
        s["setup"] += 1
    elif s["plan"] and s["setup"] < 1.0 and MOVEDATA[s["plan"]]["effect"] != TRAP:  # its opening move, until it worked
        mid = s["plan"]
        d = MOVEDATA[mid]
        name = MOVES.get(mid, "")
        acc = 1.0 if name in BOOSTS or d["effect"] == LIGHT_SCREEN else (d["accuracy"] / 100.0) ** s["risk"]
        s["setup"] += acc * act
        if s["setup"] < 1.0:
            return
        if name in BOOSTS:
            k, f = BOOSTS[name]
            s["boost"][k] = s["boost"].get(k, 1.0) * f
            s["best"] = t["best"] = None
        elif d["effect"] == LIGHT_SCREEN:
            s["screen"], t["best"] = "spc", None
        elif d["effect"] == SLEEP:
            t["sleep"], s["best"] = SLEEP_TURNS * s["status"], None
            s["sleeps"] = s.get("sleeps", 0) + 1
            # once it wakes, put it to sleep again (or attack on, after the last)
            s["setup"] = -t["sleep"] if s["sleeps"] < SLEEPS_PER_DUEL else 1.0
        elif d["effect"] == PARALYZE:
            t["para"] = s["status"]
        elif d["effect"] == POISON:
            if name == "Toxic":
                t["toxic"] = 1
            t["psn"] = s["status"]
        elif d["effect"] == LEECH_SEED:
            t["seeded"] = True
        elif d["effect"] == CONFUSE:
            t["conf"] = CONFUSED_TURNS * s["status"]
        elif d["effect"] == EVA_UP:  # (Minimize: Chansey's Softboiled outlasted a Starmie that kept missing)
            s["eva"], t["best"] = min(13, s["eva"] + 1), None
        elif d["effect"] == ACC_DOWN:
            t["acc"], t["best"] = max(1, t["acc"] - 1), None
        return
    mid, dmg, acc, per = duel_attack(s, t)
    if mid is None:
        return
    s["progress"] += acc * act / per
    if s["progress"] >= 1.0 - 1e-9:
        s["progress"] -= 1.0
        dealt = min(t["hp"], dmg)
        t["hp"] -= dealt
        e = MOVEDATA[mid]["effect"]
        if e == TRAP and t["held"] <= 0:  # a new trap once the last has run out
            t["held"] = TRAP_HOLD
        if e == DRAIN:
            s["hp"] = min(s["m"]["max_hp"], s["hp"] + dealt / 2.0)
        elif e == RECOIL:
            s["hp"] -= dealt / 4.0


def duel_tick(s, t):
    """poison and Leech Seed at the end of the turn"""
    mx = s["m"]["max_hp"]
    if s["toxic"]:
        s["hp"] -= mx * s["toxic"] / 16.0 * s["psn"]
        s["toxic"] += 1
    elif s["psn"]:
        s["hp"] -= mx / 16.0 * s["psn"]
    if s["seeded"]:
        drained = min(s["hp"], mx / 16.0)
        s["hp"] -= drained
        t["hp"] = min(t["m"]["max_hp"], t["hp"] + drained)


def duel_when(s, t):
    """when in the turn s's move takes effect: the share of the turn its accumulated accuracy needs to make a whole
    hit (or a status move work), 1 when nothing lands this turn (asleep, healing, missing); ties go by speed"""
    mx = s["m"]["max_hp"]
    if s["sleep"] > 0 or s["held"] > 0 or (s["heals"] > 0 and s["hp"] < 0.4 * mx and any(
            MOVES.get(mid) in HEALS for mid, pp in s["m"]["moves"] if mid and pp > 0)):
        return 1.0
    act = (1.0 - 0.25 * s["para"]) * (0.5 if s["conf"] > 0 else 1.0)
    if s["plan"] and 0 <= s["setup"] < 1.0 and MOVEDATA[s["plan"]]["effect"] != TRAP:
        d = MOVEDATA[s["plan"]]
        sure = MOVES.get(s["plan"]) in BOOSTS or d["effect"] == LIGHT_SCREEN
        rate, need = (1.0 if sure else (d["accuracy"] / 100.0) ** s["risk"]) * act, 1.0 - s["setup"]
    else:
        mid, dmg, acc, per = duel_attack(s, t)
        if mid is None:
            return 1.0
        rate, need = acc * act / per, 1.0 - s["progress"]
    return need / rate if need <= rate + 1e-9 else 1.0


def duel_play(m, o, plan_m, plan_o, risk, style=None):
    style = style or STYLES["balanced"]
    a, b = duel_side(m, plan_m, risk, style["status"]), duel_side(o, plan_o, 1.0)
    for turn in range(DUEL_TURNS):
        spd_a = m["spd"] * a["boost"].get("spd", 1.0) * (0.25 if a["para"] else 1.0)
        spd_b = o["spd"] * b["boost"].get("spd", 1.0) * (0.25 if b["para"] else 1.0)
        wa, wb = duel_when(a, b), duel_when(b, a)
        order = (a, b) if wa < wb - 1e-9 or (abs(wa - wb) <= 1e-9 and spd_a > spd_b) else (b, a)
        for s, t in (order, order[::-1]):
            duel_act(s, t)
            if t["hp"] <= 0 or s["hp"] <= 0:
                break
        if a["hp"] > 0 and b["hp"] > 0:
            duel_tick(a, b)
            duel_tick(b, a)
        if a["hp"] <= 0 or b["hp"] <= 0:
            break
    ka, kb = a["hp"] / max(1, m["hp"]), b["hp"] / max(1, o["hp"])
    if b["hp"] <= 0 and a["hp"] > 0:
        return 30.0 + 100.0 * min(1.0, ka) + style["bulk"] * 50.0 * (min(1.0, ka) - 0.5)
    if a["hp"] <= 0:
        return -30.0 - 100.0 * max(0.0, kb)
    return 30.0 * (ka - kb)  # nobody fell: a draw, by the HP left


def duel(m, o, risk=1.0, style=None):
    """who wins the one-on-one, and by how much (about -130 to 130), each side with its best opening (duel_plans)
    against the other's best answer to it (DUEL_TURNS above). The single-turn estimate called Zapdos against Onix
    (it cannot hurt it; Rock Slide takes a third of its HP a turn) only mildly unfavourable."""
    if trapped_by(o, m):
        return -130.0  # a faster trapper never lets it act
    key = (id(m), id(o), risk, m["hp"], o["hp"], id(style))
    hit = DUEL_CACHE.get(key)
    if hit is not None and hit[0] is m and hit[1] is o:
        return hit[2]
    theirs = duel_plans(o, m)
    v = max(min(duel_play(m, o, p, q, risk, style) for q in theirs) for p in duel_plans(m, o))
    if len(DUEL_CACHE) > 200000:
        DUEL_CACHE.clear()
    DUEL_CACHE[key] = (m, o, v)
    return v


DUEL_CACHE = {}


def entered(o, k):
    """k after switching in against o: o's best expected hit already taken"""
    hit = max((damage(o, k, mid) * hit_chance(o, k, mid) for mid, pp in o["moves"] if mid and pp > 0), default=0.0)
    return dict(k, hp=max(1, int(k["hp"] - hit)))


def team_matchup(m, o, style=None):
    """a matchup for team decisions: who wins the one-on-one (duel, accuracy weighed harder), as the game's style
    weighs it"""
    st = STYLES[style or "balanced"]
    return duel(m, o, st["risk"], st)


def shared_danger(dz):
    """dz: per member of a team, its danger from each opponent -> the average over opponents of the danger beyond the
    most endangered member, scaled to a team of three (three birds against Onix: high; each opponent endangering
    only one of ours: low)"""
    if len(dz) < 2:
        return 0.0
    total = 0.0
    for j in range(len(dz[0])):
        d = sorted((row[j] for row in dz), reverse=True)
        total += sum(d[1:])
    return total / len(dz[0]) * 2.0 / (len(dz) - 1)


def attack_options(m):
    """its attacks for Laya: sure ones (single turn, 90%+ accuracy) and risky ones (misses often, or charges first)"""
    sure, risky = [], []
    for mid, _ in m["moves"]:
        d = MOVEDATA.get(mid)
        if not mid or d is None or (d["power"] == 0 and MOVES.get(mid) not in ("Night Shade", "Seismic Toss")):
            continue
        name = "%s (%s, %d%%)" % (MOVES.get(mid, "?"), d["type"], d["accuracy"])
        (risky if d["accuracy"] < 90 or d["effect"] in (CHARGE, FLY, HYPER_BEAM) else sure).append(name)
    return "sure attacks: %s; risky attacks: %s" % (", ".join(sure) or "none", ", ".join(risky) or "none")


TRAINER_BONUS = 15       # the castle's Round 2 trainers have about this much more in every stat than the rentals
DRAFT_OPPONENTS = 0.6    # the share of the rental list (strongest by total stats) the draft prepares for
DRAFT_POOL = 30          # the shortlist is drawn from this many of the most useful picks, weighted by their gain
DRAFT_TEMPERATURE = 10.0  # gain units per factor e in that weighting (lower: the very best almost always)
BATCH_VARIETY = 8.0      # a species drafted by another game of this run counts this much less per game
BATCH_DRAFTED = {}       # species id -> games of this run that drafted it
BATCH_LOCK = threading.Lock()
# the games of a run draft one after another (each waits for the picks of the games before it, up to two minutes):
# drafting all at once, two games took the very same team



EXPLORE_POOL, EXPLORE_TEMPERATURE, EXPLORE_VARIETY = 60, 45.0, 3.0  # an experimental draft: wider, looser, and
# three times as wary of what the other games took


def draft_team(brain, status, roster, rng, size=6, shortlist=6, explore=False):
    """A rental team built one Pokemon at a time, judged by its answers: for every Pokemon in the rental list (the
    pool the castle's trainers draw from too), the team's best matchup against it (the battle estimate). A team is
    good when its average answer is good and its weakest answers are not bad; each step offers Laya a random
    shortlist of the candidates that raise that the most (a different team every run). No type rules: overlapping
    types and shared weaknesses simply add little. -> species ids"""
    mons, seen = [], set()
    for h in roster:
        m = party_mon(h)
        if m["species"].startswith("#") or m["species"] in seen:
            continue
        seen.add(m["species"])
        m["id"] = bytes.fromhex(h)[0]
        mons.append(m)
    if len(mons) < size:
        return None
    # the opponents to prepare for: the stronger part of the list (the castle's trainers use evolved Pokemon; against
    # the whole list, power that flattens Caterpie and Magikarp looked like strength)
    total = lambda m: m["max_hp"] + m["atk"] + m["def"] + m["spd"] + m["spc"]
    opps = sorted(mons, key=lambda m: -total(m))[:max(size, int(len(mons) * DRAFT_OPPONENTS))]
    # as the trainers field them (58 species compared: HP and speed about 15 higher; their Ninetales outruns the birds)
    opps = [dict(o, hp=o["hp"] + TRAINER_BONUS, max_hp=o["max_hp"] + TRAINER_BONUS,
                 **{k: o[k] + TRAINER_BONUS for k in ("atk", "def", "spd", "spc")}) for o in opps]
    # and with any attack they may know (the trainers' movesets are not the rentals': learnsets, load_learnsets)
    # with their status moves too: Alakazam's Recover and Reflect, Clefable's Thunder Wave and Arbok's Glare had been
    # left out, so the draft prepared for opponents weaker than the ones that stopped it
    if ALLOWED:
        opps = [dict(o, moves=[(mid, 10) for mid in ALLOWED.get(o["id"], ()) if MOVEDATA[mid]["power"] > 0] +
                     [(mid, 10) for mid in duel_status_moves(ALLOWED.get(o["id"], ()))]
                     if ALLOWED.get(o["id"]) else o["moves"]) for o in opps]
    style = status.data.get("style")
    vs = {m["id"]: [team_matchup(m, o, style) for o in opps] for m in mons}  # each one's duel with each opponent
    dz = {m["id"]: [danger(o, m) for o in opps] for m in mons}  # and each opponent's danger to it
    worst = max(1, len(opps) // 4)

    def answers(team):
        # depth: the best answer and, less, the second best (one star covering much of the list alone made a team
        # look complete; in a three-on-three the first answer is often down or shut out)
        out = []
        for j in range(len(opps)):
            v = sorted((vs[m["id"]][j] for m in team), reverse=True)
            out.append(v[0] if len(v) == 1 else 0.6 * v[0] + 0.4 * v[1])
        return out

    def score(team):  # the average answer plus the average of the weakest quarter, less the shared danger
        a = sorted(answers(team))
        return sum(a) / len(a) + sum(a[:worst]) / worst - SHARED_WEIGHT * shared_danger([dz[m["id"]] for m in team])

    team = []
    while len(team) < size:
        base = score(team) if team else 0.0
        gains = sorted(((score(team + [m]) - base, i) for i, m in enumerate(mons) if m not in team), reverse=True)
        with BATCH_LOCK:  # what the other games already took counts a little less (a run covers more Pokemon)
            gains = sorted(((g - BATCH_VARIETY * (EXPLORE_VARIETY if explore else 1) * BATCH_DRAFTED.get(mons[i]["id"], 0), i)
                            for g, i in gains), reverse=True)
        top = gains[:EXPLORE_POOL if explore else DRAFT_POOL]
        cands = []  # drawn without repeats, the more useful the likelier (five games no longer draft alike)
        while top and len(cands) < shortlist:
            weights = [math.exp((g - top[0][0]) / (EXPLORE_TEMPERATURE if explore else DRAFT_TEMPERATURE)) for g, _ in top]
            k = rng.choices(range(len(top)), weights)[0]
            cands.append(mons[top.pop(k)[1]])
        gain = {mons[i]["id"]: g for g, i in gains}
        values = [gain[m["id"]] for m in cands]
        opts = [(m["species"], "%s (%s), level %d: %s; weak to %s" % (
            m["species"], types_text(m["types"]), m["level"], attack_options(m),
            ", ".join(sorted(weaknesses(m))) or "nothing")) for m in cands]
        hardest = []
        if team:
            a = answers(team)
            hardest = [opps[j]["species"] for j in sorted(range(len(opps)), key=lambda j: a[j])[:5]]
        state = {"team_style": STYLES[style or "balanced"]["text"],
                 "team_so_far": [{"name": m["species"], "types": types_text(m["types"])} for m in team],
                 "opponents_the_team_has_no_good_answer_to": hardest, "picks_left": size - len(team)}
        k, lp, fp = brain.decide(status, "Which Pokemon gives this team an answer to the most opponents, especially the "
                                 "ones it cannot beat yet, with attacks it can rely on?", state, opts, values)
        status.trace("draft gains: " + ", ".join("%s %.0f" % (m["species"], v) for m, v in zip(cands, values)))
        team.append(cands[k])
        with BATCH_LOCK:  # at once: the other games draft at the same time
            BATCH_DRAFTED[cands[k]["id"]] = BATCH_DRAFTED.get(cands[k]["id"], 0) + 1
        status.log("draft: %s (Laya %.0f%%, final %.0f%%) from %s" % (
            cands[k]["species"], lp * 100, fp * 100, ", ".join(m["species"] for m in cands)))
    a = answers(team)
    status.log("draft: hardest opponents for this team: %s" % ", ".join(
        "%s (%.0f)" % (opps[j]["species"], a[j]) for j in sorted(range(len(opps)), key=lambda j: a[j])[:5]))
    return [m["id"] for m in team]


RETRY_TRIO = 25.0  # what each loss against the same team takes off the three it was lost with
RETRY_LEAD = 20.0  # and off its lead


def choose_team(brain, status, order):
    """The three Pokemon to send in against the opponent's team, the lead first: the three whose answers to the
    opponent's six (the battle estimate) are best together; Laya and the estimate pick the lead. -> slots (0-5)"""
    own = [party_mon(h) for h in order.get("own", [])]
    opp = [party_mon(h) for h in order.get("opp", [])]
    if any(m["species"].startswith("#") for m in own + opp):
        if not status.data.get("dumped_teams"):  # once, to see what the game reports
            status.data["dumped_teams"] = True
            print("[debug] undecodable teams: own %s opp %s" % ([h[:24] for h in order.get("own", [])],
                                                              [h[:24] for h in order.get("opp", [])]), flush=True)
        return None  # the screen is not filled in yet (a registered team arrives a little later): ask again
    if len(own) <= 3 or not opp:
        status.log("only %d of your Pokemon reported: sending in slots 1-3" % len(own))
        return list(range(min(3, len(own)))) or [0, 1, 2]
    # the three as a set, judged like the draft: their best answer to each of the opponent's six, the average and
    # the weakest two (the opponent sends in three, likely the ones that beat ours). Choosing each one on its own
    # sent three Pokemon slower than a Ninetales with Fire Spin while a faster one waited on the bench.
    import itertools
    style = status.data.get("style")
    vs = [[team_matchup(m, o, style) for o in opp] for m in own]
    dz = [[danger(o, m) for o in opp] for m in own]

    def trio_score(t):
        # each of ours usually meets several of theirs (who comes in next is not ours to choose), so an answer counts
        # half the best of the three and half their average (Zapdos ended up alone against Onix, which its moves
        # cannot touch, because the other two had answered Onix)
        a = sorted(0.5 * max(vs[i][j] for i in t) + 0.5 * sum(vs[i][j] for i in t) / 3 for j in range(len(opp)))
        return sum(a) / len(a) + sum(a[:2]) / 2 - SHARED_WEIGHT * shared_danger([dz[i] for i in t])

    # a retry against the same team tries something else: each loss costs the three it was lost with (and its lead)
    # this much (game 4 sent Paras, Exeggutor and Gengar against Giovanni five times, and lost five times)
    key = ", ".join(sorted(o["species"] for o in opp))  # (a string: the status page sends status.data as JSON)
    lost = status.data.setdefault("lost_picks", {}).get(key, [])
    names = lambda t: tuple(sorted(own[i]["species"] for i in t))
    trio = max(itertools.combinations(range(len(own)), 3),
               key=lambda t: trio_score(t) - RETRY_TRIO * sum(1 for x, _ in lost if x == names(t)))
    # who goes first: Laya and the estimate. The opponent's lead cannot be foreseen (its choice weighs our team in a
    # way the matchups do not predict), so against each of its six a lead either stays in or makes way for the best
    # of the other two, which takes that opponent's hit on the way in; the lead counts its two worst such outcomes as
    # much as the average. Gengar and Haunter led against Misty for the best average and lost all 13 times to her
    # Starmie; Articuno led against Lt. Surge and lost 39 of 47: against his Raichu the only way out was Nidoking
    # taking a Surf on the way in, while a Nidoking lead could make way for a fresh bird against his others.
    def lead_outcomes(i):
        out = []
        for j, o in enumerate(opp):
            way_out = max((team_matchup(entered(o, own[k]), o, style) for k in trio if k != i), default=-130.0)
            out.append(max(vs[i][j], way_out))
        return out

    values = [(lambda v: 0.5 * sum(v) / len(v) + 0.5 * sum(sorted(v)[:2]) / 2)(lead_outcomes(i)) -
              RETRY_LEAD * sum(1 for _, x in lost if x == own[i]["species"]) for i in trio]
    state = {"opponent_team": [{"name": o["species"], "level": o["level"], "types": types_text(o["types"])} for o in opp],
             "your_three": [{"name": own[i]["species"], "level": own[i]["level"], "types": types_text(own[i]["types"])}
                            for i in trio]}
    opts = [(own[i]["species"], "%s (%s): %s" % (own[i]["species"], types_text(own[i]["types"]), ", ".join(
        MOVES.get(mid, "?") for mid, _ in own[i]["moves"] if mid))) for i in trio]
    k, lp, fp = brain.decide(status, "Which of your three Pokemon should lead against the opponent's team (%s)?" %
                             ", ".join(o["species"] for o in opp), state, opts, values)
    picks = [trio[k]] + sorted((i for i in trio if i != trio[k]), key=lambda i: -sum(vs[i]))
    status.set(team=[m["id"] for m in own], trio=[own[i]["id"] for i in picks], opp_team=[o["id"] for o in opp],
               opp_seen={}, party=[])
    status.data["last_pick"] = (key, names(picks), own[picks[0]]["species"])
    status.log("team vs %s: %s" % (", ".join(o["species"] for o in opp),
                                   ", ".join(own[i]["species"] for i in picks)))
    return picks


def run_game(game, brain, status, pick_spec, wins, armed, jitter, rng, random_team=False, team=None, route_end=0,
             register=False, room=None, max_losses=0, explore=False, draft_order=0, castle=False,
             leaders_only=False, max_tries=0, max_e4_runs=0):
    """Plays one game until `wins` battles are won (or the game closes, or it stalls). pick_spec: slots (0-5) or
    "random"; random_team: choose the rental team at random on the rental carousel."""
    def new_picks(order=None):
        if pick_spec == "smart":
            if order and order.get("opp"):
                return choose_team(brain, status, order)
            return [0, 1, 2]
        p = rng.sample(range(6), 3) if pick_spec == "random" else list(pick_spec)
        if pick_spec == "random":
            status.log("team order for this battle: slots %s" % ",".join(str(i + 1) for i in p))
        return p

    picks = new_picks()
    in_battle = False
    last_choice = None
    first_pick = True
    last_order_tick = -10**9
    last_rental_tick = -10**9
    progress_tick = 0
    rentals = 0
    if armed:
        status.set(phase="playing")
    else:
        status.set(phase="waiting for the first team-order screen (choose your cup and team)")
    while game.state()["tick"] < route_end + 10:  # the route recording still plays: no presses of our own
        game.wait(30)
    progress_tick = game.state()["tick"]
    rental_round = 0
    picks_battle = None
    order_seen_since = {}
    registered = False
    rejected = set()
    last_thumb = 0.0
    last_poll = time.time()
    while status.get("wins") < wins:
        if time.time() - last_poll > 2.0:  # the bot was busy elsewhere (another game's long decision): the game ran on
            progress_tick = game.state()["tick"]  # unattended, which is no sign of a stuck game
        last_poll = time.time()
        if status.get("thumb") and time.time() - last_thumb > 1.0:  # a picture for the status page, once a second
            game.thumb(status.get("thumb"))
            last_thumb = time.time()
        st = game.state()
        now = st["tick"]
        choice, order = st.get("choice"), st.get("order")
        if choice and choice["age"] <= 2:
            last_choice = choice
        battling = 0 <= st["battle_age"] <= 30
        if now - progress_tick > (BATTLE_STALL_FRAMES if battling else STALL_FRAMES) and armed:
            status.log("stuck: nothing happened for %d frames (frame %d)" % (STALL_FRAMES, now))
            status.set(phase="stuck at frame %d" % now, done=True)
            return
        if team == "draft" and st.get("rental_age", 99) <= 2:  # the carousel is up: draft from the rental list
            roster = game.roster()
            if not roster:
                game.wait(10)
                continue
            try:  # kept for looking at the draft offline (build/, not in git)
                with open(os.path.join(ROOT, "build", "autoplay_roster.txt"), "w") as f:
                    f.write("\n".join(roster) + "\n")
            except OSError:
                pass
            waited = status.data.setdefault("draft_wait", time.time())  # after the games before it (see BATCH_DRAFTED)
            with BATCH_LOCK:
                done = sum(BATCH_DRAFTED.values())
            if done < 6 * draft_order and time.time() - waited < 120:
                status.set(phase="waiting for the drafts of the games before")
                progress_tick = now
                game.wait(30)
                continue
            team = draft_team(brain, status, roster, rng, explore=explore)
            if team is None:
                status.log("could not read the rental list: stopped")
                status.set(phase="stopped: no rental list", done=True)
                return
            status.log("drafted team (%s): %s" % (status.get("style") or "balanced", ", ".join(SPECIES.get(t, "?") for t in team)))
            status.set(team=list(team))
            progress_tick = now
            continue
        if team and st.get("rental_confirm_age", 99) <= 2:  # the team is complete: OK / OK to Register / ...
            progress_tick = now
            if register and not registered:
                game.press(DDOWN, 2)  # "OK to Register": saved, picked as a registered team next time
                game.wait(12)
                registered = True
                status.log("registering the team")
            game.press(A)
            game.wait(40)
            continue
        if team and st.get("rental_age", 99) <= 2:
            if status.get("battles") != rental_round:  # a new rental round (the castle asks again before every gym)
                rental_round = status.get("battles")
                rentals = 0
            progress_tick = now
            last_rental_tick = now
            if rentals < len(team):
                status.set(phase="choosing rentals: %s" % SPECIES.get(team[rentals], "?"))
                if pick_rental(game, team[rentals], status):
                    status.log("rented %s" % SPECIES.get(team[rentals], "?"))
                rentals += 1
            else:
                game.press(START)  # the team is complete: the game asks to register it
                game.wait(30)
            continue
        if random_team and st.get("rental_age", 99) <= 2:
            progress_tick = now
            last_rental_tick = now
            if rentals == 0:
                status.set(phase="choosing a random rental team")
            rentals += 1
            pick_random_rental(game, rng, status)
            continue
        if room and st.get("castle_age", 99) <= 2 and st.get("castle_node", -1) >= 0:  # the castle's map: go to the room
            progress_tick = now
            step = CASTLE_WAY[room].get(st["castle_node"], A)
            if step is None:  # the passage to the Elite Four: the map changes on its own
                game.wait(10)
            else:
                game.press(step)
                game.wait(12)
            continue
        if order and order["age"] <= 2:
            progress_tick = now
            if not armed:
                armed = True
                status.log("team-order screen: the bot plays from here")
            # a new battle's order screen: choose once per battle (a Laya decision takes longer than the gap check)
            if order["picked"] == 0 and picks_battle != status.get("battles") and (
                    pick_spec != "smart" or order.get("opp")) and (not first_pick or pick_spec == "smart"):
                chosen = new_picks(order)
                if chosen is None and now - order_seen_since.setdefault(status.get("battles"), now) < 600:
                    game.wait(10)
                    continue
                if chosen is None:  # the teams never decoded: the first three
                    status.log("could not read the teams: sending in slots 1-3")
                    chosen = [0, 1, 2]
                picks = chosen
                picks_battle = status.get("battles")
                rejected = set()
                # in the castle each gym is three trainers and its leader, then the Elite Four (from the wins so far)
                w = status.get("wins")
                leader = castle and (leaders_only or w >= 32 or w % 4 == 3)
                status.set(leader=leader)
                if leader:
                    status.log("%s: looking %d turns ahead" % ("Elite Four" if w >= (8 if leaders_only else 32) else
                                                               "gym leader", LEADER_DEPTH))
            last_order_tick = now
            in_battle = False
            status.set(phase="choosing the team order (battle %d)" % (status.get("battles") + 1))
            if pick_spec == "smart" and picks_battle != status.get("battles"):
                # no choice for this battle yet: undo what an earlier press picked (A while getting through the
                # screens between battles), then wait for the opponent's team and choose
                if order["sub"] == 0 and order["picked"] > 0:
                    game.press(L)  # undo the last pick
                    game.wait(10)
                elif order["sub"] == 1:  # "Is this OK?": Re-select
                    game.press(DRIGHT, 2)
                    game.wait(8)
                    game.press(A)
                    game.wait(20)
                else:
                    game.wait(10)
                continue
            if order["sub"] == 0 and order["picked"] < 6:
                if first_pick and jitter:  # a different moment for each game: the game's random numbers differ
                    game.wait(jitter)
                first_pick = False
                # the planned slot, else (taken, or not in the team) any other one that is not known to be refused
                cands = [p for p in picks if p not in rejected] + [p for p in range(6) if p not in picks]
                cands = [p for p in cands if p not in rejected]
                if order["picked"] < len(picks) and picks[order["picked"]] not in rejected:
                    slot = picks[order["picked"]]
                else:
                    slot = cands[0] if cands else 0
                before = order["picked"]
                game.press(ORDER_BUTTON[slot])
                game.wait(10)
                after = (game.state().get("order") or {}).get("picked", before)
                if after == before:
                    rejected.add(slot)
                    if len(rejected) >= 6:
                        rejected.clear()
            elif order["sub"] == 1:  # "Is this OK?" (cursor on Yes)
                game.press(A)
                game.wait(20)
            elif order["sub"] == 2:  # the picks break the cup's rules (e.g. Poke Cup: 155 levels at most)
                game.press(A)  # the notice clears the picks
                game.wait(20)
                if pick_spec != "random":
                    status.log("your picks (slots %s) break this cup's rules (level cap); choose others with --pick" %
                               ",".join(str(i + 1) for i in picks))
                    status.set(phase="stopped: picks not allowed in this cup", done=True)
                    return
                status.log("slots %s break the cup's level cap: new random picks" % ",".join(str(i + 1) for i in picks))
                picks = new_picks()
            else:  # fading in (-1) or done (3)
                game.wait(10)
            continue
        # player 1's command menu is waiting (sub 0), or a fainted Pokemon has to be replaced
        if choice and choice["age"] <= 2 and ((choice["menu"] == 0 and choice["sub"] == 0) or choice["forced"]):
            if not armed:
                game.wait(10)
                continue
            progress_tick = now
            if not in_battle:
                in_battle = True
                status.set(phase="battle %d" % (status.get("battles") + 1))
            play_turn(game, brain, status, choice)
            progress_tick = game.state()["tick"]  # the game ran on while the bot thought
            game.wait(20)
            continue
        if 0 <= st["battle_age"] <= 30:  # battle animations and messages
            if armed and not in_battle:
                in_battle = True
                status.set(phase="battle %d" % (status.get("battles") + 1))
            game.wait(15)
            continue
        if in_battle:  # the battle screen is gone: who won?
            in_battle = False
            progress_tick = now
            # the game's own verdict (1/2: side 0/1 won, 3: draw; BattleScene_DetermineWinner and the knock-out rule)
            result, side = st.get("result", 0), st.get("side", 0)
            won = result in (1, 2) and result - 1 == side
            with status.status.lock:
                status.data["history"].append({"won": won, "opp": status.data.get("opp_team", [])})
            status.set(battles=status.get("battles") + 1)
            status.data["tries"] = 0 if won else status.data.get("tries", 0) + 1  # losses in a row (the same battle)
            if won:
                status.data.pop("last_pick", None)
                status.set(wins=status.get("wins") + 1)
                status.log("battle won (%d/%d)" % (status.get("wins"), wins))
            else:
                status.set(losses=status.get("losses") + 1)
                if status.data.get("last_pick"):  # the next try against this team prefers another three or lead
                    key, trio_names, lead = status.data.pop("last_pick")
                    status.data.setdefault("lost_picks", {}).setdefault(key, []).append((trio_names, lead))
                # the Elite Four and the champion are one series: a loss starts it over at the first of them (counted
                # on, the wins said 13 of 13 after Lorelei, Bruno, a loss to Agatha, Lorelei, Bruno and Agatha)
                e4 = 8 if leaders_only else 32
                if castle and status.get("wins") >= e4:
                    status.data["e4_runs"] = status.data.get("e4_runs", 0) + 1  # failed runs at the Elite Four
                    if status.get("wins") > e4:
                        status.log("the Elite Four starts over (%d wins in it undone)" % (status.get("wins") - e4))
                        status.set(wins=e4)
                    # every win over Lorelei or Bruno reset the losses in a row: games went round them for hours
                    if max_e4_runs and status.data["e4_runs"] >= max_e4_runs:
                        status.log("battle lost: %d failed Elite Four runs, stopping" % status.data["e4_runs"])
                        status.set(phase="stopped after %d Elite Four runs (%d wins)" % (status.data["e4_runs"],
                                                                                          status.get("wins")), done=True)
                        return
                if max_tries and status.data["tries"] >= max_tries:
                    status.log("battle lost: %d times in a row, stopping" % status.data["tries"])
                    status.set(phase="stopped after %d tries (%d wins)" % (status.data["tries"], status.get("wins")),
                               done=True)
                    return
                if max_losses and status.get("losses") >= max_losses:
                    status.log("battle lost%s: %d losses, stopping" % (" (draw)" if result == 3 else "",
                                                                     status.get("losses")))
                    status.set(phase="stopped after %d losses (%d wins)" % (status.get("losses"), status.get("wins")),
                               done=True)
                    return
                status.log("battle lost%s, continuing" % (" (draw)" if result == 3 else ""))
            status.set(own=None, opp=None, options=[], party=[], last_switch={})
        # results, cup progress, dialogs, retry prompts: advance (never right after a team or rental screen, where A
        # would pick a Pokemon)
        if armed and now - last_order_tick > 120 and now - last_rental_tick > 60:
            if status.get("phase").startswith("choosing a random rental"):
                status.set(phase="registering the team")
            elif not status.get("phase").startswith("registering"):
                status.set(phase="between battles")
            game.press(A)
            game.wait(40)
        else:
            game.wait(20)
    status.set(phase="done: %d battles won, %d lost" % (status.get("wins"), status.get("losses")), done=True)
    status.log("done")


def main():
    global LOOKAHEAD_DEPTH, LEADER_DEPTH
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pick", default="smart",
                    help="which three to send in: smart (default: Laya and the estimate against the opponent's team), "
                         "random, or team-order slots in order, e.g. 1,2,3")
    ap.add_argument("--wins", type=int, default=8, help="battles to win before stopping (default 8, a cup)")
    ap.add_argument("--max-tries", type=int, default=0,
                    help="stop a game after losing the same battle this many times in a row (default 0: keep going); "
                         "with --max-losses 0, each battle is retried until won, for measuring win rates per opponent")
    ap.add_argument("--max-losses", type=int, default=0,
                    help="stop a game (and close it) after this many lost battles (default 0: keep going)")
    ap.add_argument("--speed", default=None, help="game speed (PORT_SPEED: 2, max, ...; default 1, max with --games)")
    ap.add_argument("--rom", help="Stadium ROM (default: the launcher's choice; the launcher shows first)")
    ap.add_argument("--model", default="convaiinnovations/laya", help="Laya checkpoint")
    ap.add_argument("--port", type=int, default=8777, help="status window port")
    ap.add_argument("--browser", action="store_true", help="open the status page in the browser")
    ap.add_argument("--no-browser", action="store_true", help=argparse.SUPPRESS)  # the default now (kept for scripts)
    ap.add_argument("--attach", action="store_true", help="use a game already started with PORT_BOT=" + SOCK)
    ap.add_argument("--armed", action="store_true", help="play right away (e.g. attached in the middle of a cup)")
    ap.add_argument("--games", type=int, default=0,
                    help="run this many hidden games at once from --route (the cup and team picked by the route)")
    ap.add_argument("--route", help="an input recording (tools/... PORT_RECORD) that reaches the cup's first team-order "
                                    "screen, with --games; its save: --save")
    ap.add_argument("--save", help="the Stadium save the route starts from (each game uses a copy)")
    ap.add_argument("--show", type=int, default=0, help="with --games: show the windows of the first n games")
    ap.add_argument("--no-headless", action="store_true",
                    help="with --games: hidden games draw every frame (PORT_HEADLESS=0), to compare their pictures")
    ap.add_argument("--castle", action="store_true",
                    help="Gym Leader Castle from the start: the bot goes there from the title screen, rents --team "
                         "and plays through it (with --games; no --route needed)")
    ap.add_argument("--room", choices=sorted(CASTLE_WAY),
                    help="with --castle: go straight to this room on the castle's map (elite4: needs a save with all "
                         "eight badges, --save)")
    ap.add_argument("--lookahead", type=int, default=LOOKAHEAD_DEPTH,
                    help="turns the battle is played out ahead for each decision (default %d; 0: one-turn estimate)" %
                         LOOKAHEAD_DEPTH)
    ap.add_argument("--explore", type=int, default=1,
                    help="with --team draft and --games: this many games (the last ones) draft an experimental team: "
                         "a wider, looser shortlist that avoids what the other games took (default 1)")
    ap.add_argument("--styles", default=",".join(DEFAULT_STYLES),
                    help="with --team draft: the games' team styles in turn, from %s (default %s; "
                         "balanced: every game alike)" % (", ".join(STYLE_ORDER), ",".join(DEFAULT_STYLES)))
    ap.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 2),
                    help="worker processes for the lookahead's searches (default: all cores but two; 0: none)")
    ap.add_argument("--max-e4-runs", type=int, default=5,
                    help="with --castle: stop a game after this many failed runs at the Elite Four (default 5; 0: never)")
    ap.add_argument("--leader-lookahead", type=int, default=LEADER_DEPTH,
                    help="with --castle: turns looked ahead in gym leader and Elite Four battles (default %d)" % LEADER_DEPTH)
    ap.add_argument("--leaders-only", action="store_true",
                    help="with --castle: only the gym leaders (PORT_CASTLE_LEADERS_ONLY=1; then the Elite Four and the "
                         "champion as usual: 13 battles in all, so use --wins 13)")
    ap.add_argument("--gb-rom", default=os.environ.get("PORT_GB_ROM"),
                    help="a Game Boy Pokemon ROM for the level-up learnsets (default $PORT_GB_ROM, else gb/*.gb)")
    ap.add_argument("--round2", action="store_true", help="play Round 2 (PORT_ROUND2=1; the save is not changed)")
    ap.add_argument("--team", help="six Pokemon to rent, e.g. Mewtwo,Alakazam,Snorlax,Starmie,Zapdos,Rhydon, or draft: Laya "
                                       "drafts a well-rounded team from the rental list (different every run); "
                                       "teams separated by ';' go to the games in turn")
    ap.add_argument("--exe", default=os.path.join(ROOT, "build", "pokestadium"), help="the game binary")
    ap.add_argument("--register", action="store_true",
                    help="with --team: register the team after the first rental round (saved in the game's save)")
    ap.add_argument("--random-team", action="store_true",
                    help="choose a random rental team on the rental carousel (the route ends before it)")
    args = ap.parse_args()
    LOOKAHEAD_DEPTH = args.lookahead
    LEADER_DEPTH = args.leader_lookahead if LOOKAHEAD_DEPTH else 0
    global SEARCH_POOL
    if LOOKAHEAD_DEPTH and args.workers > 0:
        from concurrent.futures import ProcessPoolExecutor
        SEARCH_POOL = ProcessPoolExecutor(args.workers, initializer=search_init, initargs=(LOOKAHEAD_DEPTH, LEADER_DEPTH))
    picks = args.pick if args.pick in ("random", "smart") else [int(x) - 1 for x in args.pick.split(",")]
    global ALLOWED
    import glob
    gb_rom = args.gb_rom or next(iter(sorted(glob.glob(os.path.join(ROOT, "gb", "*.gb")))), None)
    ALLOWED = load_learnsets(gb_rom)
    print("learnsets: %s" % ("level-up moves from " + os.path.basename(gb_rom) if gb_rom else
                             "TM/HM and starting moves only (no Game Boy ROM)"), flush=True)

    status = Status()
    url = serve_status(status, args.port, args.rom)
    print("status page: " + url, flush=True)
    if args.browser:
        webbrowser.open(url)
    brain = Brain(args.model)
    procs, threads = [], []
    exe = os.path.abspath(args.exe)

    import tempfile
    thumbs = tempfile.mkdtemp(prefix="autoplay_thumbs_")  # each game's latest picture for the status page

    def start(index, sock, env, cmd, armed, jitter, name):
        gs = status.add_game(name)
        explore = team == "draft" and index >= args.games - args.explore  # the last games of a run draft boldly
        styles = [x.strip() for x in args.styles.split(",") if x.strip() in STYLES] or ["balanced"]
        style = styles[index % len(styles)]
        gs.set(thumb=os.path.join(thumbs, "game%d.ppm" % index), goal=args.wins, explore=explore, style=style)
        proc = []

        def body():
            try:
                game = Game(sock)
                gs.log("connected")
                import random
                own = team[index % len(team)] if per_game else team  # (--team "six;six;...": a team per game)
                if per_game:
                    gs.log("fixed team: %s" % ", ".join(SPECIES.get(t, "?") for t in own))
                run_game(game, brain, gs, picks, args.wins, armed, jitter, random.Random(), args.random_team, own,
                         route_end, args.register, args.room, args.max_losses, explore, index, args.castle,
                         args.leaders_only, args.max_tries, args.max_e4_runs)
                if proc and proc[0].poll() is None:  # finished (won, or lost too often): close its game
                    proc[0].terminate()
            except (EOFError, BrokenPipeError, ConnectionResetError) as e:
                gs.log("the game was closed (%s)" % type(e).__name__)
                gs.set(phase="closed", done=True)
            except Exception as e:  # keep the other games going
                import traceback
                gs.log("stopped: %s: %s" % (type(e).__name__, e))
                traceback.print_exc()
                gs.set(phase="error: %s" % e, done=True)
        if cmd is not None:
            log = open(env["PORT_GAME_LOG"], "w") if env.get("PORT_GAME_LOG") else subprocess.DEVNULL
            proc.append(subprocess.Popen(cmd, cwd=os.path.dirname(exe), env=env, stdout=log,
                                         stderr=subprocess.STDOUT))
            procs.append(proc[0])
        t = threading.Thread(target=body, daemon=True)
        t.start()
        threads.append(t)

    try:
        team, per_game = None, False
        if args.team == "draft":
            team = "draft"  # Laya drafts it from the rental list when the carousel opens (draft_team)
        elif args.team and ";" in args.team:  # a team per game, in turn (the teams that cleared the castle, side by side)
            team, per_game = [[species_id(n) for n in t.split(",")] for t in args.team.split(";") if t.strip()], True
        elif args.team:
            team = [species_id(n) for n in args.team.split(",")]
        if args.castle:
            if not args.rom or not team:
                sys.exit("--castle needs --rom and --team")
            import tempfile
            args.route = os.path.join(tempfile.mkdtemp(prefix="autoplay_route_"), "castle.rec")
            castle_route(args.route)
            args.games = args.games or 1
            args.save = args.save or os.path.join(ROOT, "replays", "default.flash")
        route_end = 0
        if args.route:
            for line in open(args.route):
                m = re.match(r"# end (\d+)", line)
                if m:
                    route_end = int(m.group(1))
        if args.games > 0:
            if not args.route or not args.rom:
                sys.exit("--games needs --route (a recording to the first team-order screen) and --rom")
            import random, shutil, tempfile
            work = tempfile.mkdtemp(prefix="autoplay_")
            print("game logs, saves and recordings: " + work, flush=True)
            rng = random.Random()
            for i in range(args.games):
                sock = "/tmp/psbot_%d_%d.sock" % (os.getpid(), i)  # (other runs may be going)
                save = os.path.join(work, "game%d.flash" % i)
                if args.save:
                    shutil.copyfile(args.save, save)
                env = dict(os.environ, PORT_BOT=sock, PORT_SPEED=args.speed or "max", PORT_REPLAY=os.path.abspath(args.route),
                           PORT_FLASH_FILE=save, PORT_RECORD=os.path.join(work, "game%d.rec" % i), PORT_SOUND="0",
                           PORT_GAME_LOG=os.path.join(work, "game%d.log" % i))
                env.update(PORT_FREEZE_CHECK="0", PORT_AUDIO_TASKS="0")  # nobody listens; the bot detects stalls itself
                if args.round2:
                    env["PORT_ROUND2"] = "1"
                if args.leaders_only:
                    env["PORT_CASTLE_LEADERS_ONLY"] = "1"
                if i >= args.show:
                    # nobody sees these: draw only every 60th frame and skip the freeze check (a GPU read-back per
                    # frame), which doubles what 20 games manage together
                    env.update(PORT_HIDDEN_WINDOW="1", PORT_RENDER_EVERY="60", PORT_FREEZE_CHECK="0", PORT_HEADLESS="1")
                    if args.no_headless:  # every frame drawn, as in a window (to check what headless mode shows)
                        env.update(PORT_RENDER_EVERY="1", PORT_HEADLESS="0")
                start(i, sock, env, [exe, args.rom], True, rng.randint(1, 240),
                      "game %d%s" % (i + 1, " (experimental)" if team == "draft" and i >= args.games - args.explore else ""))
        else:
            env = dict(os.environ, PORT_BOT=SOCK, PORT_SPEED=args.speed or "1")
            cmd = None if args.attach else [exe] + ([args.rom] if args.rom else [])
            start(0, SOCK, env, cmd, args.armed, 0, "game")
        while any(t.is_alive() for t in threads):
            time.sleep(1)
        total = sum(g["wins"] for g in status.games), sum(g["losses"] for g in status.games)
        print("all games finished: %d battles won, %d lost" % total, flush=True)
        time.sleep(3)
    except KeyboardInterrupt:
        pass
    finally:
        for p in procs:
            if p.poll() is None:
                p.terminate()
        import shutil
        shutil.rmtree(thumbs, ignore_errors=True)


if __name__ == "__main__":
    main()
