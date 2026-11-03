"""Replay the paced N64 logo on Windows and measure gaps between its rendered frames."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

from runner_platform import BACKENDS, actual_backend, backend_home


def run(backend, binary, rom, out):
    directory = Path(tempfile.mkdtemp(prefix=f"{backend}-", dir=out))
    replay = directory / "input.rec"
    replay.write_text("# game audio\n# load waits\n# paced logo hold\n# end 120\n", encoding="ascii")
    env = {key: value for key, value in os.environ.items() if not key.startswith("PORT_")}
    backend_home(env, str(directory), backend)
    env.update(
        PORT_HIDDEN_WINDOW="1", PORT_REPLAY=str(replay), PORT_RECORD=str(directory / "output.rec"),
        PORT_TRACE_PACING="2", PORT_STOP_AT="120", PORT_FLASH_FILE=str(directory / "test.flash"),
    )
    with (directory / "trace.log").open("w", encoding="utf-8") as log:
        process = subprocess.run([str(binary), str(rom)], env=env, cwd=directory,
                                 stdout=log, stderr=subprocess.STDOUT, timeout=60)
    text = (directory / "trace.log").read_text(encoding="utf-8", errors="replace")
    _, _, hold = text.partition("[intro] logo hold begin\n")
    body, _, end = hold.partition("[intro] Game Boy save scan:")
    duration = re.match(r" \d+ us; logo hold after scan: (\d+) us \((\d+) retraces\)", end)
    gaps = [float(value) for value in re.findall(r"^\[pacing\] drawn ([\d.]+)", body, re.MULTILINE)]
    retraces = int(duration[2]) if duration else None
    max_gap = max(gaps, default=None)
    passed = (
        process.returncode == 0 and "[stop] reached frame" in text
        and actual_backend(directory) == BACKENDS[backend][0]
        and retraces is not None and 60 <= retraces <= 63
        and len(gaps) >= 20 and max_gap < 250
    )
    return {
        "backend": backend, "passed": passed, "trace": str(directory / "trace.log"),
        "hold_retraces": retraces, "hold_ms": int(duration[1]) / 1000 if duration else None,
        "draw_intervals": len(gaps), "max_gap_ms": max_gap, "gaps_over_50_ms": sum(gap > 50 for gap in gaps),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bin", type=Path, required=True)
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--backends", nargs="+", choices=BACKENDS, default=list(BACKENDS))
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    results = []
    for backend in args.backends:
        result = run(backend, args.bin.resolve(), args.rom.resolve(), out)
        results.append(result)
        print(json.dumps(result), flush=True)
    (out / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    return int(not all(result["passed"] for result in results))


if __name__ == "__main__":
    raise SystemExit(main())
