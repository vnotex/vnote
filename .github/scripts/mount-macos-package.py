#!/usr/bin/env python3
"""Mount the signed CI package, recovering only a clean transient attach failure."""

import argparse
import os
from pathlib import Path
import plistlib
import subprocess
import sys
import tempfile
import time


TRANSIENT_ERROR = b"hdiutil: attach failed - Resource temporarily unavailable"


def run_logged(arguments, stem, timeout):
    try:
        result = subprocess.run(
            ["hdiutil", *arguments], capture_output=True, timeout=timeout,
            env=dict(os.environ, LC_ALL="C"))
    except subprocess.TimeoutExpired as error:
        # subprocess.run kills/reaps only its own child, not shared disk helpers.
        stem.with_suffix(".stdout").write_bytes(error.stdout or b"")
        stem.with_suffix(".stderr").write_bytes(error.stderr or b"")
        stem.with_suffix(".status").write_text("timeout\n")
        raise
    stem.with_suffix(".stdout").write_bytes(result.stdout)
    stem.with_suffix(".stderr").write_bytes(result.stderr)
    stem.with_suffix(".status").write_text(f"{result.returncode}\n")
    return result


def require_unattached(image, mountpoint, stem):
    result = run_logged(["info", "-plist"], stem, 15)
    if result.returncode:
        raise RuntimeError("Cannot determine disk-image attachment state")
    info = plistlib.loads(result.stdout)
    if not isinstance(info, dict) or not isinstance(info.get("images"), list):
        raise RuntimeError("Missing images array in hdiutil info")
    for entry in info["images"]:
        path = entry.get("image-path") if isinstance(entry, dict) else None
        if not isinstance(path, str) or not path or not Path(path).is_absolute():
            raise RuntimeError("Missing or invalid image-path in hdiutil info")
        # Any matching image is attached, even a dev-entry without a mount-point.
        # Never reuse or detach an existing/partial attachment, wherever mounted.
        if Path(path).resolve(strict=True) == image:
            raise RuntimeError("Package image is already or partially attached")
    if os.path.ismount(mountpoint) or any(Path(mountpoint).iterdir()):
        raise RuntimeError("Package mountpoint is mounted or nonempty")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("mountpoint")
    args = parser.parse_args()
    image = args.image.resolve(strict=True)
    diagnostics = Path(os.environ["RUNNER_TEMP"]) / "vnote-package-diagnostics"
    diagnostics.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="dmg-attach-", dir=diagnostics))
    print(f"Disk-image mount diagnostics: {directory}", flush=True)
    # The caller owns the successful mount and its existing EXIT detach trap.
    # Reject a preexisting mountpoint; retries reuse this directory unchanged.
    Path(args.mountpoint).mkdir()
    require_unattached(image, args.mountpoint, directory / "before-attach")
    for attempt in range(1, 4):
        print(f"hdiutil attach attempt {attempt}: {image}", flush=True)
        result = run_logged(
            ["attach", str(image), "-readonly", "-nobrowse", "-mountpoint", args.mountpoint],
            directory / f"attach-{attempt}", 60)
        if result.stdout:
            print(result.stdout.decode(errors="replace"), end="", flush=True)
        if result.stderr:
            print(result.stderr.decode(errors="replace"), end="", file=sys.stderr, flush=True)
        if result.returncode == 0:
            if attempt > 1:
                print(f"::warning::DMG attach recovered on attempt {attempt}; "
                      f"original diagnostics retained in {directory}", flush=True)
            return 0
        # Save state before deciding whether another attach is safe. A failed
        # info command, unknown schema or partial attachment must fail closed.
        require_unattached(image, args.mountpoint, directory / f"after-attach-{attempt}")
        if result.stderr.strip() != TRANSIENT_ERROR or attempt == 3:
            raise RuntimeError(f"DMG attach failed on attempt {attempt}; not retrying")
        delay = attempt * 2
        print(f"::warning::DMG attach temporarily unavailable; retrying in {delay}s "
              f"after confirming the image is unattached", flush=True)
        time.sleep(delay)
    return 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"DMG mount failed: {error}", file=sys.stderr)
        sys.exit(1)
