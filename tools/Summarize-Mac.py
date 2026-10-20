#!/usr/bin/env python3
"""Summarize an explicitly selected scene segment; never stops the game."""
import argparse
import json
from pathlib import Path
from mac_performance import summarize

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("run", type=Path)
parser.add_argument("--start", type=float, required=True, help="Seconds since first guest frame hook")
parser.add_argument("--end", type=float)
parser.add_argument("--label", required=True, help="e.g. menu-idle or opening-race; label is supplied, not detected")
args = parser.parse_args()
if args.start < 0 or (args.end is not None and args.end <= args.start):
    parser.error("Use a nonnegative start and an end after start")
try:
    result = summarize(args.run, args.start, args.end, args.label)
    print(json.dumps(result, indent=2))
except (OSError, ValueError) as error:
    parser.exit(1, f"Summary failed: {error}\n")
