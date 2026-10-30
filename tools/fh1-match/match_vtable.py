"""Finds the FM4 vtable that corresponds to an FH1 vtable, and maps its slots.

Scans FM4 for runs of pointers into .text (vtables), compares each run's slot
functions (first words, normalised) with the FH1 vtable's by sequence
alignment, and prints the best candidates. With --map, prints the FH1 slot to
FM4 slot mapping for the best one and the first instructions of both methods.

Usage: python -I match_vtable.py <fh1 dir> <fm4 dir> <fh1 vtable> <slots> [--map] [--slots 0x19c,...]
"""

import difflib
import struct
import sys

from match_funcs import BASE, norm, words
from align_funcs import text


def sections(dir_):
    out = {}
    for line in open(dir_ + "/sections.txt"):
        p = line.split()
        if len(p) >= 3 and p[0].startswith("."):
            out[p[0]] = (int(p[1], 16), int(p[2], 16))
    return out


def method_sig(data, addr, n=6):
    try:
        return tuple(norm(w) for w in words(data, addr, n))
    except struct.error:
        return ()


def main():
    fh1_dir, fm4_dir, vt, slots = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), int(sys.argv[4])
    fh1 = open(fh1_dir + "/image.bin", "rb").read()
    fm4 = open(fm4_dir + "/image.bin", "rb").read()
    fm4_text = sections(fm4_dir)[".text"]
    fh1_slots = words(fh1, vt, slots)
    fh1_sigs = [method_sig(fh1, a) for a in fh1_slots]

    def in_text(p):
        return fm4_text[0] <= p < fm4_text[0] + fm4_text[1]

    # Runs of text pointers in FM4's read-only data.
    rdata = sections(fm4_dir)[".rdata"]
    all_words = words(fm4, rdata[0], rdata[1] // 4)
    runs, start = [], None
    for i, w in enumerate(all_words + [0]):
        if in_text(w):
            if start is None:
                start = i
        elif start is not None:
            if i - start >= slots // 3:
                runs.append((rdata[0] + 4 * start, i - start))
            start = None
    scored = []
    for addr, count in runs:
        sigs = [method_sig(fm4, a) for a in words(fm4, addr, count)]
        ratio = difflib.SequenceMatcher(None, fh1_sigs, sigs, autojunk=False).ratio()
        scored.append((ratio, addr, count, sigs))
    scored.sort(reverse=True)
    for ratio, addr, count, _ in scored[:5]:
        print(f"candidate {addr:08X} ({count} slots) ratio {ratio:.3f}")
    if "--map" not in sys.argv:
        return
    ratio, addr, count, sigs = scored[0]
    fm4_slots = words(fm4, addr, count)
    sm = difflib.SequenceMatcher(None, fh1_sigs, sigs, autojunk=False)
    mapping = {}
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag in ("equal", "replace") and i2 - i1 == j2 - j1:
            for k in range(i2 - i1):
                mapping[i1 + k] = j1 + k
    wanted = None
    if "--slots" in sys.argv:
        wanted = [int(s, 16) // 4 for s in sys.argv[sys.argv.index("--slots") + 1].split(",")]
    for i in (wanted if wanted is not None else sorted(mapping)):
        j = mapping.get(i)
        fa = fh1_slots[i]
        print(f"FH1 slot {4*i:#x} {fa:08X}  ->  " + (f"FM4 slot {4*j:#x} {fm4_slots[j]:08X}" if j is not None else "unmapped"))
        for k in range(6):
            left = text(words(fh1, fa + 4 * k, 1)[0], fa + 4 * k)
            right = text(words(fm4, fm4_slots[j] + 4 * k, 1)[0], fm4_slots[j] + 4 * k) if j is not None else ""
            print(f"    {left:<36} | {right}")


if __name__ == "__main__":
    main()
