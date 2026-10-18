# Project Forza Plus 4 integration

Forza Plus is managed in Installation, Step 3; there is no separate mod tab.
The sidebar uses separate green installed indicators for the game and Forza
Plus; verification still runs internally during installation.
Normal/Hard/Sandbox/Vanilla are installation-time choices. To change the mode,
close the game, press Uninstall Forza Plus, then install again with the new mode.

Uninstall selects the original installation before removing the separate mod
game files and its shared title content. Original game files/content and profile
saves remain intact. Mod saves/settings/caches stay in its user root, with an
explicit preserved marker that permits reinstalling content alongside them.
The uninstaller preflights absolute deletion roots, checks installation markers
and refuses reparse points/junctions before any deletion. Regression fixtures
cover uninstall, reinstall with a different mode, save preservation, original
content preservation and junction rejection.

The supplied mod documents additional camera FOV changes, an optional harder-AI
file and PhysicsSettings.ini replacements for high-FPS operation. These are not
yet exposed as runtime options. Implementing them requires reversible updates,
receipt/signature handling and gameplay validation, especially at 120 Hz.

The inspected local archives are `Project Forza Plus 4 v1.3.zip` and
`Project Forza Plus 4 Update v1.3.zip`. Their included installation instructions
target Xenia and require replacing the executable and installing extracted DLC.
The base archive contains about 9.56 GB uncompressed, including 20 content
packages. It is supplied separately by the user.

## Native adaptation

The mod's executable has module hash `7C40571DE93038D2` and entry point
`0x82759E30`; this port's USA Rev 1 executable has hash `D1CCBADBE3A1B3B7`
and entry point `0x8275F9F0`. Their data and code layouts differ substantially.
The installer therefore keeps the executable used for recompilation.

FM4 USA Rev 1 exposes protected-file resource `0D163575` at `0x82EA0000`,
size `0x5771`. Its big-endian index contains a 32 KiB block size and 66
offset/size entries, followed by root-relative NUL-terminated filenames and
SHA-256 hashes for each file block. FM3's SHA-1/27-entry table is not compatible.
At launch, only an installation carrying `.fm4-plus-ready` rebuilds these
signatures from disk. A new guest system-heap resource is allocated, and both
the host and guest XEX resource directories are updated. The adjacent title
dashboard resource at `0x82EA5780` is preserved. Guest code and the disk XEX
are not patched.

The installer copies the original game and installed shared content into
separate staging folders, then overlays the mod's main Media data and selected
career database. Selected-mode DLC databases override base content. The update
overrides base mode databases, audio and physics data. Main RenderScenarios
remain owned by the port so its shadow workaround and backups stay consistent.
Only packages with existing runtime headers are copied from the mod. Profiles,
saves, caches, alternative frame-rate physics, harder AI and Xenia settings are
not imported. Publishing the completed copies makes them selectable; incomplete
staging folders are ignored on retry.

Game and content changes have separate SHA-256 receipts. Full verification
merges the game receipt with the original Disc 1 manifest and shadow-workaround
rules, preserving the original executable checks, and verifies mod content too.
Selecting the original installation restores its original user root, while
Forza Plus uses its own root for saves and caches.

## Validation

- CTest fixtures cover all four modes, update precedence, original executable
  and render preservation, separate saves, content-header preservation,
  missing-header package omission, existing-destination refusal and ZIP traversal.
- The supplied base plus update installed in Normal mode against the local
  Disc 1 and four licensed Disc 2 packages. Sixteen other packages were omitted.
- Full verification: zero missing files, wrong sizes or wrong hashes in the
  game; 508 changed content entries verified.
- A native smoke run rebuilt the 66-entry resource (24 files changed size),
  loaded XMediaFacade, reached the first present call and closed with exit 0.

These checks establish installation and initial startup. They do not establish
complete gameplay compatibility, correctness of every mod feature, or equivalence
to the mod's patched executable. Races, career progression and long-term save
compatibility still require validation.
