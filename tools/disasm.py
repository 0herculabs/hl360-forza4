"""Disassembles a range of the dumped FM4 image (work/image/image.bin).

Usage: python disasm.py <address> [count] [--before N]
"""

import argparse
import os
import sys

import capstone

BASE = 0x82000000
IMAGE = os.path.join(os.path.dirname(__file__), "..", "work", "image", "image.bin")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("address", type=lambda s: int(s, 16))
    ap.add_argument("count", type=int, nargs="?", default=16)
    ap.add_argument("--before", type=int, default=0)
    ap.add_argument("--image", default=IMAGE)
    ap.add_argument("--base", type=lambda s: int(s, 16), default=BASE)
    args = ap.parse_args()

    with open(args.image, "rb") as f:
        data = f.read()
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    start = args.address - 4 * args.before
    for i in range(args.before + args.count):
        addr = start + 4 * i
        off = addr - args.base
        word = data[off:off + 4]
        insns = list(md.disasm(word, addr))
        text = f"{insns[0].mnemonic} {insns[0].op_str}" if insns else ".long"
        mark = ">" if addr == args.address else " "
        print(f"{mark}{addr:08X}  {word.hex()}  {text}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

