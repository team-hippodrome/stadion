"""Build a Windows caller-name index from the committed game function declarations."""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
declarations = re.findall(r'extern void (port_fn_\d+)\(\) __asm__\(PORT_ASM_NAME\("([^"]+)"\)\);', source)
if not declarations:
    raise SystemExit("No game function declarations found for Windows cross-check symbols")
lines = ['#include <stddef.h>', '#define PORT_ASM_NAME(n) n']
lines += [f'extern void {fn}() __asm__(PORT_ASM_NAME("{name}"));' for fn, name in declarations]
lines += ['struct PortHostSymbol { void* address; const char* name; };',
          'const struct PortHostSymbol gPortHostSymbols[] = {']
lines += [f'    {{ (void*){fn}, "{name}" }},' for fn, name in declarations]
lines += ['};', f'const unsigned gPortHostSymbolCount = {len(declarations)};']
path = pathlib.Path(sys.argv[2])
path.parent.mkdir(parents=True, exist_ok=True)
path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
