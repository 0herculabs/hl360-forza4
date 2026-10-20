"""Apply an ordered SDK patch stack, including patches that extend earlier files."""
from pathlib import Path
import shutil
import subprocess
import tempfile


def apply_sdk_patches(source, patches):
    source = Path(source).resolve()
    patches = [Path(p).resolve() for p in patches]
    paths = set()
    for patch in patches:
        result = subprocess.run(["git", "apply", "--numstat", str(patch)],
                                cwd=source, capture_output=True, text=True, check=True)
        for line in result.stdout.splitlines():
            relative = Path(line.split("\t", 2)[2])
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError(f"Unsafe patch path: {relative}")
            paths.add(relative)
    # Find the applied prefix in disposable copies. Reverse newest first so a
    # later patch may legitimately extend a file introduced by an earlier one.
    # Unrelated working-tree edits are copied and preserved throughout.
    for count in range(len(patches), -1, -1):
        with tempfile.TemporaryDirectory(prefix="fm4-sdk-patches-") as directory:
            scratch = Path(directory)
            for relative in paths:
                original = source / relative
                if original.is_file():
                    target = scratch / relative
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(original, target)
            valid = True
            for patch in reversed(patches[:count]):
                result = subprocess.run(["git", "apply", "--reverse", str(patch)],
                                        cwd=scratch, capture_output=True)
                if result.returncode:
                    valid = False
                    break
            if not valid:
                continue
            for patch in patches:
                result = subprocess.run(["git", "apply", str(patch)],
                                        cwd=scratch, capture_output=True)
                if result.returncode:
                    valid = False
                    break
            if not valid:
                continue
        # Validate the complete suffix against a copy before touching the SDK.
        for patch in patches[count:]:
            print(f"+ git apply {patch}", flush=True)
            subprocess.run(["git", "apply", str(patch)], cwd=source, check=True)
        print(f"SDK patches: {count} already applied, {len(patches) - count} applied", flush=True)
        return count
    raise ValueError("SDK patch stack has conflicting or partially applied changes; source was not modified")
