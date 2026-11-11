"""Game-side callers of chosen D3D functions (outside the D3D block).

Usage: python -I callers.py <pd generated dir> <lo hex> <hi hex> <toml> name [name ...]
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from match_d3d import index, parse_names  # noqa: E402

pd = index(sys.argv[1])
lo, hi = int(sys.argv[2], 16), int(sys.argv[3], 16)
names = {nm: k for k, (nm, c) in parse_names(sys.argv[4]).items()}
inside = lambda n: lo <= int(n[4:], 16) < hi
for want in sys.argv[5:]:
    target = names.get(want) or ('sub_' + want.upper() if len(want) == 8 else None)
    if not target:
        print(want, 'not named')
        continue
    sites = collections.Counter(n for n, f in pd.items() if not inside(n) for c in f['calls'] if c == target)
    print('%s (%s): %d game call sites in %d functions: %s' % (
        want, target[4:], sum(sites.values()), len(sites),
        ', '.join('%s x%d' % (n[4:], k) for n, k in sorted(sites.items()))[:400]))
