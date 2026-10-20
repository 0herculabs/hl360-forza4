# macOS SDK checks

`python3 tools/Build-Mac.py --stage smoke` compiles the actual FM4 app hooks,
including wheel integration, with a stub image declaration. The stub cannot
boot the game. The standalone test runs Vulkan fill/fence/readback checks.
`--window-smoke` additionally exercises presentation and resize with a visible
window for 60 seconds. GPU checks require host Metal device access.

The harness was carried from hl360-forza3 commit 3865642, which adapted
DarthMDev/fm4-recomp's game-independent harness. Captured shader and gameplay
results from other ports do not establish FM4 support.
