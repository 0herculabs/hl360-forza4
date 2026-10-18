"""Writes launcher/data/game_files.sha256 from an extracted Disc 1.

Usage: python tools/make_manifest.py [game/disc1]
"""

import hashlib
import os
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")


def main():
    game = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "game", "disc1")
    rows = []
    for folder, _, files in os.walk(game):
        for name in files:
            path = os.path.join(folder, name)
            rel = os.path.relpath(path, game).replace(os.sep, "/")
            if rel.startswith("$SystemUpdate/"):
                continue
            digest = hashlib.sha256()
            with open(path, "rb") as f:
                for chunk in iter(lambda: f.read(8 << 20), b""):
                    digest.update(chunk)
            rows.append((rel, os.path.getsize(path), digest.hexdigest()))
    rows.sort(key=lambda r: r[0].lower())
    out_path = os.path.join(ROOT, "launcher", "data", "game_files.sha256")
    with open(out_path, "w", newline="\n") as out:
        out.write("# Forza Motorsport 4 USA Rev 1, Disc 1 (title 4D530910, media 33E7B39F USA)"
                  " - files the port was built from\n")
        for rel, size, digest in rows:
            out.write(f"{digest} {size} {rel}\n")
    print(f"{len(rows)} files, {sum(r[1] for r in rows) / 1e9:.2f} GB -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
