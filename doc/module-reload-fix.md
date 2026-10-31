# Recompiled module reload fix

`patches/sdk/0005-module-unload-vfs-path.patch` fixes lookup of registered
recompiled modules during unload. Registrations are title-relative, while
`UserModule::path()` is a VFS absolute path. The previous lookup missed the
registration on unload and left the shared-library entry behind, causing the
next load to fail with `already loaded; refusing duplicate load` and the game's
dirty-disc error.

The patch resolves the registered path relative to the executable and compares
VFS absolute paths, while retaining the existing normalized-key lookup.

Regression check (run beside the matching runtime and module DLLs):

```text
fm4tool module-reload-test <game_root> <test_user_dir> <absolute_module_dll_path>
```

Use a disposable test user directory. The probe loads the real XEX and exercises
three XMediaFacade load/unload cycles with guest DLL entry calls disabled.
It also verifies that the module can be found by its VFS absolute path.

On 2026-10-17, the original runtime failed on cycle 2; the patched Release
runtime passed all three cycles against the Desktop FM4 Project Forza Plus root.
The patched `rexruntime.dll` was installed in Desktop/FM4 with a timestamped
backup of the original. A complete race and return to menus still needs gameplay
validation. Graphics settings and saves were not changed by this fix.

The shared SDK source was patched, but its installed SDK DLL was not replaced.
If a future project build copies that older installed DLL, rebuild/install the
patched SDK before distributing the runtime again.

## v0.3.10 regression correction

The v0.3.9 link step copied the older installed SDK runtime over the validated
DLL. The crash on 2026-10-19 at circuit exit reported duplicate loading of
xmediafacade_default.xex followed by XamShowDirtyDiscErrorUI and a breakpoint.
The shipped v0.3.9 runtime reproduced the failure on reload cycle 2.

A validated runtime DLL was then pinned in patches/sdk/runtime and copied after
the SDK DLLs. That pin has been removed: the SDK is now built from ReXGlue
v0.10.0 with patches 0001-0006 and 0008 applied, so its own runtime carries
this fix together with the later 0006 thunk reuse. Full circuit exit
validation still requires gameplay.
