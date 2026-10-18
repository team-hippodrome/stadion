"""Command line interface: python3 -m stadium_save <command> ... (see README.md)."""
import argparse
import json
import sys

from . import gamedata, pokemon
from .save import (CUP_DIVISIONS, GLC_STAGES, MODES, RECORDS, TEAM_CATEGORIES, SaveFile, get_path, set_path)


def _open(path):
    return SaveFile.load(path)


def _out(args, s):
    out = args.output or args.save
    s.save(out)
    print("wrote", out)


def parse_mon(spec, defaults):
    """SPECIES[:LEVEL][:MOVE/MOVE/...][@key=value,...] e.g. 'MEWTWO:100:PSYCHIC/RECOVER/AMNESIA/BARRIER@nick=MEW2'."""
    opts = {}
    if "@" in spec:
        spec, rest = spec.split("@", 1)
        for kv in rest.split(","):
            k, v = kv.split("=", 1)
            opts[k.strip()] = v.strip()
    parts = spec.split(":")
    species = parts[0]
    level = int(parts[1]) if len(parts) > 1 and parts[1] else 50
    moves = parts[2].split("/") if len(parts) > 2 and parts[2] else None
    dv = int(opts.get("dv", 15))
    se = 65535 if opts.get("statexp") == "max" else int(opts.get("statexp", 0))
    return pokemon.make_mon(species, level, moves=moves, nickname=opts.get("nick"),
                            ot_name=opts.get("ot", defaults.get("ot", "STADIUM")),
                            ot_id=int(opts.get("otid", defaults.get("ot_id", 0))),
                            dvs={"atk": dv, "def": dv, "spd": dv, "spc": dv},
                            stat_exp={k: se for k in pokemon.STAT_KEYS})


def _mons_from_args(args):
    mons = []
    if getattr(args, "json", None):
        with open(args.json) as f:
            mons += json.load(f)
    defaults = {"ot": args.ot, "ot_id": args.trainer_id}
    mons += [parse_mon(m, defaults) for m in args.mons]
    for m in mons:
        for e in pokemon.validate(m):
            print("warning: %s: %s" % (m.get("nickname", m.get("species")), e), file=sys.stderr)
    return mons


def _mon_line(m):
    if m is None:
        return "-"
    name = m.get("species", "index %d" % m.get("species_index", 0))
    nick = m["nickname"] if isinstance(m["nickname"], str) else m["nickname"]["text"]
    moves = "/".join(str(x["move"]) for x in m["moves"] if x)
    return "%-10s L%-3d %-10s OT %s/%05d  %s" % (name, m["level"], nick, m["ot_name"] if isinstance(m["ot_name"], str)
                                                 else m["ot_name"]["text"], m["ot_id"], moves)


# ---- commands ----

def cmd_info(args):
    print(info_text(_open(args.save), args.verbose), end="")


def info_text(s, verbose=False):
    lines = []
    out = lines.append
    d = s.to_dict()
    bad = [c for c in s.check() if c[3] != "primary" or c[2] != "valid"]
    out("records: %d, needing repair: %d" % (len(RECORDS), len(bad)))
    o = d["options"]
    out("options: sound %s, voice %s, GB Tower speed %d, Kids Club hyper %s" % (
        "mono" if o["sound"] & 1 else "stereo", "off" if o["sound"] & 2 else "on", o["gb_tower_speed"],
        "yes" if o["kids_club"] & 1 else "no"))
    if o["bonus_pokemon"]:
        out("bonus Pokémon waiting at the Lab: %s%s" % (pokemon.species_name(o["bonus_pokemon"] & 0xFF),
                                                          " (Round 2)" if o["bonus_pokemon"] & 0x100 else ""))
    for r, p in enumerate(o["progress"]):
        cleared = [MODES.get(m, str(m)) for m in range(16) if p["cleared_modes"] & (1 << m)]
        out("Round %d: cleared %s; Poké Cup %d/4, Prime Cup %d/4, Castle %d/8, Mewtwo %s" % (
            r + 1, ", ".join(cleared) or "nothing", p["poke_cup"], p["prime_cup"], p["gym_leader_castle"],
            "unlocked" if p["mewtwo_unlocked"] else "locked"))
    for cat in TEAM_CATEGORIES:
        for i, t in enumerate(d["teams"][cat]):
            if t["flags"] & 1 or t["entries"]:
                out("team %s[%d] %r (ID %05d): %s" % (cat, i, t["name"], t["trainer_id"],
                                                       ", ".join(m.get("species", "?") for m in t["entries"])))
    for i, b in enumerate(d["boxes"]):
        if b["entries"]:
            out("box %d: %d Pokémon" % (i + 1, len(b["entries"])))
            if verbose:
                for m in b["entries"]:
                    out("    " + _mon_line(m))
    for i, b in enumerate(d["item_boxes"]):
        if b["flags"] & 1 or b["entries"]:
            out("item box %d (%r %05d): %s" % (i + 1, b["name"], b["trainer_id"], ", ".join(
                "%s x%d" % (e.get("_name", e["item"]), e["quantity"]) for e in b["entries"])))
    statues = [(dex + 1, e) for dex, e in enumerate(d["victory_palace"]["entries"]) if e]
    out("Victory Palace statues: %d%s" % (len(statues), (": " + ", ".join(
        gamedata.SPECIES[x] for x, _ in statues)) if statues and len(statues) <= 20 else ""))
    out("album photos: %d, photo set: %d" % (sum(1 for e in d["album"]["entries"] if e),
                                               sum(1 for e in d["photos"]["entries"] if e)))
    c = d["continue"]
    if c["flags"] & 1:
        se = c["session"]
        where = MODES.get(se["mode"], se["mode"])
        if se["mode"] in (3, 6):
            where += " " + CUP_DIVISIONS[se["mode_selection"] & 3]
        elif se["mode"] == 7 and se["mode_selection"] < len(GLC_STAGES):
            where += " " + GLC_STAGES[se["mode_selection"]]
        out("saved battle: %s, battle %d, %d Pokémon, continues %d" % (where, se["battle"], c["count"],
                                                                        se["continues"]))
        if verbose:
            for m in c["pokemon"][:c["count"]]:
                out("    " + _mon_line(m))
    return "\n".join(lines) + "\n"


def cmd_check(args):
    s = _open(args.save)
    bad = 0
    for path, p, b, which in s.check():
        if p != "valid" or b != "valid" or args.verbose:
            print("%-32s primary %-8s backup %-8s game loads %s" % ("/".join(map(str, path)), p, b, which))
        if p != "valid" or b != "valid":
            bad += 1
    print("%d records, %d not valid in both copies" % (len(RECORDS), bad))
    return 1 if bad else 0


def cmd_fix(args):
    s = _open(args.save)
    for path, copy_ in s.fix_checksums(args.include_pending):
        print("footer rewritten: %s (%s)" % ("/".join(map(str, path)), copy_))
    _out(args, s)


def cmd_repair(args):
    s = _open(args.save)
    for path in s.repair():
        print("repaired: %s" % "/".join(map(str, path)))
    _out(args, s)


def cmd_dump(args):
    s = _open(args.save)
    text = s.to_json()
    if args.output:
        with open(args.output, "w", encoding="utf-8") as f:
            f.write(text + "\n")
        print("wrote", args.output)
    else:
        print(text)


def cmd_build(args):
    with open(args.json, encoding="utf-8") as f:
        s = SaveFile.from_json(f.read())
    s.save(args.output)
    print("wrote", args.output)


def cmd_new(args):
    SaveFile().save(args.output)
    print("wrote", args.output)


def _parse_value(v):
    try:
        return json.loads(v)
    except json.JSONDecodeError:
        return v


def _parse_path(p):
    return [int(x) if x.lstrip("-").isdigit() else x for x in p.split(".")]


def cmd_get(args):
    d = _open(args.save).to_dict()
    print(json.dumps(get_path(d, _parse_path(args.path)), indent=1, ensure_ascii=False))


def cmd_set(args):
    s = _open(args.save)

    def fn(d):
        for a in args.assignments:
            p, v = a.split("=", 1)
            set_path(d, _parse_path(p), _parse_value(v))

    s.edit(fn)
    _out(args, s)


def cmd_unlock(args):
    s = _open(args.save)

    def fn(d):
        rounds = [0, 1] if args.round2 else [0]
        for r in rounds:
            p = d["options"]["progress"][r]
            p.update(cleared_modes=0x1F8 if args.mewtwo else 0xF8, gym_leader_castle=8, poke_cup=4, prime_cup=4,
                     mewtwo_unlocked=1)
        d["options"]["gb_tower_speed"] = args.tower_speed

    s.edit(fn)
    _out(args, s)


def cmd_options(args):
    s = _open(args.save)

    def fn(d):
        o = d["options"]
        if args.sound:
            o["sound"] = (o["sound"] & ~1) | (args.sound == "mono")
        if args.voice:
            o["sound"] = (o["sound"] & ~2) | (2 if args.voice == "off" else 0)
        if args.tower_speed is not None:
            o["gb_tower_speed"] = args.tower_speed
        if args.kids_club_hyper:
            o["kids_club"] = (o["kids_club"] & ~1) | (args.kids_club_hyper == "on")
        if args.bonus is not None:
            if args.bonus in ("0", "none"):
                o["bonus_pokemon"] = 0
            else:
                name, _, r2 = args.bonus.partition(":")
                o["bonus_pokemon"] = pokemon.species_lookup(name) | (0x100 if r2 == "round2" else 0)

    s.edit(fn)
    _out(args, s)


def _list_record(d, args):
    if args.kind == "team":
        return d["teams"][args.category][args.slot]
    if args.kind == "box":
        return d["boxes"][args.box - 1]
    return d["item_boxes"][args.box - 1]


def cmd_team(args):
    s = _open(args.save)
    args.kind = "team"

    def fn(d):
        t = _list_record(d, args)
        if args.action == "clear":
            t.update(flags=0, name="", trainer_id=0, entries=[])
            for k in [k for k in t if k.startswith("_") and k != "_footer"]:
                del t[k]
            return
        mons = _mons_from_args(args)
        if not 1 <= len(mons) <= 6:
            raise SystemExit("a team needs 1..6 Pokémon")
        t.update(flags=1, name=args.name, trainer_id=args.trainer_id, entries=mons)
        t.pop("_unused_entries", None)

    s.edit(fn)
    _out(args, s)


def cmd_box(args):
    s = _open(args.save)
    args.kind = "box"

    def fn(d):
        b = _list_record(d, args)
        if args.action == "clear":
            b["entries"] = []
        else:
            b["entries"] = b["entries"] + _mons_from_args(args)
            if len(b["entries"]) > 20:
                raise SystemExit("a box holds 20 Pokémon")
        b.pop("_unused_entries", None)

    s.edit(fn)
    _out(args, s)


def cmd_items(args):
    s = _open(args.save)
    args.kind = "items"

    def fn(d):
        b = _list_record(d, args)
        if args.action == "clear":
            b.update(flags=0, name="", trainer_id=0, entries=[])
        else:
            entries = []
            for spec in args.items:
                name, _, qty = spec.rpartition(":")
                if not name:
                    name, qty = qty, "1"
                item = int(name) if name.isdigit() else next(
                    i for i, n in enumerate(gamedata.ITEMS) if n and n.upper() == name.upper())
                entries.append({"item": item, "quantity": int(qty)})
            b["entries"] = b["entries"] + entries
            b["flags"] |= 1
            if args.name is not None:
                b["name"] = args.name
            if args.trainer_id is not None:
                b["trainer_id"] = args.trainer_id
        b.pop("_unused_entries", None)

    s.edit(fn)
    _out(args, s)


def cmd_statue(args):
    s = _open(args.save)

    def fn(d):
        ents = d["victory_palace"]["entries"]
        targets = range(1, 152) if args.species == "all" else [pokemon.species_lookup(args.species)]
        for dex in targets:
            if args.action == "clear":
                ents[dex - 1] = None
            else:
                ents[dex - 1] = {"mode": args.mode | (0x80 if args.round2 else 0), "level": args.level,
                                 "ot_id": args.trainer_id, "nickname": args.nickname or gamedata.SPECIES[dex],
                                 "ot_name": args.ot}

    s.edit(fn)
    _out(args, s)


def cmd_continue(args):
    s = _open(args.save)

    def fn(d):
        c = d["continue"]
        if args.action == "clear":
            c.update(flags=0, count=0)
            c["session"].update(mode=0, mode_selection=0, battle=0, round2=0, continues=0, opponent_history=[0] * 8,
                                gb_starter_choice=0, trainer_id=0, player_name="")
            c["pokemon"] = ["fill:00*55"] * 6
            c["battle_byte_52"] = [0] * 6
            c["battle_byte_53"] = [0] * 6
            return
        mons = _mons_from_args(args)
        if not 1 <= len(mons) <= 6:
            raise SystemExit("1..6 Pokémon")
        c["flags"] = 1
        c["session"].update(mode=args.mode, mode_selection=args.selection, battle=args.battle,
                            round2=1 if args.round2 else 0, continues=args.continues,
                            trainer_id=args.trainer_id, player_name=args.ot)
        c["count"] = len(mons)
        c["pokemon"] = mons + ["fill:00*55"] * (6 - len(mons))

    s.edit(fn)
    _out(args, s)


def cmd_serve(args):
    from . import web
    web.serve(args.port, args.save, writable=args.write, exit_with_parent=args.exit_with_parent, gb=args.gb)


def main(argv=None):
    ap = argparse.ArgumentParser(prog="stadium_save", description="Pokémon Stadium (US) save editor")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def add(name, fn, help_, save=True, out=True):
        p = sub.add_parser(name, help=help_)
        if save:
            p.add_argument("save", help="128 KiB FlashRAM image (.fla/.flash)")
        if out:
            p.add_argument("-o", "--output", help="output file (default: overwrite the input)")
        p.set_defaults(fn=fn)
        return p

    p = add("info", cmd_info, "summary of the save", out=False)
    p.add_argument("-v", "--verbose", action="store_true")
    p = add("check", cmd_check, "validate both copies of every record; exit 1 if any is not valid", out=False)
    p.add_argument("-v", "--verbose", action="store_true")
    p = add("fix", cmd_fix, "recompute footers (POKE + checksum) of records with a bad footer")
    p.add_argument("--include-pending", action="store_true", help="also commit XXXX (mid-transaction) records")
    add("repair", cmd_repair, "do what the game does on load: keep the valid copy, reset broken records")
    p = add("dump", cmd_dump, "everything as JSON (exact: build turns it back into the same bytes)", out=False)
    p.add_argument("-o", "--output")
    p = add("build", cmd_build, "JSON -> save", save=False, out=False)
    p.add_argument("json")
    p.add_argument("-o", "--output", required=True)
    p = add("new", cmd_new, "write a blank save identical to the one the game creates", save=False, out=False)
    p.add_argument("-o", "--output", required=True)
    p = add("get", cmd_get, "print one JSON path, e.g. options.progress.0", out=False)
    p.add_argument("path")
    p = add("set", cmd_set, "set JSON paths: options.sound=1 teams.poke_cup.0.name='\"RED\"'")
    p.add_argument("assignments", nargs="+")
    p = add("unlock", cmd_unlock, "mark every cup, the Castle and Mewtwo as cleared")
    p.add_argument("--round2", action="store_true", help="Round 2 progress as well")
    p.add_argument("--mewtwo", action="store_true", help="also set the 'Mewtwo beaten' bit (0x100)")
    p.add_argument("--tower-speed", type=int, default=2, choices=(0, 1, 2))
    p = add("options", cmd_options, "sound/voice/GB Tower/Kids Club/bonus Pokémon")
    p.add_argument("--sound", choices=("stereo", "mono"))
    p.add_argument("--voice", choices=("on", "off"))
    p.add_argument("--tower-speed", type=int, choices=(0, 1, 2))
    p.add_argument("--kids-club-hyper", choices=("on", "off"))
    p.add_argument("--bonus", help="SPECIES[:round2] waiting at the Lab, or none")

    mon_help = "SPECIES[:LEVEL][:MOVE/MOVE..][@nick=X,ot=Y,otid=N,dv=0..15,statexp=N|max]"
    p = add("team", cmd_team, "registered teams")
    p.add_argument("action", choices=("set", "clear"))
    p.add_argument("category", choices=TEAM_CATEGORIES)
    p.add_argument("slot", type=int, choices=range(10))
    p.add_argument("mons", nargs="*", help=mon_help)
    p.add_argument("--json", help="JSON list of Pokémon (format of 'dump')")
    p.add_argument("--name", default="RED", help="trainer name shown for the team")
    p.add_argument("--trainer-id", type=int, default=12345)
    p.add_argument("--ot", default="RED", help="default OT name for new Pokémon")
    p = add("box", cmd_box, "Pokémon Lab boxes (1..12)")
    p.add_argument("action", choices=("add", "clear"))
    p.add_argument("box", type=int, choices=range(1, 13))
    p.add_argument("mons", nargs="*", help=mon_help)
    p.add_argument("--json")
    p.add_argument("--trainer-id", type=int, default=12345)
    p.add_argument("--ot", default="RED")
    p = add("items", cmd_items, "Pokémon Lab item boxes (1..4, one per Game Boy trainer)")
    p.add_argument("action", choices=("add", "clear"))
    p.add_argument("box", type=int, choices=range(1, 5))
    p.add_argument("items", nargs="*", help="ITEM[:QTY] (name or id)")
    p.add_argument("--name", help="owner trainer name")
    p.add_argument("--trainer-id", type=int)
    p = add("statue", cmd_statue, "Victory Palace statues")
    p.add_argument("action", choices=("set", "clear"))
    p.add_argument("species", help="name, dex number or 'all'")
    p.add_argument("--mode", type=int, default=3, help="session mode of the win (3 Poké Cup ... 7 Castle)")
    p.add_argument("--round2", action="store_true")
    p.add_argument("--level", type=int, default=50)
    p.add_argument("--nickname")
    p.add_argument("--ot", default="RED")
    p.add_argument("--trainer-id", type=int, default=12345)
    p = add("continue", cmd_continue, "saved cup/Castle run")
    p.add_argument("action", choices=("set", "clear"))
    p.add_argument("mons", nargs="*", help=mon_help)
    p.add_argument("--json")
    p.add_argument("--mode", type=int, default=3)
    p.add_argument("--selection", type=int, default=0, help="division (cups) or stage (Castle)")
    p.add_argument("--battle", type=int, default=1)
    p.add_argument("--round2", action="store_true")
    p.add_argument("--continues", type=int, default=0)
    p.add_argument("--trainer-id", type=int, default=12345)
    p.add_argument("--ot", default="RED")
    p = add("serve", cmd_serve, "local web UI", out=False)
    p.add_argument("--port", type=int, default=8642, help="0 = any free port (printed on startup)")
    p.add_argument("--write", action="store_true", help="add a 'Save to file' button that overwrites the save "
                   "(previous contents kept as <save>.bak)")
    p.add_argument("--exit-with-parent", action="store_true", help="stop when the parent process exits")
    p.add_argument("--gb", action="append", default=[], metavar="LABEL|SAVE|ROM",
                   help="also edit this Pokémon Red/Blue/Yellow save (repeatable; ROM gives title and language)")

    args = ap.parse_args(argv)
    return args.fn(args) or 0


if __name__ == "__main__":
    sys.exit(main())
