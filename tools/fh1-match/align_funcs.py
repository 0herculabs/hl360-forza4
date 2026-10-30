"""Aligns one FH1 function with its FM4 match and prints where they differ.

Pairs instructions with the same normalised form (see match_funcs.py) and
prints the pairs whose raw words differ: changed struct offsets, constants and
call targets. With --all every aligned pair is printed.

Usage: python -I align_funcs.py <fh1 image dir> <fm4 image dir> <fh1 addr> <fm4 addr> [--all]
"""

import difflib
import sys

import capstone

from match_funcs import BASE, load, norm, words

md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)


def text(w, addr):
    insns = list(md.disasm(w.to_bytes(4, "big"), addr))
    return f"{insns[0].mnemonic} {insns[0].op_str}" if insns else f".long {w:08x}"


def main():
    fh1_dir, fm4_dir, a, b = sys.argv[1:5]
    show_all = "--all" in sys.argv
    a, b = int(a, 16), int(b, 16)
    fh1, fh1_funcs = load(fh1_dir)
    fm4, fm4_funcs = load(fm4_dir)
    wa = words(fh1, a, fh1_funcs[a])
    wb = words(fm4, b, fm4_funcs[b])
    sm = difflib.SequenceMatcher(None, [norm(w) for w in wa], [norm(w) for w in wb], autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            for k in range(i2 - i1):
                x, y = wa[i1 + k], wb[j1 + k]
                if show_all or x != y:
                    pa, pb = a + 4 * (i1 + k), b + 4 * (j1 + k)
                    print(f"{pa:08X} {text(x, pa):<34} | {pb:08X} {text(y, pb)}")
        elif show_all:
            print(f"-- {tag} FH1 {i2 - i1} / FM4 {j2 - j1}")


if __name__ == "__main__":
    main()
