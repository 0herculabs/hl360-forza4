"""Package the standalone SDL launcher for Finder without duplicating game data."""
from pathlib import Path
import plistlib
import shutil
import subprocess


def stage_launcher(package):
    package = Path(package).resolve()
    contents = package / "FM4 Launcher.app" / "Contents"
    executable = contents / "MacOS" / "fm4-launcher"
    executable.parent.mkdir(parents=True, exist_ok=True)
    (contents / "Resources").mkdir(exist_ok=True)
    temporary = executable.with_name(".fm4-launcher.new")
    shutil.copy2(package / "fm4-launcher", temporary)
    temporary.chmod(0o755)
    temporary.replace(executable)
    with (contents / "Info.plist").open("wb") as output:
        plistlib.dump({"CFBundleExecutable": "fm4-launcher",
                       "CFBundleIdentifier": "org.hl360.forza4.launcher",
                       "CFBundleName": "FM4 Launcher",
                       "CFBundleDisplayName": "FM4 Launcher",
                       "CFBundlePackageType": "APPL",
                       "CFBundleVersion": "1",
                       "CFBundleShortVersionString": "0.1",
                       "LSMinimumSystemVersion": "14.0",
                       "NSHighResolutionCapable": True}, output)
    subprocess.run(["codesign", "--force", "--sign", "-", str(contents.parent)], check=True)
    return contents.parent
