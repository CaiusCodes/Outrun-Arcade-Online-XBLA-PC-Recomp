"""Map GoldenEye's D3D device-struct offsets to Perfect Dark's.

GoldenEye's native renderer reads fields of the guest D3D device (August 2007 XDK) at fixed
offsets; Perfect Dark links the June 2009 XDK, where fields can sit elsewhere. For every
function named in both games (checkpoint 1), align the two instruction streams (opcodes and
registers, immediates masked) and, for aligned instructions, pair up the displacements of
memory operands `N(rX)` and the immediates of `addi rD,rX,N`. Votes are counted per GoldenEye
offset; a field the native renderer uses is then looked up with `query`.

Usage:
  python -I offset_map.py <ge generated dir> <ge d3d_functions.toml> <pd generated dir>
                          <pd d3d_functions_pd.toml> <out file> [offset ...]
Reads only. The out file lists, per GoldenEye offset, the Perfect Dark offsets seen and where.
"""
import collections
import difflib
import re
import sys

sys.path.insert(0, __import__('os').path.dirname(__file__))
from match_d3d import index  # noqa: E402

RX_MEM = re.compile(r'^(-?\d+)\((r\d+)\)$')
RX_NUM = re.compile(r'^-?\d+$')


def names(toml_path):
    out = {}
    with open(toml_path, encoding='utf-8') as f:
        for line in f:
            m = re.match(r'^0x([0-9A-Fa-f]{8})\s*=\s*\{\s*name\s*=\s*"([^"]+)"', line)
            if m:
                out[m.group(2)] = 'sub_' + m.group(1).upper()
    return out


def skeleton(line):
    """Opcode and registers only; numbers masked. Returns (key, [numbers with role])."""
    parts = line.split(' ', 1)
    op = parts[0]
    ops = parts[1].split(',') if len(parts) > 1 else []
    key = [op]
    nums = []
    for i, o in enumerate(ops):
        o = o.strip()
        m = RX_MEM.match(o)
        if m:
            key.append('M(%s)' % m.group(2))
            nums.append(('mem', int(m.group(1))))
        elif RX_NUM.match(o):
            key.append('N')
            nums.append(('imm', int(o)))
        elif o.startswith('0x'):
            key.append('T')
        else:
            key.append(o)
    return ' '.join(key), nums, op


def main():
    ge_gen, ge_toml, pd_gen, pd_toml, out_path = sys.argv[1:6]
    wanted = [int(x, 0) for x in sys.argv[6:]]
    ge = index(ge_gen)
    pd = index(pd_gen)
    ge_names = names(ge_toml)
    pd_names = names(pd_toml)
    votes = collections.defaultdict(collections.Counter)  # ge offset -> pd offset -> count
    where = collections.defaultdict(lambda: collections.defaultdict(set))
    pairs = 0
    for name, pd_fn in pd_names.items():
        ge_fn = ge_names.get(name)
        if not ge_fn or ge_fn not in ge or pd_fn not in pd:
            continue
        a = [skeleton(x) for x in ge[ge_fn]['asm']]
        b = [skeleton(x) for x in pd[pd_fn]['asm']]
        sm = difflib.SequenceMatcher(None, [x[0] for x in a], [x[0] for x in b], autojunk=False)
        pairs += 1
        for tag, i1, i2, j1, j2 in sm.get_opcodes():
            if tag != 'equal':
                continue
            for k in range(i2 - i1):
                ka, na, op = a[i1 + k]
                kb, nb, _ = b[j1 + k]
                for (ra, va), (rb, vb) in zip(na, nb):
                    if ra != 'mem' and not (ra == 'imm' and op in ('addi', 'addic')):
                        continue
                    if abs(va) < 64 and va == vb:
                        continue  # small fields at the same place say little
                    votes[va][vb] += 1
                    where[va][vb].add(name)
    with open(out_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# GoldenEye offset -> Perfect Dark offsets (votes) from %d function pairs\n' % pairs)
        for va in sorted(votes):
            if va < 64:
                continue
            row = ', '.join('%d x%d' % (vb, c) for vb, c in votes[va].most_common(4))
            f.write('%6d: %s\n' % (va, row))
    for w in wanted:
        print('%6d (0x%X):' % (w, w))
        for vb, c in (votes[w].most_common(5) if w in votes else []):
            print('    -> %6d (0x%X) x%d  e.g. %s' % (vb, vb, c, ', '.join(sorted(where[w][vb])[:4])))
        if w not in votes:
            near = sorted((v for v in votes if votes[v]), key=lambda v: abs(v - w))[:4]
            print('    no direct votes; nearest GoldenEye offsets seen:',
                  ', '.join('%d->%s' % (v, votes[v].most_common(1)[0][0]) for v in near))


if __name__ == '__main__':
    main()
