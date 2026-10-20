"""Independent fixture copies, with APFS cloning and a buffered fallback."""
import ctypes
import functools
import os
import platform
import shutil
import stat
import hashlib
from pathlib import Path


def isolate_thumbnail_copies(user, backup):
    """Move only this private run's FM4 thumbnail cache aside; retain hashes."""
    user, backup = Path(user), Path(backup)
    moved = {}
    for path, is_directory in list(fixture_entries(user)):
        if is_directory:
            continue
        relative = path.relative_to(user)
        if (relative.parts[-5:-1] != ('4D530910', '00000001', 'ForzaProfile', 'Thumbnails')
                or not path.name.startswith('Thumbnail_') or path.suffix != '.xdc'):
            continue
        target = backup / relative
        if target.exists():
            raise ValueError(f"Thumbnail backup already exists: {target}")
        target.parent.mkdir(parents=True, exist_ok=True)
        moved[str(relative)] = hashlib.sha256(path.read_bytes()).hexdigest()
        path.rename(target)
    return moved


def fixture_entries(root):
    """Yield (path, is_directory) using stat rather than unreliable d_type hints."""
    pending = [Path(root)]
    while pending:
        directory = pending.pop()
        with os.scandir(directory) as entries:
            for entry in entries:
                mode = entry.stat(follow_symlinks=False).st_mode
                path = Path(entry.path)
                if stat.S_ISDIR(mode):
                    yield path, True
                    pending.append(path)
                elif stat.S_ISREG(mode):
                    yield path, False
                else:
                    raise ValueError(f"Unsupported fixture entry (expected ordinary file/directory): {path}")


def remove_fixture_tree(root):
    """Remove an owned payload without relying on directory entry type hints."""
    root = Path(root)
    if root.is_symlink():
        raise ValueError(f"Refusing to remove a linked payload: {root}")
    entries = list(fixture_entries(root))
    for path, is_directory in entries:
        if not is_directory:
            path.unlink()
    for path in sorted((p for p, is_dir in entries if is_dir), key=lambda p: len(p.parts), reverse=True):
        path.rmdir()
    root.rmdir()


def copy_fixture_tree(source, destination):
    source, destination = Path(source), Path(destination)
    destination.mkdir(parents=True, exist_ok=False)
    for path, is_directory in fixture_entries(source):
        target = destination / path.relative_to(source)
        if is_directory:
            target.mkdir(parents=True, exist_ok=True)
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            copy_fixture_file(path, target)


@functools.lru_cache(maxsize=1)
def _clonefile():
    library = ctypes.CDLL('/usr/lib/libSystem.B.dylib', use_errno=True)
    clone = library.clonefile
    clone.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int)
    clone.restype = ctypes.c_int
    return clone


def copy_fixture_file(source, destination, *, progress=None, hasher=None):
    """Copy without hard links; optionally hash and report bytes in the same pass.

    Explicit buffered I/O avoids macOS fcopyfile's opaque fallback on external
    filesystems, and permits progress while a large content archive is copied.
    """
    if os.path.exists(destination) and os.path.samefile(source, destination):
        raise shutil.SameFileError(f'{source!r} and {destination!r} are the same file')
    if platform.system() == 'Darwin':
        if _clonefile()(os.fsencode(source), os.fsencode(destination), 0) == 0:
            if hasher is not None:
                with open(destination, 'rb') as copied:
                    while block := copied.read(4 * 1024 * 1024):
                        hasher.update(block)
                        if progress:
                            progress(len(block))
            elif progress:
                progress(os.stat(source).st_size)
            return destination
    with open(source, 'rb') as original, open(destination, 'wb') as copied:
        while block := original.read(4 * 1024 * 1024):
            copied.write(block)
            if hasher is not None:
                hasher.update(block)
            if progress:
                progress(len(block))
    shutil.copystat(source, destination)
    return destination
