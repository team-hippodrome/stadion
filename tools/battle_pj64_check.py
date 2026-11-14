#!/usr/bin/env python3 -I
"""A third reference for one sweep finding: Flamethrower burning its target, played in the team's Project64 build
(team-hippodrome/pj64-pokestadium-debug) through its agent server, to see how the original game draws the burned
target (the port shows a flat red silhouette, ares a red-tinted model).

Usage: tools/battle_pj64_check.py <out dir> [--port 9123] [--exe <pj64-stadium>] [--frames 420]
Start pj64-stadium with --agent first (or pass --exe to have it started, with the ROM and STADIUM_REPO set).

What it does: plays the route of tools/battle_xcheck.py (Poke Cup, first battle, Charmander leading), synchronised
on loaded fragments instead of frame counts (Project64's timing differs), then patches the battle engine in RAM:
Battle_RandomAccuracy returns 0 (Flamethrower hits) and both random calls of BattleEffect_SecondaryStatusChance
read 0 (the burn lands). Everything else stays as the game plays it. It chooses Flamethrower (A, C-up) and saves
a screenshot of every second frame from the start of the move's animation (BattleScene_EnterRow3StartMoveAnim) as
<out>/pj64_<frame>.png. Compare with the sweep's case sheet (build-xcheck-sweep/cases/053_base_*/sheet_053_k0.png).
"""
import json, os, socket, subprocess, sys, time

ROM = os.environ.get("ROM", "/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64")
ORDER_SELECT, BATTLE_ENGINE = 56, 51          # fragment ids (link address 0x84800000, 0x84300000)
RANDOM_ACCURACY = 0x8436F788                  # Battle_RandomAccuracy
STATUS_CHANCE_RANDOM_CALLS = (0x84373FAC, 0x84373FCC)  # jal Battle_Random in BattleEffect_SecondaryStatusChance
START_MOVE_ANIM = 0x84302A78                  # BattleScene_EnterRow3StartMoveAnim
COLLECT_PLAYER_ACTION = 0x8430F794            # BattleScene_CollectPlayerAction
JR_RA, LI_V0_0 = 0x03E00008, 0x34020000       # jr ra / ori v0, zero, 0


class Agent:
    def __init__(self, port):
        for _ in range(60):  # (pj64-stadium takes a while to start listening)
            try:
                self.s = socket.create_connection(("127.0.0.1", port), timeout=120)
                break
            except ConnectionRefusedError:
                time.sleep(1)
        else:
            sys.exit("no agent server on port %d" % port)
        self.f = self.s.makefile("r")
        self.n = 0

    def __call__(self, method, **params):
        self.n += 1
        self.s.sendall((json.dumps({"id": self.n, "method": method, "params": params}) + "\n").encode())
        r = json.loads(self.f.readline())
        if not r.get("ok"):
            raise RuntimeError("%s: %s" % (method, r.get("error")))
        return r.get("result")


def loaded(agent, fid):
    return any(f["id"] == fid for f in agent("fragments.list"))


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    out = os.path.abspath(sys.argv[1])
    args = dict(zip(sys.argv[2::2], sys.argv[3::2]))
    port = int(args.get("--port", "9123"))
    frames = int(args.get("--frames", "420"))
    os.makedirs(out, exist_ok=True)
    if "--exe" in args:
        subprocess.Popen([args["--exe"], ROM, "--agent", str(port)], stdout=open(os.path.join(out, "pj64.log"), "w"),
                         stderr=subprocess.STDOUT)
    agent = Agent(port)
    print(agent("status"))
    # the route: mash A+Start through the menus into the Poke Cup (as replays/to_battle.rec) until the team order
    # screen is loaded, then pick A (Charmander), C-up, C-left and confirm with A
    t0 = time.time()
    while not loaded(agent, ORDER_SELECT):
        agent("input.press", port=1, buttons=["A", "Start"], frames=6, wait=True)
        agent("emu.wait_frames", frames=14, timeout_ms=10000)
        if time.time() - t0 > 600:
            sys.exit("no order screen after 10 minutes")
    print("order screen at", agent("status").get("frame"))
    agent("emu.wait_frames", frames=400, timeout_ms=60000)
    for b, gap in (("A", 13), ("CUp", 13), ("CLeft", 37), ("A", 30)):
        agent("input.press", port=1, buttons=[b], frames=6, wait=True)
        agent("emu.wait_frames", frames=gap, timeout_ms=10000)
    while not loaded(agent, BATTLE_ENGINE):
        agent("emu.wait_frames", frames=30, timeout_ms=10000)
    # the battle engine is in memory: Flamethrower hits and burns
    agent("memory.write", address=hex(RANDOM_ACCURACY), type="u32", values=[JR_RA, LI_V0_0])
    for a in STATUS_CHANCE_RANDOM_CALLS:
        agent("memory.write", address=hex(a), type="u32", values=[LI_V0_0])
    print("battle engine patched at", agent("status").get("frame"))
    # the player's first decision: A (Battle), then C-up (move 1: Flamethrower)
    agent("breakpoint.add", address=hex(COLLECT_PLAYER_ACTION), kind="exec")
    agent("emu.wait_break", timeout_ms=600000)
    agent("breakpoint.clear")
    agent("emu.resume")
    agent("emu.wait_frames", frames=30, timeout_ms=10000)
    agent("input.press", port=1, buttons=["A"], frames=6, wait=True)
    agent("emu.wait_frames", frames=30, timeout_ms=10000)
    agent("input.press", port=1, buttons=["CUp"], frames=6, wait=True)
    # the move animation, frame by frame (every second frame: the game draws at 30 fps)
    agent("breakpoint.add", address=hex(START_MOVE_ANIM), kind="exec")
    while True:
        agent("emu.wait_break", timeout_ms=600000)
        regs = agent("cpu.registers")
        agent("breakpoint.clear")
        agent("emu.resume")
        break
    print("move animation starts at", agent("status").get("frame"), "(a0 %s)" % regs.get("a0", "?"))
    for k in range(0, frames, 2):
        agent("screenshot", path=os.path.join(out, "pj64_%03d.png" % k))
        agent("emu.wait_frames", frames=2, timeout_ms=10000)
    print("saved %d screenshots to %s" % (frames // 2, out))


if __name__ == "__main__":
    main()
