# gb_rom_variant.py <in.gb> <out.gb> [title=STR] [country=N] [ram=N] [cart=0xNN] [flip=OFFSET]: a Game Boy ROM with a changed header (header checksum recomputed; flip= changes one byte without fixing the global checksum). Scratch files only, never commit ROMs.
import sys
rom = bytearray(open(sys.argv[1], 'rb').read())
for kv in sys.argv[3:]:
    k, v = kv.split('=', 1)
    if k == 'title':
        rom[0x134:0x143] = v.encode().ljust(15, b'\0')[:15]
    elif k == 'country':
        rom[0x14A] = int(v, 0)
    elif k == 'ram':
        rom[0x149] = int(v, 0)
    elif k == 'cart':
        rom[0x147] = int(v, 0)
    elif k == 'flip':
        rom[int(v, 0)] ^= 0x5A
s = 0
for i in range(0x134, 0x14D):
    s = (s - rom[i] - 1) & 0xFF
rom[0x14D] = s
open(sys.argv[2], 'wb').write(rom)
