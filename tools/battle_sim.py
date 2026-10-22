#!/usr/bin/env python3
"""Pokemon Stadium battles played in code, to test the autoplay bot's decisions without the game.

The battles follow the Game Boy rules the game uses: the damage roll (217-255), critical hits from base speed
(autoplay.crit_chance), the 1/256 miss, stat stages, sleep (1-7 turns), paralysis, freeze, burn, poison and Toxic,
confusion, flinching, trapping moves, two-turn moves, Hyper Beam's recharge, Explosion, recoil, draining, Leech Seed,
Reflect and Light Screen. Our side decides with the bot's own code (autoplay.choose_team, autoplay.decide_turn; Laya
replaced by the estimate it nearly always follows). The opponent picks its moves as the castle's computer scores
them (autoplay.ai_scores: the best, now and then the second best) and sends in its best answer after a faint.

The leaders' teams (species and the moves they used) come from autoplay logs; their stats are the rentals' plus
autoplay.TRAINER_BONUS. Left out: Substitute, Bide, Counter's exact timing, Mimic, Metronome, Transform, Disable,
the computer's voluntary switches. Compare with real batches before trusting a result (--calibrate).

  build/laya-venv/bin/python tools/battle_sim.py --logs LOG... --team "Jynx,Gengar,..." --battles 40
"""
import argparse, collections, math, multiprocessing, os, random, re, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import autoplay as ap  # noqa: E402

LEADERS = ["Brock", "Misty", "Surge", "Erika", "Koga", "Sabrina", "Blaine", "Giovanni",
           "Lorelei", "Bruno", "Agatha", "Lance", "Rival"]
STAGES = ("atk", "def", "spd", "spc", "acc", "eva")
# move effects (the Game Boy's numbering, as gb_data.c and autoplay use it)
POISON1, DRAIN, BURN1, FREEZE1, PARA1, EXPLODE, DREAM = 2, 3, 4, 5, 6, 7, 8
UP1 = {10: "atk", 11: "def", 12: "spd", 13: "spc", 14: "acc", 15: "eva"}
DOWN1 = {18: "atk", 19: "def", 20: "spd", 21: "spc", 22: "acc", 23: "eva"}
UP2 = {50: "atk", 51: "def", 52: "spd", 53: "spc", 54: "acc", 55: "eva"}
DOWN2 = {58: "atk", 59: "def", 60: "spd", 61: "spc", 62: "acc", 63: "eva"}
DOWN_SIDE = {68: "atk", 69: "def", 70: "spd", 71: "spc"}
HAZE, MULTI, FLINCH1, SLEEP, POISON2, BURN2, PARA2, FLINCH2 = 25, 29, 31, 32, 33, 34, 36, 37
OHKO, CHARGE, SUPER_FANG, SPECIAL, TRAP, FLY, TWICE, JUMP_KICK = 38, 39, 40, 41, 42, 43, 44, 45
FOCUS, RECOIL, CONFUSE, HEAL, LIGHT_SCREEN, REFLECT, POISON, PARALYZE = 47, 48, 49, 56, 64, 65, 66, 67
CONFUSE_SIDE, TWINEEDLE, HYPER_BEAM, LEECH = 76, 77, 80, 84
PSN, BRN, FRZ, PAR = 0x08, 0x10, 0x20, 0x40


class Mon:
    def __init__(self, base):
        self.base = base
        self.species, self.id, self.level, self.types = base["species"], base["id"], base["level"], base["types"]
        self.max_hp = base["max_hp"]
        self.hp = base["max_hp"]
        self.raw = {k: base[k] for k in ("atk", "def", "spd", "spc")}
        self.moves = [[mid, pp if pp else ap.MOVEDATA.get(mid, {}).get("pp", 10)] for mid, pp in base["moves"] if mid]
        self.sleep = 0
        self.status = 0  # PSN, BRN, FRZ, PAR (sleep counted apart)
        self.toxic = 0
        self.fresh()

    def fresh(self):  # what a switch clears
        self.stage = dict.fromkeys(STAGES, 7)
        self.conf = self.trapped = self.trapping = 0
        self.trap_move = self.charging = None
        self.recharge = self.invuln = self.seeded = self.reflect = self.lscreen = self.focus = False
        self.trap_damage = 0
        if self.toxic:
            self.toxic = 1  # Toxic goes back to plain poison counting on a switch (Game Boy: stays bad poison, n reset)

    def stat(self, k):
        num, den = ap.STAGE_MULTIPLIER[self.stage[k]]
        v = max(1, self.raw[k] * num // den)
        if k == "spd" and self.status & PAR:
            v = max(1, v // 4)
        if k == "atk" and self.status & BRN:
            v = max(1, v // 2)
        return v

    def view(self):
        """the dict the bot's code reads (autoplay.runtime_mon's fields)"""
        st = (min(self.sleep, 7) if self.sleep else 0) | self.status | (PSN if self.toxic else 0)
        f1 = (0x10 if self.charging else 0) | (0x20 if self.trapping else 0) | (0x40 if self.invuln else 0) | \
             (0x80 if self.conf else 0)
        f2 = (0x80 if self.seeded else 0) | (0x04 if self.focus else 0)
        return {"species": self.species, "id": self.id, "hp": self.hp, "max_hp": self.max_hp, "level": self.level,
                "types": self.types, "moves": [(m, pp) for m, pp in self.moves], "status": st,
                "atk": self.stat("atk"), "def": self.stat("def"), "spd": self.stat("spd"), "spc": self.stat("spc"),
                "flags": (f1, f2), "acc": self.stage["acc"], "eva": self.stage["eva"]}

    def party(self):
        """a party member as autoplay.party_mon reads it (full stats, current HP and status)"""
        st = (min(self.sleep, 7) if self.sleep else 0) | self.status | (PSN if self.toxic else 0)
        return dict(self.base, hp=self.hp, status=st, moves=[(m, pp) for m, pp in self.moves])


class Battle:
    def __init__(self, ours, theirs, rng, log=None):
        self.side = [ours, theirs]  # lists of Mon
        self.active = [0, 0]
        self.rng = rng
        self.log = log
        self.turns = 0

    def say(self, text):
        if self.log is not None:
            self.log.append(text)

    def mon(self, s):
        return self.side[s][self.active[s]]

    def alive(self, s):
        return [i for i, m in enumerate(self.side[s]) if m.hp > 0]

    # ---- damage ------------------------------------------------------------------------------------------------
    def damage(self, att, dfn, mid, crit):
        d = ap.MOVEDATA[mid]
        name = ap.MOVES.get(mid, "")
        if name in ("Seismic Toss", "Night Shade"):
            return att.level
        if name == "Dragon Rage":
            return 40
        if name == "Sonicboom":
            return 20
        if name == "Psywave":
            return self.rng.randint(1, max(1, att.level * 3 // 2 - 1))
        if d["effect"] == SUPER_FANG:
            return max(1, dfn.hp // 2)
        eff = ap.effectiveness(d["type"], dfn.types)
        if eff == 0 or d["power"] == 0:
            return 0
        phys = d["type"] in ap.PHYSICAL
        ka, kb = ("atk", "def") if phys else ("spc", "spc")
        if crit:  # a critical hit counts the level twice and ignores stat stages, Reflect and Light Screen
            a, b, level = att.raw[ka], dfn.raw[kb], att.level * 2
        else:
            a, b, level = att.stat(ka), dfn.stat(kb), att.level
            if (phys and dfn.reflect) or (not phys and dfn.lscreen):
                b *= 2
        if d["effect"] == EXPLODE:
            b = max(1, b // 2)
        if a > 255 or b > 255:  # the Game Boy's one-byte stats
            a, b = max(1, a // 4), max(1, b // 4)
        base = ((2 * level // 5 + 2) * d["power"] * a // max(1, b)) // 50 + 2
        if d["type"] in att.types:
            base = base * 3 // 2
        base = int(base * eff)
        return max(1, base * self.rng.randint(217, 255) // 255)

    def hits(self, att, dfn, mid):
        if ap.MOVES.get(mid) == "Swift":
            return not dfn.invuln
        if dfn.invuln:
            return False
        p = ap.hit_chance(att.view(), dfn.view(), mid)
        return self.rng.random() < p

    # ---- one move ----------------------------------------------------------------------------------------------
    def use(self, s, mid):
        att, dfn = self.mon(s), self.mon(1 - s)
        if att.hp <= 0:
            return
        if att.recharge:
            att.recharge = False
            return
        if att.sleep:
            att.sleep -= 1
            self.say("%s is asleep" % att.species)
            return
        if att.status & FRZ:
            return
        if att.trapped:
            return  # held by the opponent's Wrap, Fire Spin and the like
        if getattr(att, "flinched", False):
            att.flinched = False
            return
        if att.status & PAR and self.rng.random() < 63 / 256:
            self.say("%s is fully paralysed" % att.species)
            return
        if att.conf:
            att.conf -= 1
            if self.rng.random() < 0.5:
                hurt = ((2 * att.level // 5 + 2) * 40 * att.stat("atk") // max(1, att.stat("def"))) // 50 + 2
                att.hp = max(0, att.hp - hurt)
                self.say("%s hurts itself in confusion" % att.species)
                return
        if att.trapping:  # keeps at its trapping move, no accuracy roll
            att.trapping -= 1
            dfn.hp = max(0, dfn.hp - att.trap_damage)
            if not att.trapping:
                dfn.trapped = 0
            return
        if att.charging:
            mid, att.charging, att.invuln = att.charging, None, False
        else:
            slot = next((m for m in att.moves if m[0] == mid), None)
            if slot is None or slot[1] <= 0:
                mid = 165  # Struggle
            else:
                slot[1] -= 1
            d = ap.MOVEDATA.get(mid, {"power": 50, "type": "Normal", "effect": 0, "accuracy": 100})
            if d["effect"] in (CHARGE, FLY):
                att.charging = mid
                att.invuln = ap.MOVES.get(mid) in ("Dig", "Fly")
                return
        d = ap.MOVEDATA.get(mid, {"power": 50, "type": "Normal", "effect": 0, "accuracy": 100})
        e, name = d["effect"], ap.MOVES.get(mid, "")
        self.say("%s uses %s" % (att.species, name))
        self_target = e in UP1 or e in UP2 or e in (HEAL, LIGHT_SCREEN, REFLECT, FOCUS, HAZE)
        if not self_target and not self.hits(att, dfn, mid):
            self.say("  it misses")
            if e == JUMP_KICK:
                att.hp = max(0, att.hp - 1)
            return
        if d["power"] > 0 or name in ("Seismic Toss", "Night Shade", "Dragon Rage", "Sonicboom", "Psywave", "Super Fang"):
            self.attack(att, dfn, mid, d, e, name)
        else:
            self.status_move(att, dfn, mid, d, e, name)

    def attack(self, att, dfn, mid, d, e, name):
        if e == OHKO:
            if att.stat("spd") < dfn.stat("spd"):
                return
            dfn.hp = 0
            return
        if e == DREAM and not dfn.sleep:
            return
        if ap.effectiveness(d["type"], dfn.types) == 0 and name not in ("Seismic Toss", "Night Shade", "Super Fang",
                                                                       "Dragon Rage", "Sonicboom", "Psywave"):
            return
        times = 1
        if e == MULTI:
            times = self.rng.choice((2, 2, 2, 3, 3, 3, 4, 5))
        elif e in (TWICE, TWINEEDLE):
            times = 2
        dealt = 0
        for _ in range(times):
            crit = d["power"] > 0 and self.rng.random() < ap.crit_chance(att.view(), mid)
            dmg = min(dfn.hp, self.damage(att, dfn, mid, crit))
            dfn.hp -= dmg
            dealt += dmg
            if dfn.hp <= 0:
                break
        self.say("  %d damage%s" % (dealt, "" if dfn.hp > 0 else ", %s faints" % dfn.species))
        if e == DRAIN or e == DREAM:
            att.hp = min(att.max_hp, att.hp + max(1, dealt // 2))
        elif e == RECOIL:
            att.hp = max(0, att.hp - max(1, dealt // 4))
        elif e == EXPLODE:
            att.hp = 0
        elif e == HYPER_BEAM and dfn.hp > 0:
            att.recharge = True
        elif e == TRAP and dfn.hp > 0:
            att.trapping = self.rng.choice((1, 1, 1, 2, 2, 2, 3, 4))  # turns after this one (2-5 in all)
            att.trap_damage = dealt
            dfn.trapped = att.trapping
        if dfn.hp <= 0:
            return
        chance = {POISON1: 0.2, BURN1: 0.1, FREEZE1: 0.1, PARA1: 0.1, POISON2: 0.4, BURN2: 0.3, PARA2: 0.3,
                  FLINCH1: 0.1, FLINCH2: 0.3, CONFUSE_SIDE: 0.1, TWINEEDLE: 0.2}.get(e)
        if e in DOWN_SIDE and self.rng.random() < 85 / 256:
            dfn.stage[DOWN_SIDE[e]] = max(1, dfn.stage[DOWN_SIDE[e]] - 1)
        if chance is None or self.rng.random() >= chance:
            return
        free = not dfn.sleep and not dfn.status
        if e in (FLINCH1, FLINCH2):
            dfn.flinched = True
        elif e == CONFUSE_SIDE:
            dfn.conf = dfn.conf or self.rng.randint(2, 5)
        elif free and e in (POISON1, POISON2, TWINEEDLE) and "Poison" not in dfn.types:
            dfn.status |= PSN
        elif free and e in (BURN1, BURN2) and "Fire" not in dfn.types:
            dfn.status |= BRN
        elif free and e == FREEZE1 and "Ice" not in dfn.types:
            dfn.status |= FRZ
        elif free and e in (PARA1, PARA2) and d["type"] not in dfn.types:
            dfn.status |= PAR
        if dfn.status & FRZ and d["type"] == "Fire":
            dfn.status &= ~FRZ

    def status_move(self, att, dfn, mid, d, e, name):
        free = not dfn.sleep and not dfn.status and not dfn.toxic
        if e == SLEEP and free:
            dfn.sleep = self.rng.randint(1, 7)
        elif e == POISON and free and "Poison" not in dfn.types:
            if name == "Toxic":
                dfn.toxic = 1
            else:
                dfn.status |= PSN
        elif e == PARALYZE and free and not (name == "Thunder Wave" and "Ground" in dfn.types):
            dfn.status |= PAR
        elif e == CONFUSE and not dfn.conf:
            dfn.conf = self.rng.randint(2, 5)
        elif e == LEECH and "Grass" not in dfn.types:
            dfn.seeded = True
        elif e in UP1 or e in UP2:
            k = (UP1.get(e) or UP2.get(e))
            att.stage[k] = min(13, att.stage[k] + (1 if e in UP1 else 2))
        elif e in DOWN1 or e in DOWN2:
            k = (DOWN1.get(e) or DOWN2.get(e))
            dfn.stage[k] = max(1, dfn.stage[k] - (1 if e in DOWN1 else 2))
        elif e == HEAL:
            if att.hp >= att.max_hp:
                return
            if name == "Rest":
                att.hp, att.sleep, att.status, att.toxic = att.max_hp, 2, 0, 0
            else:
                att.hp = min(att.max_hp, att.hp + att.max_hp // 2)
        elif e == LIGHT_SCREEN:
            att.lscreen = True
        elif e == REFLECT:
            att.reflect = True
        elif e == FOCUS:
            att.focus = True
        elif e == HAZE:
            for m in (att, dfn):
                m.stage = dict.fromkeys(STAGES, 7)
            dfn.status &= ~(PAR | BRN | FRZ)
            dfn.sleep = 0

    def end_of_turn(self):
        for s in (0, 1):
            m, o = self.mon(s), self.mon(1 - s)
            if m.hp <= 0:
                continue
            if m.toxic:
                m.hp = max(0, m.hp - max(1, m.max_hp * m.toxic // 16))
                m.toxic += 1
            elif m.status & (PSN | BRN):
                m.hp = max(0, m.hp - max(1, m.max_hp // 16))
            if m.seeded and m.hp > 0 and o.hp > 0:
                drain = min(m.hp, max(1, m.max_hp // 16))
                m.hp -= drain
                o.hp = min(o.max_hp, o.hp + drain)

    def switch(self, s, i):
        self.mon(s).fresh()
        self.mon(1 - s).trapping = 0
        self.mon(1 - s).trapped = 0
        self.active[s] = i
        self.mon(s).fresh()
        self.say("%s in %s" % ("we send" if s == 0 else "they send", self.mon(s).species))


# ---- the opponent: the castle's computer -----------------------------------------------------------------------
def aiv(m):  # a Pokemon as autoplay.ai_scores reads it (the lookahead's form)
    return ap.sim_mon(m.view())


def ai_move(battle):
    me, you = battle.mon(1), battle.mon(0)
    if me.charging or me.recharge or me.trapping:
        return None
    moves = [m for m, pp in me.moves if pp > 0]
    if not moves:
        return 165
    scores = ap.ai_scores(aiv(me), aiv(you), moves)
    order = sorted(range(len(moves)), key=lambda i: (-scores[i], battle.rng.random()))
    k = order[1] if len(order) > 1 and battle.rng.random() < ap.AI_SECOND else order[0]
    return moves[k]


def ai_send(battle):
    you = battle.mon(0)
    best, pick = None, None
    for i in battle.alive(1):
        m = battle.side[1][i]
        v = max(ap.ai_scores(aiv(m), aiv(you), [x for x, pp in m.moves if pp > 0]) or [0.0])
        if best is None or v > best:
            best, pick = v, i
    return pick


def ai_team(six, ours, rng):
    """three of its six and the lead: the ones its scoring likes best against our six"""
    def worth(o):
        return sum(max(ap.ai_scores(aiv(o), aiv(m), [x for x, _ in o.moves]) or [0.0]) for m in ours) - \
            sum(max(ap.ai_scores(aiv(m), aiv(o), [x for x, _ in m.moves]) or [0.0]) for m in ours)
    ranked = sorted(range(len(six)), key=lambda i: (-worth(six[i]), rng.random()))
    return ranked[:3]


# ---- our side: the bot's own code ------------------------------------------------------------------------------
class Brain:  # Laya replaced by the estimate (it follows it on nearly every turn)
    def decide(self, status, question, state, opts, values):
        k = max(range(len(values)), key=lambda i: values[i])
        return k, 1.0, 1.0


class Status:
    def __init__(self, style="balanced", log=None):
        self.data = {"style": style, "leader": True, "wins": 0, "losses": 0}
        self.lines = log

    def get(self, k):
        return self.data.get(k)

    def set(self, **kw):
        self.data.update(kw)

    def log(self, text):
        if self.lines is not None:
            self.lines.append(text)

    def trace(self, text):
        self.log("    " + text)


_party_mon = ap.party_mon


def _party_mon_dicts(h):  # choose_team on dicts instead of the game's bytes
    return h if isinstance(h, dict) else _party_mon(h)


ap.party_mon = _party_mon_dicts


def play_battle(our_six, their_six, status, brain, rng, log=None):
    """one battle; status keeps the retry memory (lost_picks) across battles. -> (won, turns)"""
    ours_all = [Mon(m) for m in our_six]
    theirs_all = [Mon(m) for m in their_six]
    picks = ap.choose_team(brain, status, {"own": [m.party() for m in ours_all], "opp": [m.party() for m in theirs_all]})
    ours = [ours_all[i] for i in picks[:3]]
    theirs = [theirs_all[i] for i in ai_team(theirs_all, ours_all, rng)]
    b = Battle(ours, theirs, rng, log)
    status.data["last_switch"] = {}
    for turn in range(200):
        b.turns = turn
        me, you = b.mon(0), b.mon(1)
        party = [m.party() for m in ours]
        opp_bench = [m.party() for i, m in enumerate(theirs) if i != b.active[1] and m.hp > 0]
        kind, i = ap.decide_turn(brain, status, me.view(), you.view(), party, b.active[0], opp_bench, False)
        mine = None
        if kind == "switch":
            b.switch(0, i)
        elif kind == "move":
            mine = me.moves[i][0] if i < len(me.moves) else None
        elif kind == "struggle":
            mine = 165
        if me.charging or me.recharge or me.trapping:  # locked in: the game plays the move on
            mine = me.charging or 0
        theirs_move = ai_move(b)
        # who goes first: Quick Attack, then speed (a tie at random)
        order = [0, 1]
        prio = [ap.MOVES.get(mine) == "Quick Attack", ap.MOVES.get(theirs_move) == "Quick Attack"]
        spd = [b.mon(0).stat("spd"), b.mon(1).stat("spd")]
        if kind == "switch":
            order = [1]  # a switch takes our turn
        elif prio[1] and not prio[0] or (prio[0] == prio[1] and (spd[1] > spd[0] or (spd[0] == spd[1] and rng.random() < 0.5))):
            order = [1, 0]
        for s in order:
            if b.mon(0).hp <= 0 or b.mon(1).hp <= 0:
                break
            b.use(s, mine if s == 0 else theirs_move)
        if b.mon(0).hp > 0 and b.mon(1).hp > 0:
            b.end_of_turn()
        for m in (b.mon(0), b.mon(1)):
            m.flinched = False
        # faints: their best answer comes in; ours as the bot chooses
        if b.mon(1).hp <= 0:
            if not b.alive(1):
                return True, turn + 1
            b.switch(1, ai_send(b))
        if b.mon(0).hp <= 0:
            if not b.alive(0):
                return False, turn + 1
            party = [m.party() for m in ours]
            opp_bench = [m.party() for i, m in enumerate(theirs) if i != b.active[1] and m.hp > 0]
            kind, i = ap.decide_turn(brain, status, b.mon(0).view(), b.mon(1).view(), party, b.active[0], opp_bench, True)
            b.switch(0, i if kind == "send" else b.alive(0)[0])
    return sum(m.hp for m in ours) / sum(m.max_hp for m in ours) > sum(m.hp for m in theirs) / sum(m.max_hp for m in theirs), 200


def record(status, won):
    """the retry memory, as autoplay.run_game keeps it"""
    pick = status.data.pop("last_pick", None)
    if pick and not won:
        status.data.setdefault("lost_picks", {}).setdefault(pick[0], []).append((pick[1], pick[2]))


# ---- data: the leaders' teams from logs, the rentals --------------------------------------------------------------
def load_rentals(path):
    mons = {}
    for h in open(path).read().split():
        m = _party_mon(h)
        m["id"] = bytes.fromhex(h)[0]
        mons.setdefault(m["species"], m)
    return mons


def load_leaders(logs, rentals):
    """the leaders' teams in castle order: their six species (as each battle's team line shows them) and the moves
    seen; stats the rentals' plus TRAINER_BONUS"""
    teams, moves = {}, collections.defaultdict(set)
    for p in logs:
        wins = collections.Counter()
        for line in open(p, errors="replace"):
            for seg in re.split(r"(?=\[game \d\])", line):
                m = re.match(r"\[game (\d)\] team vs (.+?): ", seg)
                if m:
                    pos = min(wins[m[1]], 12)
                    teams.setdefault(pos, collections.Counter())[tuple(s.strip() for s in m[2].split(","))] += 1
                m = re.search(r"vs (\w[\w ]*?) \d+/\d+.*?\(moves (.*?)\)", seg)
                if m:
                    moves[m[1]] |= set(m[2].split(", "))
                m = re.match(r"\[game (\d)\] battle (won|lost)", seg)
                if m:
                    if m[2] == "won":
                        wins[m[1]] += 1
                    elif wins[m[1]] > 8:
                        wins[m[1]] = 8
    mid_of = {v: k for k, v in ap.MOVES.items()}
    out = []
    for pos in range(13):
        if pos not in teams:
            out.append(None)
            continue
        six = teams[pos].most_common(1)[0][0]
        team = []
        for n in six:
            r = rentals.get(n)
            if r is None:
                continue
            mv = [(mid_of[x], ap.MOVEDATA.get(mid_of[x], {}).get("pp", 10)) for x in sorted(moves.get(n, ())) if x in mid_of][:4]
            B = ap.TRAINER_BONUS
            team.append(dict(r, hp=r["max_hp"] + B, max_hp=r["max_hp"] + B, moves=mv or r["moves"],
                             **{k: r[k] + B for k in ("atk", "def", "spd", "spc")}))
        out.append(team)
    return out


# ---- runs -------------------------------------------------------------------------------------------------------
def _battles(job):
    """worker: n battles of one team against one leader (a retry memory per series of tries, as in the castle)"""
    team, leader, n, seed, style, depth = job
    ap.LEADER_DEPTH = depth
    rng = random.Random(seed)
    won = 0
    status = Status(style)
    for k in range(n):
        if k % 5 == 0:  # five tries per leader in a run: a fresh retry memory after each five
            status = Status(style)
        w, _ = play_battle(team, leader, status, Brain(), rng)
        record(status, w)
        won += w
    return won


def main():
    ap_ = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap_.add_argument("--logs", nargs="+", required=True, help="autoplay logs to read the leaders' teams from")
    ap_.add_argument("--rentals", default=os.path.join(ap.ROOT, "build", "autoplay_roster.txt"))
    ap_.add_argument("--team", action="append", required=True, help="six species, comma separated (repeatable)")
    ap_.add_argument("--style", default="balanced")
    ap_.add_argument("--battles", type=int, default=20, help="battles per team and leader")
    ap_.add_argument("--leaders", default=",".join(LEADERS), help="which leaders (default all 13)")
    ap_.add_argument("--depth", type=int, default=ap.LEADER_DEPTH, help="lookahead turns (the bot's leader depth)")
    ap_.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    ap_.add_argument("--seed", type=int, default=1)
    ap_.add_argument("--show", action="store_true", help="play one battle per team and leader and print it")
    args = ap_.parse_args()
    ap.ALLOWED = ap.load_learnsets(next(iter(sorted(__import__("glob").glob(os.path.join(ap.ROOT, "gb", "*.gb")))), None))
    ap.LEADER_DEPTH = args.depth
    rentals = load_rentals(args.rentals)
    leaders = load_leaders(args.logs, rentals)
    want = [LEADERS.index(x) for x in args.leaders.split(",")]
    teams = [[rentals[n.strip()] for n in t.split(",")] for t in args.team]
    if args.show:
        for t in teams:
            for pos in want:
                log = []
                status = Status(args.style, log)
                w, turns = play_battle(t, leaders[pos], status, Brain(), random.Random(args.seed), log)
                print("== %s vs %s: %s in %d turns" % ("/".join(m["species"] for m in t), LEADERS[pos], "won" if w else "lost", turns))
                print("\n".join(log))
        return
    jobs, keys = [], []
    for ti, t in enumerate(teams):
        for pos in want:
            if not leaders[pos]:
                continue
            for c in range(0, args.battles, 5):
                jobs.append((t, leaders[pos], min(5, args.battles - c), args.seed * 1000003 + ti * 7919 + pos * 104729 + c,
                             args.style, args.depth))
                keys.append((ti, pos))
    t0 = time.time()
    with multiprocessing.Pool(args.workers) as pool:
        res = pool.map(_battles, jobs)
    won = collections.Counter()
    for k, w in zip(keys, res):
        won[k] += w
    total = sum(j[2] for j in jobs)
    print("%d battles in %.0f s" % (total, time.time() - t0))
    print("%-48s " % "team" + " ".join("%8s" % LEADERS[p] for p in want))
    for ti, t in enumerate(teams):
        print("%-48s " % ", ".join(m["species"] for m in t) +
              " ".join("%7.0f%%" % (100 * won[(ti, p)] / args.battles) for p in want))


if __name__ == "__main__":
    main()
