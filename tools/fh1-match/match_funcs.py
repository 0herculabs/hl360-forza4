"""Finds the FM4 functions that correspond to known FH1 functions.

FH1 is built on the FM4 engine, so many functions are the same code with
different struct offsets. Each function (bounds from .pdata) is normalised:
opcodes and registers stay, displacements, immediates and branch targets go.
Exact normalised matches are reported first, then the closest fuzzy matches.

Usage: python -I match_funcs.py <fh1 image dir> <fm4 image dir> <fh1 address>...
Each image dir holds fm4tool's image.bin and sections.txt.
"""

import difflib
import struct
import sys
from collections import Counter, defaultdict

BASE = 0x82000000
D_FORM = set(range(7, 16)) | set(range(24, 30)) | set(range(32, 56)) | {58, 62}


def load(dir_):
    data = open(dir_ + "/image.bin", "rb").read()
    pdata = None
    for line in open(dir_ + "/sections.txt"):
        parts = line.split()
        if parts and parts[0] == ".pdata":
            pdata = (int(parts[1], 16), int(parts[2], 16))
    funcs = {}
    start, size = pdata
    for i in range(0, size, 8):
        begin, info = struct.unpack(">II", data[start - BASE + i:start - BASE + i + 8])
        length = (info >> 8) & 0x3FFFFF
        if begin and length:
            funcs[begin] = length
    return data, funcs


def words(data, addr, count):
    off = addr - BASE
    return list(struct.unpack(">%dI" % count, data[off:off + 4 * count]))


def norm(w):
    op = w >> 26
    if op in D_FORM:
        return w & 0xFFFF0000
    if op == 18:
        return w & 0xFC000003
    if op == 16:
        return w & 0xFFFF0003
    return w


def signature(data, addr, length):
    return tuple(norm(w) for w in words(data, addr, length))


def main():
    fh1_dir, fm4_dir = sys.argv[1], sys.argv[2]
    targets = [int(a.replace("sub_", ""), 16) for a in sys.argv[3:]]
    fh1, fh1_funcs = load(fh1_dir)
    fm4, fm4_funcs = load(fm4_dir)
    by_sig = defaultdict(list)
    fm4_sigs = {}
    for addr, length in fm4_funcs.items():
        sig = signature(fm4, addr, length)
        fm4_sigs[addr] = sig
        by_sig[sig].append(addr)
    for target in targets:
        if target not in fh1_funcs:
            print(f"{target:08X}: not a function start in FH1")
            continue
        sig = signature(fh1, target, fh1_funcs[target])
        exact = by_sig.get(sig, [])
        print(f"== FH1 {target:08X} ({len(sig)} insns): exact {['%08X' % a for a in exact]}")
        if len(exact) == 1:
            continue
        hist = Counter(w >> 26 for w in sig)
        scored = []
        for addr, other in fm4_sigs.items():
            if not (0.7 * len(sig) <= len(other) <= 1.3 * len(sig) + 2):
                continue
            oh = Counter(w >> 26 for w in other)
            common = sum((hist & oh).values())
            if common < 0.6 * max(len(sig), len(other)):
                continue
            scored.append((difflib.SequenceMatcher(None, sig, other, autojunk=False).ratio(), addr))
        scored.sort(reverse=True)
        for ratio, addr in scored[:5]:
            print(f"   {addr:08X} len {fm4_funcs[addr]} ratio {ratio:.3f}")


if __name__ == "__main__":
    main()
