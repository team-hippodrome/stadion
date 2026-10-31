#!/bin/bash
# Runs the ares reference driver: tools/ares_ref/ref.sh <script> [state to load]
# Pictures named in the script (shot / every) are converted from .ppm to .png next to where they were written.
# ARES_REF_BUILD: directory with the ares_ref binary; ARES_REF_SRC: ares source tree (for the PIF firmware).
# Paths in the script are relative to the current directory.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SCRIPT="$1"; STATE="$2"
ROM="${ROM:-/Users/michael/Downloads/romis/Pokemon Stadium (USA).z64}"
: "${ARES_REF_BUILD:?set ARES_REF_BUILD to the ares build directory containing ares_ref}"
: "${ARES_REF_SRC:?set ARES_REF_SRC to the ares source tree}"
export ARES_REF_FIRMWARE="$ARES_REF_SRC/mia/Firmware/Nintendo 64"
export DYLD_LIBRARY_PATH=/Applications/ares.app/Contents/Frameworks  # MoltenVK for paraLLEl-RDP
# (macOS drops DYLD_* variables when a system binary starts the program: through /usr/bin/nice or perl, ares_ref
# finds no MoltenVK and stops with "Vulkan init failed: No RDP rendering support". Lower its priority with renice.)
# nall rewrites path arguments containing spaces: pass the ROM through a symlink
ln -sfn "$ROM" /tmp/ares_ref_rom.z64
ARGS=(/tmp/ares_ref_rom.z64 "$SCRIPT")
[ -n "$STATE" ] && ARGS+=(--load "$STATE")
"$ARES_REF_BUILD/ares_ref" "${ARGS[@]}" 2>&1 | grep -E "^\[ref\]|error|failed" || true
# convert the pictures this script names
python3 -I - "$SCRIPT" "$HERE/../ppm2png.py" <<'EOF'
import os, subprocess, sys
script, conv = sys.argv[1], sys.argv[2]
for line in open(script):
    p = line.split()
    if not p or p[0].startswith('#'):
        continue
    files = []
    if p[0] == 'shot':
        files = [p[2]]
    elif p[0] == 'every':
        files = [os.path.join(p[2], f) for f in sorted(os.listdir(p[2])) if f.endswith('.ppm')]
        if files:  # contact sheet <dir>/sheet.ppm: every picture at half width, 4 per row, labelled by order
            pics = []
            for f in files:
                data = open(f, 'rb').read().split(b'\n', 3)
                w, h = map(int, data[1].split())
                px = data[3]
                hw = w // 2
                rows = [b''.join(px[(y * w + x * 2) * 3:(y * w + x * 2) * 3 + 3] for x in range(hw)) for y in range(h)]
                pics.append((hw, h, rows))
            cols = 4
            cw, chh = max(p_[0] for p_ in pics), max(p_[1] for p_ in pics)
            nrows = (len(pics) + cols - 1) // cols
            W, H = cw * cols, chh * nrows
            out = bytearray(W * H * 3)
            for i, (pw, ph, rows) in enumerate(pics):
                ox, oy = (i % cols) * cw, (i // cols) * chh
                for y in range(ph):
                    o = ((oy + y) * W + ox) * 3
                    out[o:o + pw * 3] = rows[y]
                sheet = os.path.join(p[2], 'sheet.ppm')
            with open(sheet, 'wb') as fo:
                fo.write(b'P6\n%d %d\n255\n' % (W, H) + bytes(out))
            files.append(sheet)
    for f in files:
        if os.path.exists(f):
            subprocess.run([sys.executable, '-I', conv, f], check=True)
            os.remove(f)
EOF
