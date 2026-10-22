#!/usr/bin/env python3
"""Searches the rental list for the six that most often clear Round 2 of the Gym Leader Castle, in the simulator
(tools/battle_sim.py). Castle-specific on purpose: it plays the leaders' actual teams (from autoplay logs).

A team's worth is its chance to clear the castle as autoplay plays it: each gym leader with up to five tries, then
the Elite Four and the rival as one series, run again up to --runs times. Every leader's win rate comes from
simulated battles; all teams meet the same dice (the same seeds per leader and battle), so differences between
teams are not just luck.

Start: the teams given with --team plus the bot's own drafts in each style; then rounds of one-Pokemon swaps from
the strongest rentals, kept when the clear chance rises. Screening uses fewer battles (and the lookahead's quick
depth); the best go through a full evaluation. Every evaluation is kept in --cache (JSON), so a search can go on later.

  pypy3 tools/team_search.py --logs LOG... --team "Jynx,Gengar,..." --rounds 4
"""
import argparse, json, multiprocessing, os, random, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import battle_sim as bs  # noqa: E402

ap = bs.ap
E4 = [8, 9, 10, 11, 12]  # Lorelei, Bruno, Agatha, Lance, the rival


def clear_chance(rates, tries=5, runs=5):
    """rates: win rate per leader (13) -> the chance to clear: every gym within `tries`, then the five of the Elite
    Four in one go within `runs` runs"""
    p = 1.0
    for i in range(8):
        p *= 1 - (1 - rates[i]) ** tries
    run = 1.0
    for i in E4:
        run *= rates[i]
    return p * (1 - (1 - run) ** runs), run


def _job(job):
    team, leader, seeds, style, depth = job
    ap.LEADER_DEPTH = depth
    won = 0
    for seed in seeds:
        rng = random.Random(seed)
        w, _ = bs.play_battle(team, leader, bs.Status(style), bs.Brain(), rng)
        won += w
    return won


class Search:
    def __init__(self, args):
        self.args = args
        ap.ALLOWED = ap.load_learnsets(os.path.join(ap.ROOT, "gb", "yellow_de.gb"))
        self.rentals = bs.load_rentals(args.rentals)
        self.leaders = bs.load_leaders(args.logs, self.rentals)
        self.cache = {}
        if os.path.exists(args.cache):
            self.cache = json.load(open(args.cache))
        self.pool = multiprocessing.Pool(args.workers)

    def key(self, team, n, depth):
        return "%s|%d|%d" % (",".join(sorted(team)), n, depth)

    def evaluate(self, teams, n, depth):
        """win rates per leader for each team (n battles each, the same seeds for every team)"""
        todo = [t for t in teams if self.key(t, n, depth) not in self.cache]
        jobs, owners = [], []
        for t in todo:
            mons = [self.rentals[x] for x in t]
            for pos in range(13):
                seeds = [self.args.seed * 1000003 + pos * 7919 + k for k in range(n)]
                for c in range(0, n, 4):  # small jobs: the pool stays busy to the end
                    jobs.append((mons, self.leaders[pos], seeds[c:c + 4], self.args.style, depth))
                    owners.append((tuple(t), pos))
        if jobs:
            t0 = time.time()
            res = self.pool.map(_job, jobs, chunksize=1)
            won = {}
            for (t, pos), w in zip(owners, res):
                won.setdefault(t, [0] * 13)[pos] += w
            for t, w in won.items():
                self.cache[self.key(t, n, depth)] = [x / n for x in w]
            json.dump(self.cache, open(self.args.cache, "w"))
            print("  %d teams x 13 leaders x %d battles in %.0f s" % (len(todo), n, time.time() - t0), flush=True)
        return [self.cache[self.key(t, n, depth)] for t in teams]

    def score(self, rates):
        return clear_chance(rates, runs=self.args.runs)[0]

    def drafts(self):
        teams = []
        for style in ("balanced", "aggressive", "safe", "control", "bulky"):
            ap.BATCH_DRAFTED.clear()
            for seed in range(self.args.drafts):
                st = bs.Status(style)
                ids = ap.draft_team(bs.Brain(), st, open(self.args.rentals).read().split(), random.Random(seed))
                teams.append([ap.SPECIES[i] for i in ids])
        return teams

    def report(self, scored, top=8):
        print("%-58s %7s %6s  %s" % ("team", "clear", "E4 run", " ".join("%4s" % n[:4] for n in bs.LEADERS)))
        for s, t, r in scored[:top]:
            print("%-58s %6.1f%% %5.1f%%  %s" % (", ".join(t), 100 * s, 100 * clear_chance(r)[1],
                                                 " ".join("%3.0f%%" % (100 * x) for x in r)), flush=True)

    def run(self):
        a = self.args
        start = [[x.strip() for x in t.split(",")] for t in (a.team or [])] + self.drafts()
        seen, teams = set(), []
        for t in start:
            k = tuple(sorted(t))
            if k not in seen and all(x in self.rentals for x in t):
                seen.add(k)
                teams.append(t)
        print("start: %d teams" % len(teams), flush=True)
        rates = self.evaluate(teams, a.screen, a.screen_depth)
        scored = sorted(((self.score(r), t, r) for t, r in zip(teams, rates)), key=lambda x: -x[0])
        self.report(scored)
        # the swap pool: the rentals that appear in the start teams and the draft's strongest
        pool = sorted({x for t in teams for x in t} | set(a.extra.split(",") if a.extra else ()))
        rng = random.Random(a.seed)
        for rnd in range(a.rounds):
            best = scored[:a.beam]
            cand = []
            for s, t, r in best:
                for _ in range(a.neighbours):
                    i = rng.randrange(6)
                    new = rng.choice([x for x in pool if x not in t])
                    u = t[:i] + [new] + t[i + 1:]
                    k = tuple(sorted(u))
                    if k not in seen:
                        seen.add(k)
                        cand.append(u)
            print("round %d: %d new teams" % (rnd + 1, len(cand)), flush=True)
            rates = self.evaluate(cand, a.screen, a.screen_depth)
            scored = sorted(scored + [(self.score(r), t, r) for t, r in zip(cand, rates)], key=lambda x: -x[0])
            self.report(scored, 5)
        final = [t for _, t, _ in scored[:a.finalists]]
        print("final: the best %d with %d battles per leader at full depth" % (len(final), a.full), flush=True)
        rates = self.evaluate(final, a.full, ap.LEADER_DEPTH)
        scored = sorted(((self.score(r), t, r) for t, r in zip(final, rates)), key=lambda x: -x[0])
        self.report(scored, a.finalists)


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--logs", nargs="+", required=True)
    p.add_argument("--rentals", default=os.path.join(ap.ROOT, "build", "autoplay_roster.txt"))
    p.add_argument("--team", action="append", help="a start team (repeatable); the bot's drafts are added")
    p.add_argument("--drafts", type=int, default=2, help="drafts per style for the start")
    p.add_argument("--extra", default="", help="more rentals for the swap pool, comma separated")
    p.add_argument("--style", default="balanced", help="the style the bot plays the battles with")
    p.add_argument("--screen", type=int, default=8, help="battles per leader when screening")
    p.add_argument("--screen-depth", type=int, default=3, help="lookahead turns when screening")
    p.add_argument("--full", type=int, default=40, help="battles per leader for the finalists")
    p.add_argument("--finalists", type=int, default=5)
    p.add_argument("--rounds", type=int, default=3)
    p.add_argument("--beam", type=int, default=4, help="teams to build swaps from each round")
    p.add_argument("--neighbours", type=int, default=6, help="swaps tried per team and round")
    p.add_argument("--runs", type=int, default=5, help="Elite Four runs allowed (autoplay --max-e4-runs)")
    p.add_argument("--workers", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--cache", default="team_search.json")
    Search(p.parse_args()).run()


if __name__ == "__main__":
    main()
