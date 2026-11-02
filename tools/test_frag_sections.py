#!/usr/bin/env python3
"""Compile and exercise fragment data resets without a game ROM. Requires Clang on PATH."""
import argparse
from pathlib import Path
import os
import re
import struct
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'clang'))
    args = parser.parse_args()
    generator = Path(__file__).with_name('gen_frag_sections.py')
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        sources = root / 'src'
        generated = root / 'generated'
        # Real entry names let the generator discover these tiny stand-in fragments.
        entries = [('area_select', 'AreaSelect_Main'), ('free_battle_rules', 'FreeBattleRules_Main')]
        # Use names from the committed entry table so this also exercises ownership discovery.
        table = (generator.parent.parent / 'src/port/generated/port_fragment_entries.c').read_text()
        names = re.findall(r'\(void\*\)(\w+),\s*"fragment\w+"', table)
        entries[1] = ('other_fragment', next(name for name in names if name != 'AreaSelect_Main'))
        for i, (directory, entry) in enumerate(entries):
            path = sources / 'fragments' / directory / 'scene.c'
            path.parent.mkdir(parents=True)
            path.write_text(f'int data{i} = {17 + i};\nint zero{i}[257];\nvoid {entry}(void) {{}}\n')
        subprocess.run([sys.executable, '-I', str(generator), str(sources), str(generated)], check=True)
        main_c = root / 'main.c'
        main_c.write_text('''
extern int data0, data1, zero0[257], zero1[257];
extern void Port_FragmentResetData(void*);
''' + '\n'.join(f'extern void {entry}(void);' for _, entry in entries) + f'''
int main(void) {{
    data0 = data1 = 99;
    zero0[0] = zero0[256] = zero1[0] = zero1[256] = 99;
    Port_FragmentResetData({entries[0][1]});
    if (data0 != 17 || zero0[0] || zero0[256]) return 1;
    if (data1 != 99 || zero1[0] != 99 || zero1[256] != 99) return 2;
    Port_FragmentResetData({entries[1][1]});
    if (data1 != 18 || zero1[0] || zero1[256]) return 3;
    data0 = zero0[256] = 123;
    Port_FragmentResetData({entries[0][1]});
    return data0 != 17 || zero0[256] != 0;
}}
''')
        objects = []
        for i, (directory, _) in enumerate(entries):
            obj = root / f'scene{i}.o'
            subprocess.run([args.cc, '-O2', '-include', str(generated / 'frag_sect' / (directory + '.h')),
                            '-c', str(sources / 'fragments' / directory / 'scene.c'), '-o', str(obj)], check=True)
            objects.append(str(obj))
        exe = root / ('test.exe' if os.name == 'nt' else 'test')
        subprocess.run([args.cc, '-O2', str(main_c), str(generated / 'frag_sections.c'), *objects, '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
        if os.name == 'nt':
            pe = exe.read_bytes()
            header = struct.unpack_from('<I', pe, 0x3c)[0]
            sections = struct.unpack_from('<H', pe, header + 6)[0]
            if sections > 20:
                raise AssertionError(f'Fragment subsections did not merge: {sections} PE sections')
    print('Fragment initialized-data, BSS, isolation and repeated-load checks passed.')


if __name__ == '__main__':
    main()
