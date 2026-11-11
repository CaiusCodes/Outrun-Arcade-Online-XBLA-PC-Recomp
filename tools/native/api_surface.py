"""Which D3D-library functions Perfect Dark's own code calls, named or not.

The D3D library is one block of the image (the named functions' range). Every
function in that block that code outside it calls is part of the API the game
uses: what a native renderer has to cover. Prints them with their name (if any)
and number of game call sites, then the unnamed ones' imports and size.

Usage: python -I api_surface.py <pd generated dir> <lo hex> <hi hex> <toml> [<toml> ...]
(lo/hi: the D3D block; names outside it are left out as unreliable.)
"""
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from match_d3d import index, parse_names, pm4_packets  # noqa: E402

pd = index(sys.argv[1])
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
inside = lambda n: lo <= int(n[4:], 16) < hi
named = {}
for t in sys.argv[4:]:
    for k, (nm, conf) in parse_names(t).items():
        if inside(k):
            named.setdefault(k, nm)
callers = collections.defaultdict(list)
for n, f in pd.items():
    if inside(n):
        continue
    for c in f['calls']:
        if inside(c):
            callers[c].append(n)
print('D3D block 0x%08X-0x%08X; %d functions in it; game code calls %d of them' % (
    lo, hi, sum(inside(n) for n in pd), len(callers)))
unnamed = []
for c in sorted(callers, key=lambda c: -len(callers[c])):
    nm = named.get(c)
    sites = len(callers[c])
    distinct = len(set(callers[c]))
    if nm:
        print('  %s %-48s %4d calls from %d functions' % (c[4:], nm, sites, distinct))
    else:
        unnamed.append(c)
print('Unnamed but called by the game: %d' % len(unnamed))
for c in unnamed:
    f = pd.get(c, {'asm': [], 'imps': [], 'calls': []})
    print('  %s ? %4d calls from %d functions, %d instructions, imports %s, pm4 %s, calls %s' % (
        c[4:], len(callers[c]), len(set(callers[c])), len(f['asm']), ','.join(sorted(set(f['imps']))) or '-',
        ','.join(sorted(pm4_packets(f['asm']))) or '-',
        ','.join(sorted(set(named.get(x, x[4:]) for x in f['calls'])))[:160] or '-'))
