"""Name Perfect Dark's statically linked Direct3D functions from GoldenEye's list.

GoldenEye XBLA (August 2007 XDK) has 313 D3D/XGraphics functions named by hand in
goldeneye-recomp/native/d3d_functions.toml. Perfect Dark XBLA links the June 2009 XDK,
so much of the same library code is in its image, at other addresses and sometimes
changed. Both games' ReXGlue output keeps every PowerPC instruction as a comment, so:

1. index each function's instructions and calls in both generated trees;
2. normalise them (branch and call targets, and address halves built with lis, masked);
3. match: identical normalised bodies first, then near ones (difflib), then along
   the call graph (a matched function's calls, in order, name the callees).

Usage: python -I match_d3d.py <ge generated dir> <ge d3d_functions.toml> <pd generated dir> <out dir>
Writes <out>/d3d_functions_pd.toml and <out>/match_report.txt. Reads only.
"""
import collections
import difflib
import glob
import math
import os
import re
import sys

RX_DEF = re.compile(r'^(?:DEFINE_REX_FUNC|PPC_FUNC_IMPL)\((?:__imp__)?(sub_[0-9A-F]{8})\)')
RX_CALL = re.compile(r'\b(sub_[0-9A-F]{8})\(ctx, base\)')
RX_IMP = re.compile(r'\b__imp__(\w+)\(ctx, base\)')
RX_HEX = re.compile(r'0x[0-9a-fA-F]+')
RX_REG = re.compile(r'^r\d+$')
# Mnemonics whose first operand is not a destination register.
STORES = re.compile(r'^(st|cmp|tw|td|dcb|icb|mt|sync|isync|eieio|b|bl|bc|nop|lwsync)')


def index(gen_dir):
    funcs = {}
    cur = None
    for path in sorted(glob.glob(os.path.join(gen_dir, '*.cpp'))):
        with open(path, encoding='utf-8', errors='replace') as f:
            for line in f:
                m = RX_DEF.match(line)
                if m:
                    cur = {'asm': [], 'calls': [], 'imps': []}
                    funcs[m.group(1)] = cur
                    continue
                if cur is None:
                    continue
                s = line.strip()
                if s.startswith('// '):
                    text = s[3:]
                    if re.match(r'^[a-z][a-z0-9.+-]*( |$)', text):
                        cur['asm'].append(text)
                else:
                    for m in RX_CALL.finditer(s):
                        cur['calls'].append(m.group(1))
                    for m in RX_IMP.finditer(s):
                        cur['imps'].append(m.group(1))
    return funcs


def normalise(asm):
    """Instructions with addresses masked: branch/call targets, and every immediate
    that uses a register holding a lis-built address half."""
    out = []
    lis_regs = set()
    for line in asm:
        parts = line.split(' ', 1)
        op = parts[0]
        ops = parts[1].split(',') if len(parts) > 1 else []
        if op.startswith('b') and any(RX_HEX.search(o) for o in ops):
            ops = [RX_HEX.sub('T', o) for o in ops]
            out.append(op + ' ' + ','.join(ops))
            continue
        if op == 'lis' and len(ops) == 2:
            lis_regs.add(ops[0])
            out.append('lis %s,H' % ops[0])
            continue
        uses_lis = False
        new_ops = []
        for o in ops:
            mm = re.match(r'^(-?\d+)\((r\d+)\)$', o)
            if mm and mm.group(2) in lis_regs:
                new_ops.append('L(%s)' % mm.group(2))
                uses_lis = True
            else:
                new_ops.append(o)
        if not uses_lis and op in ('addi', 'ori', 'oris', 'addis') and len(ops) == 3 and ops[1] in lis_regs:
            new_ops[2] = 'L'
            uses_lis = True
        if ops and RX_REG.match(ops[0]) and not STORES.match(op):
            if uses_lis and op in ('addi', 'ori', 'oris', 'addis'):
                lis_regs.add(ops[0])
            else:
                lis_regs.discard(ops[0])
        out.append(op + (' ' + ','.join(new_ops) if new_ops else ''))
    return out


def parse_names(toml_path):
    names = {}
    rx = re.compile(r'^0x([0-9A-Fa-f]{8}) = \{ name = "([^"]+)" \}\s*(?:#\s*(\w+))?')
    with open(toml_path, encoding='utf-8') as f:
        for line in f:
            m = rx.match(line)
            if m:
                names['sub_' + m.group(1).upper()] = (m.group(2), m.group(3) or '')
    return names


def histogram(norm):
    return collections.Counter(l.split(' ', 1)[0] for l in norm)


def cosine(a, b):
    dot = sum(a[k] * b.get(k, 0) for k in a)
    na = math.sqrt(sum(v * v for v in a.values()))
    nb = math.sqrt(sum(v * v for v in b.values()))
    return dot / (na * nb) if na and nb else 0.0


PM4_OPS = {0x10: 'NOP', 0x21: 'REG_RMW', 0x22: 'DRAW_INDX', 0x23: 'VIZ_QUERY', 0x25: 'SET_STATE',
           0x26: 'WAIT_FOR_IDLE', 0x27: 'IM_LOAD', 0x2B: 'IM_LOAD_IMMEDIATE', 0x2D: 'SET_CONSTANT',
           0x2E: 'LOAD_CONSTANT_CONTEXT', 0x2F: 'LOAD_ALU_CONSTANT', 0x34: 'DRAW_INDX_BIN',
           0x35: 'DRAW_INDX_2_BIN', 0x36: 'DRAW_INDX_2', 0x37: 'INDIRECT_BUFFER_PFD', 0x3B: 'INVALIDATE_STATE',
           0x3C: 'WAIT_REG_MEM', 0x3D: 'MEM_WRITE', 0x3E: 'REG_TO_MEM', 0x3F: 'INDIRECT_BUFFER',
           0x44: 'COND_EXEC', 0x45: 'COND_WRITE', 0x46: 'EVENT_WRITE', 0x48: 'ME_INIT',
           0x4A: 'SET_SHADER_BASES', 0x4B: 'SET_BIN_BASE_OFFSET', 0x4F: 'MEM_WRITE_CNTR', 0x50: 'SET_BIN_MASK',
           0x51: 'SET_BIN_SELECT', 0x52: 'WAIT_REG_EQ', 0x53: 'WAIT_REG_GTE', 0x54: 'INTERRUPT',
           0x55: 'SET_CONSTANT2', 0x56: 'SET_SHADER_CONSTANTS', 0x58: 'EVENT_WRITE_SHD',
           0x59: 'EVENT_WRITE_CFL', 0x5A: 'EVENT_WRITE_EXT', 0x5B: 'EVENT_WRITE_ZPD', 0x5C: 'WAIT_UNTIL_READ',
           0x5D: 'WAIT_IB_PFD_COMPLETE', 0x5E: 'CONTEXT_UPDATE', 0x64: 'XE_SWAP'}


def pm4_packets(asm):
    """PM4 type-3 packet headers built in a function (lis/ori/addi chains and li)."""
    regs, vals = {}, []
    for l in asm:
        m = re.match(r'^lis (r\d+),(-?\d+)$', l)
        if m:
            v = (int(m.group(2)) << 16) & 0xFFFFFFFF
            regs[m.group(1)] = v
            vals.append(v)
            continue
        m = re.match(r'^(ori|addi) (r\d+),(r\d+),(-?\d+)$', l)
        if m and m.group(3) in regs:
            v = (regs[m.group(3)] | int(m.group(4))) if m.group(1) == 'ori' else \
                (regs[m.group(3)] + int(m.group(4)))
            v &= 0xFFFFFFFF
            regs[m.group(2)] = v
            vals.append(v)
    return {PM4_OPS[(v >> 8) & 0x7F] for v in vals
            if (v >> 30) == 3 and (v & 0xFE) == 0 and ((v >> 8) & 0x7F) in PM4_OPS}


def main():
    ge_dir, ge_toml, pd_dir, out_dir = sys.argv[1:5]
    known_pd = parse_names(sys.argv[5]) if len(sys.argv) > 5 else {}  # e.g. the state tables
    os.makedirs(out_dir, exist_ok=True)
    ge = index(ge_dir)
    pd = index(pd_dir)
    names = parse_names(ge_toml)
    ge_norm = {n: normalise(f['asm']) for n, f in ge.items()}
    pd_norm = {n: normalise(f['asm']) for n, f in pd.items()}
    pd_hist = {n: histogram(v) for n, v in pd_norm.items()}
    by_hash = collections.defaultdict(list)
    for n, v in pd_norm.items():
        by_hash[hash(tuple(v))].append(n)

    found = {}  # ge name -> (pd func, how, score)
    taken = {}
    # 0. names already known in Perfect Dark (D3D's state tables): they win.
    ge_by_name = {nm: g for g, (nm, conf) in names.items()}
    for p, (nm, conf) in known_pd.items():
        g = ge_by_name.get(nm)
        if g and p in pd and g not in found and p not in taken:
            found[g] = (p, 'table', 0.0)
            taken[p] = g
    # 1. identical bodies (skip tiny ones: a few instructions match everywhere)
    for g, (nm, conf) in names.items():
        if g in found:
            continue
        if g not in ge_norm or len(ge_norm[g]) < 6:
            continue
        cands = by_hash.get(hash(tuple(ge_norm[g])), [])
        if len(cands) == 1 and cands[0] not in taken:
            found[g] = (cands[0], 'exact', 1.0)
            taken[cands[0]] = g

    # 2. near bodies
    pd_by_len = sorted(pd_norm, key=lambda n: len(pd_norm[n]))
    lens = [len(pd_norm[n]) for n in pd_by_len]
    import bisect
    for g, (nm, conf) in names.items():
        if g in found or g not in ge_norm or len(ge_norm[g]) < 12:
            continue
        a = ge_norm[g]
        ha = histogram(a)
        lo = bisect.bisect_left(lens, int(len(a) * 0.7))
        hi = bisect.bisect_right(lens, int(len(a) * 1.4) + 1)
        scored = []
        for n in pd_by_len[lo:hi]:
            if n in taken or cosine(ha, pd_hist[n]) < 0.9:
                continue
            sm = difflib.SequenceMatcher(None, a, pd_norm[n], autojunk=False)
            if sm.real_quick_ratio() < 0.7 or sm.quick_ratio() < 0.7:
                continue
            scored.append((sm.ratio(), n))
        scored.sort(reverse=True)
        if scored and scored[0][0] >= 0.75 and (len(scored) == 1 or scored[0][0] - scored[1][0] >= 0.04):
            found[g] = (scored[0][1], 'near', round(scored[0][0], 3))
            taken[scored[0][1]] = g

    # 3. along the call graph, until nothing new
    def follow_calls():
        changed = True
        while changed:
            changed = False
            for g, (p, how, score) in list(found.items()):
                gc, pc = ge[g]['calls'], pd[p]['calls']
                # Align the two call sequences by callee size; runs of equal length pair
                # position by position.
                for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(
                        None, [len(ge_norm.get(c, [])) for c in gc], [len(pd_norm.get(c, [])) for c in pc],
                        autojunk=False).get_opcodes():
                    if tag not in ('equal', 'replace') or (i2 - i1) != (j2 - j1):
                        continue
                    for k in range(i2 - i1):
                        gcallee, pcallee = gc[i1 + k], pc[j1 + k]
                        if gcallee in names and gcallee not in found and pcallee not in taken:
                            found[gcallee] = (pcallee, 'calls of ' + names[g][0], 0.0)
                            taken[pcallee] = gcallee
                            changed = True

    follow_calls()

    # 4. by behaviour: the same imports, PM4 packets and already-named callees,
    # weighed with code similarity, within the D3D part of the image (the matched
    # functions' address range, padded).
    ge_pm4 = {g: pm4_packets(ge[g]['asm']) for g in names if g in ge}
    pd_pm4 = {}
    matched_pd = [int(p[4:], 16) for p, _, _ in found.values()]
    lo_addr, hi_addr = min(matched_pd) - 0x8000, max(matched_pd) + 0x8000

    def feats(calls, imps, pm4, named):
        return {'i:' + x for x in imps} | {'p:' + x for x in pm4} | {'c:' + named[c] for c in calls if c in named}

    for rounds in range(3):
        ge_named = {g: names[g][0] for g in found}
        pd_named = {p: names[g][0] for g, (p, _, _) in found.items()}
        added = 0
        for g, (nm, conf) in names.items():
            if g in found or g not in ge or len(ge_norm[g]) < 8:
                continue
            fg = feats(ge[g]['calls'], ge[g]['imps'], ge_pm4[g], ge_named)
            if not fg:
                continue
            scored = []
            for p, f in pd.items():
                if p in taken or not lo_addr <= int(p[4:], 16) <= hi_addr:
                    continue
                n = len(pd_norm[p])
                if not 0.5 * len(ge_norm[g]) <= n <= 2.0 * len(ge_norm[g]) + 4:
                    continue
                if p not in pd_pm4:
                    pd_pm4[p] = pm4_packets(f['asm'])
                fp = feats(f['calls'], f['imps'], pd_pm4[p], pd_named)
                if not fp:
                    continue
                jac = len(fg & fp) / len(fg | fp)
                if jac < 0.5:
                    continue
                ratio = difflib.SequenceMatcher(None, ge_norm[g], pd_norm[p], autojunk=False).ratio()
                scored.append((0.5 * jac + 0.5 * ratio, jac, ratio, p))
            scored.sort(reverse=True)
            if scored and scored[0][0] >= 0.6 and (len(scored) == 1 or scored[0][0] - scored[1][0] >= 0.05):
                s, jac, ratio, p = scored[0]
                found[g] = (p, 'behaviour', round(s, 3))
                taken[p] = g
                added += 1
        follow_calls()
        if not added:
            break

    # report
    lines = ['# Perfect Dark XBLA (title 584109C2): Direct3D functions named from GoldenEye\'s list',
             '# (goldeneye-recomp native/d3d_functions.toml, August 2007 XDK) by matching normalised',
             '# code against Perfect Dark\'s (June 2009 XDK). exact = identical body; near = difflib',
             '# ratio shown; "calls of X" = the same call position inside matched function X.',
             '# The GoldenEye confidence is kept after it. Not wired into anything.', '', '[functions]', '']
    for g, (nm, conf) in sorted(names.items(), key=lambda kv: kv[1][0]):
        if g in found:
            p, how, score = found[g]
            detail = how if how not in ('near', 'behaviour') else '%s %.3f' % (how, score)
            lines.append('0x%s = { name = "%s" }  # %s; GoldenEye %s %s, %d vs %d instructions' % (
                p[4:], nm, detail, g[4:], conf, len(ge_norm.get(g, [])), len(pd_norm.get(p, []))))
    with open(os.path.join(out_dir, 'd3d_functions_pd.toml'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    hows = collections.Counter(h.split(' ')[0] if not h.startswith('calls') else 'call-graph'
                               for _, h, _ in found.values())
    missing = sorted(nm for g, (nm, c) in names.items() if g not in found)
    rep = ['GoldenEye names: %d (%d with code in its generated tree)' % (len(names), sum(g in ge for g in names)),
           'Perfect Dark functions indexed: %d; GoldenEye: %d' % (len(pd), len(ge)),
           'Matched: %d (%s)' % (len(found), ', '.join('%s %d' % kv for kv in hows.most_common())),
           'Not matched: %d' % len(missing), ''] + ['  ' + m for m in missing]
    with open(os.path.join(out_dir, 'match_report.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(rep) + '\n')
    print('\n'.join(rep[:4]))


if __name__ == '__main__':
    main()
