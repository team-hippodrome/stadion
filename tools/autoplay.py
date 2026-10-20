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

A status window (a local web page, opened in your browser) shows both Pokemon, the options with Laya's probabilities
and every decision. --no-browser leaves it closed (http://127.0.0.1:8777/), --model picks another Laya checkpoint.
The bot holds controller 1 only when it acts; you can still press buttons yourself.
"""
import argparse, json, math, os, re, socket, subprocess, sys, threading, time, webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCK = "/tmp/pokestadium_bot.sock"  # unix socket paths must be short

# N64 controller bits
A, B, START, L, R, CU, CD, CL, CR = 0x8000, 0x4000, 0x1000, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1
DUP, DDOWN, DLEFT, DRIGHT = 0x0800, 0x0400, 0x0200, 0x0100
STALL_FRAMES = 20000  # no decision, battle or team screen for this long (5.5 minutes of game time): stuck
MOVE_BUTTON = [CU, CR, CD, CL]          # move slots 1-4 in battle (BattleScene_SelectMoveFromButtons)
PARTY3_BUTTON = [CU, CR, CD]            # party slots with three Pokemon (BattleScene_HandlePartySlotCursorInput)
PARTY6_BUTTON = [CU, CL, B, CR, CD, A]  # party slots with more
ORDER_BUTTON = [B, CL, CU, A, CD, CR]   # team-order screen slots 1-6 (BattleMenu_HandleOrderSelectPanel_PartyInput)

# Laya decides together with a simple estimate of each option (Laya alone is close to chance on unfamiliar decisions:
# it repeated weak moves and switched back and forth). The final choice averages both, as probabilities.
# Laya's share is kept below the estimate's: at 0.5, Laya's vote alone carried a switch the estimate put 114 points
# below attacking (the newcomer took the hit it predicted)
LAYA_WEIGHT = 0.3
PRIOR_TEMPERATURE = 8.0   # estimate units (about expected damage) per factor e in the prior's odds
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
    return species, moves, movedata


SPECIES, MOVES, MOVEDATA = load_tables()


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
    return {"species": SPECIES.get(b[0x0B], "#%d" % b[0x0B]), "hp": u16(b, 0x0C), "max_hp": u16(b, 0x28),
            "level": b[0x26], "types": [TYPES.get(b[0x1C], "?"), TYPES.get(b[0x1D], "?")],
            "moves": [(b[0x1F + i], b[0x32 + i] & 0x3F) for i in range(4)], "status": b[0x15],
            "atk": u16(b, 0x2A), "def": u16(b, 0x2C), "spd": u16(b, 0x2E), "spc": u16(b, 0x30)}


def party_mon(hexstr):
    """BattleMon (game/src/game_state.h): a party member"""
    b = bytes.fromhex(hexstr)
    # the species is the first byte (a registered team keeps a flag in the second)
    return {"species": SPECIES.get(b[0], "#%d" % b[0]), "hp": u16(b, 0x02), "max_hp": u16(b, 0x26),
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
        self.data = {"name": name, "phase": "starting", "own": None, "opp": None, "bench": [], "options": [],
                     "log": [], "wins": 0, "losses": 0, "battles": 0, "done": False}

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


PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>Autoplay</title><style>
:root{--bg:#10141b;--card:#1a2130;--text:#e6e9ef;--dim:#8b93a3;--accent:#4c8dff;--good:#2ea043;--bad:#e5484d}
body{margin:0;background:var(--bg);color:var(--text);font:14px/1.4 -apple-system,system-ui,sans-serif}
main{max-width:1100px;margin:0 auto;padding:16px;display:grid;gap:12px;grid-template-columns:1fr 1fr}
section{background:var(--card);border-radius:10px;padding:12px 14px;min-width:0}
h2{margin:0 0 8px;font-size:13px;color:var(--dim);text-transform:uppercase;letter-spacing:.05em}
.wide{grid-column:1/-1}.mon{font-size:18px;font-weight:600}.dim{color:var(--dim)}
.bar{height:8px;background:#2a3344;border-radius:4px;overflow:hidden;margin:6px 0}.bar i{display:block;height:100%}
table{width:100%;border-collapse:collapse}td,th{padding:3px 6px;text-align:left}th{color:var(--dim);font-weight:500}
td.p{width:35%}.pb{height:10px;background:var(--accent);border-radius:3px}.pick td{color:#fff;font-weight:600}
#games tr{cursor:pointer}#games tr.sel td{background:#243049}.won{color:var(--good)}.lost{color:var(--bad)}
#log{max-height:320px;overflow:auto;font:12px/1.5 ui-monospace,monospace;white-space:pre-wrap}
@media(max-width:700px){main{grid-template-columns:1fr}}
</style></head><body><main>
<section class="wide"><h2>Games</h2><div id="sum" class="dim"></div><table id="games"></table></section>
<section><h2>Your Pokemon</h2><div id="own"></div></section>
<section><h2>Opponent</h2><div id="opp"></div></section>
<section class="wide"><h2>Last decision</h2><table id="opts"></table></section>
<section class="wide"><h2>Bench</h2><div id="bench"></div></section>
<section class="wide"><h2>Decisions</h2><div id="log"></div></section>
</main><script>
let sel=0;function esc(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
function mon(m){if(!m)return '<span class="dim">-</span>';const p=m.max_hp?Math.round(100*m.hp/m.max_hp):0;
return `<div class="mon">${esc(m.species)} <span class="dim">L${m.level}</span></div><div class="dim">${esc(m.types)}</div>
<div class="bar"><i style="width:${p}%;background:${p<25?'var(--bad)':'var(--good)'}"></i></div><div>${m.hp} / ${m.max_hp} HP</div>
${(m.moves||[]).map(x=>`<div class="dim">${esc(x)}</div>`).join('')}`}
async function tick(){try{const d=await (await fetch('state.json',{cache:'no-store'})).json();const gs=d.games||[];
const w=gs.reduce((a,g)=>a+g.wins,0),l=gs.reduce((a,g)=>a+g.losses,0),done=gs.filter(g=>g.done).length;
document.getElementById('sum').textContent=`${gs.length} games · ${done} finished · battles won ${w} · lost ${l}`;
document.getElementById('games').innerHTML='<tr><th>game</th><th>phase</th><th>won</th><th>lost</th><th>last decision</th></tr>'+
gs.map((g,i)=>`<tr class="${i==sel?'sel':''}" onclick="sel=${i};tick()"><td>${esc(g.name)}</td><td>${esc(g.phase)}</td>
<td class="won">${g.wins}</td><td class="lost">${g.losses}</td><td class="dim">${esc((g.log[0]||'').slice(9))}</td></tr>`).join('');
const g=gs[sel]||{};document.getElementById('own').innerHTML=mon(g.own);document.getElementById('opp').innerHTML=mon(g.opp);
document.getElementById('opts').innerHTML='<tr><th>option</th><th>final</th><th></th><th>Laya</th><th>estimate</th></tr>'+
(g.options||[]).map(o=>`<tr class="${o.picked?'pick':''}"><td>${o.picked?'▶ ':''}${esc(o.name)}</td>
<td class="p"><div class="pb" style="width:${Math.round(o.p*100)}%"></div></td><td>${(o.p*100).toFixed(1)}%</td>
<td>${(o.laya*100).toFixed(1)}%</td><td>${(o.prior*100).toFixed(1)}%</td></tr>`).join('');
document.getElementById('bench').innerHTML=(g.bench||[]).map(b=>`<div>${esc(b)}</div>`).join('')||'<span class="dim">-</span>';
document.getElementById('log').textContent=(g.log||[]).join('\\n')}catch(e){}}
tick();setInterval(tick,700)</script></body></html>"""


def serve_status(status, port):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            body = status.json().encode() if self.path.startswith("/state.json") else PAGE.encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json" if self.path.startswith("/state.json") else "text/html")
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
ASLEEP = 0x07  # the status byte's sleep counter (Game Boy encoding)
PRIORITY = {98}  # Quick Attack


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
    base = ((2 * att["level"] / 5 + 2) * d["power"] * max(1, a) / max(1, b)) / 50 + 2
    stab = 1.5 if d["type"] in att["types"] else 1.0
    return base * stab * eff * 0.925


def threat(att, dfn):
    """The share of dfn's current HP att's best move takes (1.0: a knock-out)"""
    best = 0.0
    for mid, pp in att["moves"]:
        if mid and pp > 0:
            d = MOVEDATA.get(mid, {"accuracy": 100})
            best = max(best, damage(att, dfn, mid) * d["accuracy"] / 100.0)
    return min(1.5, best / max(1, dfn["hp"]))


def faster(a, b):
    return a["spd"] > b["spd"] or (a["spd"] == b["spd"] and False)


def move_value(own, opp, mid):
    """How much a move does for winning, about 0-130 (100: takes the opponent's remaining HP)"""
    d = MOVEDATA.get(mid)
    if d is None:
        return 0.0
    acc = d["accuracy"] / 100.0
    hp_frac = own["hp"] / max(1, own["max_hp"])
    opp_status = opp.get("status", 0)
    if d["power"] == 0 and MOVES.get(mid) not in ("Night Shade",):  # status moves
        e = d["effect"]
        if e in (SLEEP, PARALYZE, POISON) and threat(opp, own) >= 1.0:
            # the next hit knocks us out: no time to profit from it (Drowzee at 22 HP chose Hypnosis)
            return 5.0
        if e == SLEEP:
            return 75 * acc if opp_status == 0 else 0.0
        if e == PARALYZE:
            if opp_status != 0 or (d["type"] == "Electric" and "Ground" in opp["types"]):
                return 0.0
            return (55 if faster(opp, own) else 40) * acc
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


TRAP_PENALTY = 50.0  # a faster opponent with Wrap, Bind, Clamp or Fire Spin can keep a Pokemon from acting at all


def trapped_by(opp, mon):
    return faster(opp, mon) and any(MOVEDATA.get(mid, {}).get("effect") == TRAP for mid, pp in opp["moves"] if mid and pp > 0)


def offense(mon, opp):
    return max([move_value(mon, opp, mid) for mid, pp in mon["moves"] if mid and pp > 0] or [0.0])


def matchup(mon, opp):
    """How well a Pokemon stands against the opponent: its best move's value minus the opponent's threat to it"""
    return (offense(mon, opp) - 100 * min(1.0, threat(opp, mon)) * (1.0 if faster(opp, mon) else 0.7) -
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


def mon_view(m):
    return {"species": m["species"], "level": m["level"], "hp": m["hp"], "max_hp": m["max_hp"],
            "types": types_text(m["types"]),
            "moves": ["%s (%d PP)" % (MOVES.get(mid, "?"), pp) for mid, pp in m["moves"] if mid]}


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


def play_turn(game, brain, status, choice):
    LAST_SWITCH = status.data.setdefault("last_switch", {})
    own, opp = runtime_mon(choice["own"]), runtime_mon(choice["opp"])
    party = [party_mon(p) for p in choice["party"]]
    active = choice["active"]
    status.set(own=mon_view(own), opp=mon_view(opp),
               bench=["%s %d/%d HP%s" % (p["species"], p["hp"], p["max_hp"], " (in battle)" if i == active else "")
                      for i, p in enumerate(party)])
    state = battle_state(own, opp, party, active)
    slot_buttons = PARTY3_BUTTON if len(party) <= 3 else PARTY6_BUTTON
    bench = [i for i, p in enumerate(party) if i != active and p["hp"] > 0]

    if choice["forced"]:
        if not bench:
            return
        opts = [(party[i]["species"], "%s, %s, %d/%d HP" % (party[i]["species"], types_text(party[i]["types"]),
                                                             party[i]["hp"], party[i]["max_hp"])) for i in bench]
        values = [matchup(party[i], opp) for i in bench]
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
    # voluntary switches only from a losing matchup, and not again soon after one (no switching back and forth)
    may_switch = here < SWITCH_WHEN_BELOW and LAST_SWITCH.get("age", 99) >= SWITCH_COOLDOWN
    for i in (bench if may_switch else []):
        if party[i]["species"] == LAST_SWITCH.get("from"):
            continue  # never straight back to the one just switched out
        name = "switch to " + party[i]["species"]
        opts.append((name, "%s: %s, %d/%d HP" % (name, types_text(party[i]["types"]), party[i]["hp"], party[i]["max_hp"])))
        actions.append(("switch", i))
        # the newcomer takes the opponent's next hit before it acts
        incoming = 100 * min(1.0, threat(opp, party[i]))
        values.append(offense(party[i], opp) - incoming - SWITCH_COST + max(0.0, -here) -
                      (TRAP_PENALTY if trapped_by(opp, party[i]) else 0.0))
    LAST_SWITCH["age"] = LAST_SWITCH.get("age", 99) + 1
    st = lambda m: "" if m["status"] == 0 else " status %#x" % m["status"]
    status.trace("%s %d/%d%s spd %d vs %s %d/%d%s spd %d (moves %s) | %s" % (
        own["species"], own["hp"], own["max_hp"], st(own), own["spd"], opp["species"], opp["hp"], opp["max_hp"], st(opp),
        opp["spd"], ", ".join(MOVES.get(mid, "?") for mid, _ in opp["moves"] if mid),
        ", ".join("%s %.0f" % (o[0], v) for o, v in zip(opts, values))))
    if not opts:  # no PP left anywhere: the game uses Struggle on its own
        game.press(A)
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


def draft_team(brain, status, roster, rng, size=6, shortlist=6):
    """A rental team built one Pokemon at a time: each step offers Laya a random shortlist of strong candidates (a
    different one every run), scored by overall strength (the battle estimate against the whole rental list), the
    roster's Pokemon the team newly hits super effectively, weaknesses the team already has and types it already has
    (a first draft took three Psychic types). -> species ids"""
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
    strength = {m["id"]: sum(matchup(m, o) for o in mons) / len(mons) for m in mons}
    ranked = sorted(mons, key=lambda m: -strength[m["id"]])
    team = []

    def hits(m):  # the roster's Pokemon one of m's damaging moves hits super effectively
        types = {MOVEDATA[mid]["type"] for mid, _ in m["moves"] if mid in MOVEDATA and MOVEDATA[mid]["power"] > 0}
        return {o["id"] for o in mons if any(effectiveness(t, o["types"]) > 1 for t in types)}

    while len(team) < size:
        pool = [m for m in ranked[:40] if m not in team]  # the strong part of the list
        cands = rng.sample(pool, min(shortlist, len(pool)))
        covered = set().union(*[hits(m) for m in team]) if team else set()
        weak = [w for m in team for w in weaknesses(m)]
        have = [t for m in team for t in set(m["types"])]
        values, opts = [], []
        for m in cands:
            shared = sum(weak.count(w) for w in weaknesses(m))
            same = sum(have.count(t) for t in set(m["types"]))
            values.append(strength[m["id"]] + 0.4 * len(hits(m) - covered) - 12 * shared - 20 * same)
            opts.append((m["species"], "%s (%s), level %d: %s; weak to %s" % (
                m["species"], types_text(m["types"]), m["level"],
                ", ".join(MOVES.get(mid, "?") for mid, _ in m["moves"] if mid),
                ", ".join(sorted(weaknesses(m))) or "nothing")))
        state = {"team_so_far": [{"name": m["species"], "types": types_text(m["types"])} for m in team],
                 "team_weaknesses": sorted(set(weak)), "picks_left": size - len(team)}
        k, lp, fp = brain.decide(status, "Which Pokemon makes this team the most well rounded and strongest for a long "
                                 "series of battles against varied opponents?", state, opts, values)
        status.trace("draft scores: " + ", ".join("%s %.0f (strength %.0f)" % (m["species"], v, strength[m["id"]])
                                                  for m, v in zip(cands, values)))
        team.append(cands[k])
        status.log("draft: %s (Laya %.0f%%, final %.0f%%) from %s" % (
            cands[k]["species"], lp * 100, fp * 100, ", ".join(m["species"] for m in cands)))
    return [m["id"] for m in team]


def choose_team(brain, status, order):
    """The three Pokemon to send in against the opponent's team, best first: each one's matchups against the
    opponent's six (the battle estimate), averaged with Laya's view. -> slots (0-5)"""
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
    # the opponent sends in three of its six, likely the ones that beat ours: the worst three matchups count as much
    # as the average (an Electric type's easy win against Golbat hid three Ground types)
    values = []
    for m in own:
        each = sorted(matchup(m, o) for o in opp)
        values.append(0.5 * sum(each) / len(each) + 0.5 * sum(each[:3]) / len(each[:3]))
    state = {"opponent_team": [{"name": o["species"], "level": o["level"], "types": types_text(o["types"])} for o in opp],
             "your_team": [{"name": m["species"], "level": m["level"], "types": types_text(m["types"])} for m in own]}
    opts = [(m["species"], "%s (%s): %s" % (m["species"], types_text(m["types"]), ", ".join(
        MOVES.get(mid, "?") for mid, _ in m["moves"] if mid))) for m in own]
    k, lp, fp = brain.decide(status, "Which Pokemon is strongest against the opponent's team (%s)?" %
                             ", ".join(o["species"] for o in opp), state, opts, values)
    final = {o["name"]: o["p"] for o in status.get("options")}
    order_slots = sorted(range(len(own)), key=lambda i: -final.get(own[i]["species"], 0))
    picks = order_slots[:3]
    status.log("team vs %s: %s" % (", ".join(o["species"] for o in opp),
                                   ", ".join(own[i]["species"] for i in picks)))
    return picks


def run_game(game, brain, status, pick_spec, wins, armed, jitter, rng, random_team=False, team=None, route_end=0,
             register=False, room=None):
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
    while status.get("wins") < wins:
        st = game.state()
        now = st["tick"]
        choice, order = st.get("choice"), st.get("order")
        if choice and choice["age"] <= 2:
            last_choice = choice
        if now - progress_tick > STALL_FRAMES and armed:
            status.log("stuck: nothing happened for %d frames (frame %d)" % (STALL_FRAMES, now))
            status.set(phase="stuck at frame %d" % now, done=True)
            return
        if team == "draft" and st.get("rental_age", 99) <= 2:  # the carousel is up: draft from the rental list
            roster = game.roster()
            if not roster:
                game.wait(10)
                continue
            team = draft_team(brain, status, roster, rng)
            if team is None:
                status.log("could not read the rental list: stopped")
                status.set(phase="stopped: no rental list", done=True)
                return
            status.log("drafted team: %s" % ", ".join(SPECIES.get(t, "?") for t in team))
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
            status.set(battles=status.get("battles") + 1)
            if won:
                status.set(wins=status.get("wins") + 1)
                status.log("battle won (%d/%d)" % (status.get("wins"), wins))
            else:
                status.set(losses=status.get("losses") + 1)
                status.log("battle lost%s, continuing" % (" (draw)" if result == 3 else ""))
            status.set(own=None, opp=None, options=[], bench=[], last_switch={})
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
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pick", default="smart",
                    help="which three to send in: smart (default: Laya and the estimate against the opponent's team), "
                         "random, or team-order slots in order, e.g. 1,2,3")
    ap.add_argument("--wins", type=int, default=8, help="battles to win before stopping (default 8, a cup)")
    ap.add_argument("--speed", default=None, help="game speed (PORT_SPEED: 2, max, ...; default 1, max with --games)")
    ap.add_argument("--rom", help="Stadium ROM (default: the launcher's choice; the launcher shows first)")
    ap.add_argument("--model", default="convaiinnovations/laya", help="Laya checkpoint")
    ap.add_argument("--port", type=int, default=8777, help="status window port")
    ap.add_argument("--no-browser", action="store_true", help="do not open the status window")
    ap.add_argument("--attach", action="store_true", help="use a game already started with PORT_BOT=" + SOCK)
    ap.add_argument("--armed", action="store_true", help="play right away (e.g. attached in the middle of a cup)")
    ap.add_argument("--games", type=int, default=0,
                    help="run this many hidden games at once from --route (the cup and team picked by the route)")
    ap.add_argument("--route", help="an input recording (tools/... PORT_RECORD) that reaches the cup's first team-order "
                                    "screen, with --games; its save: --save")
    ap.add_argument("--save", help="the Stadium save the route starts from (each game uses a copy)")
    ap.add_argument("--show", type=int, default=0, help="with --games: show the windows of the first n games")
    ap.add_argument("--castle", action="store_true",
                    help="Gym Leader Castle from the start: the bot goes there from the title screen, rents --team "
                         "and plays through it (with --games; no --route needed)")
    ap.add_argument("--room", choices=sorted(CASTLE_WAY),
                    help="with --castle: go straight to this room on the castle's map (elite4: needs a save with all "
                         "eight badges, --save)")
    ap.add_argument("--round2", action="store_true", help="play Round 2 (PORT_ROUND2=1; the save is not changed)")
    ap.add_argument("--team", help="six Pokemon to rent, e.g. Mewtwo,Alakazam,Snorlax,Starmie,Zapdos,Rhydon, or draft: Laya "
                                       "drafts a well-rounded team from the rental list (different every run)")
    ap.add_argument("--exe", default=os.path.join(ROOT, "build", "pokestadium"), help="the game binary")
    ap.add_argument("--register", action="store_true",
                    help="with --team: register the team after the first rental round (saved in the game's save)")
    ap.add_argument("--random-team", action="store_true",
                    help="choose a random rental team on the rental carousel (the route ends before it)")
    args = ap.parse_args()
    picks = args.pick if args.pick in ("random", "smart") else [int(x) - 1 for x in args.pick.split(",")]

    status = Status()
    url = serve_status(status, args.port)
    print("status window: " + url, flush=True)
    if not args.no_browser:
        webbrowser.open(url)
    brain = Brain(args.model)
    procs, threads = [], []
    exe = os.path.abspath(args.exe)

    def start(index, sock, env, cmd, armed, jitter, name):
        gs = status.add_game(name)

        def body():
            try:
                game = Game(sock)
                gs.log("connected")
                import random
                run_game(game, brain, gs, picks, args.wins, armed, jitter, random.Random(), args.random_team, team,
                         route_end, args.register, args.room)
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
            procs.append(subprocess.Popen(cmd, cwd=os.path.dirname(exe), env=env, stdout=log,
                                          stderr=subprocess.STDOUT))
        t = threading.Thread(target=body, daemon=True)
        t.start()
        threads.append(t)

    try:
        team = None
        if args.team == "draft":
            team = "draft"  # Laya drafts it from the rental list when the carousel opens (draft_team)
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
                if i >= args.show:
                    # nobody sees these: draw only every 60th frame and skip the freeze check (a GPU read-back per
                    # frame), which doubles what 20 games manage together
                    env.update(PORT_HIDDEN_WINDOW="1", PORT_RENDER_EVERY="60", PORT_FREEZE_CHECK="0")
                start(i, sock, env, [exe, args.rom], True, rng.randint(1, 240), "game %d" % (i + 1))
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


if __name__ == "__main__":
    main()
