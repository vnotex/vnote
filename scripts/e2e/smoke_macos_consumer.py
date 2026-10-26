#!/usr/bin/env python3
"""Launch a signed macOS package on its matching native CI architecture.

Cold Rosetta translation is not application startup; CI runs this same 30-second
consumer gate on separate native arm64 and x86_64 hosts using the same signed DMG.
"""

import argparse
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile


def run_packaged_consumer(command, directory, environment, architecture):
    process = subprocess.Popen(command, cwd=directory, env=environment,
                               start_new_session=True)
    try:
        status = process.wait(timeout=30)
    except subprocess.TimeoutExpired as original_timeout:
        diagnostics = Path(os.environ['RUNNER_TEMP']) / 'vnote-package-diagnostics'
        try:
            diagnostics.mkdir(parents=True, exist_ok=True)
            stem = diagnostics / f'consumer-{architecture}'
            # Keep the privileged sample's destination runner-owned/readable.
            stem.with_suffix('.rosetta.txt').touch()
            with stem.with_suffix('.log').open('w') as output:
                for diagnostic in (
                    ['sample', str(process.pid), '2', '-file', str(stem.with_suffix('.sample.txt'))],
                    ['screencapture', '-x', str(stem.with_suffix('.png'))],
                    ['/usr/sbin/lsof', '-nP', '-p', str(process.pid)],
                    ['/usr/bin/vmmap', str(process.pid)],
                    ['sudo', '-n', '/usr/bin/sample', 'oahd', '2',
                     '-file', str(stem.with_suffix('.rosetta.txt'))],
                ):
                    output.write(' '.join(diagnostic) + '\n')
                    output.flush()
                    try:
                        observed = subprocess.run(diagnostic, stdout=output,
                            stderr=subprocess.STDOUT, timeout=10)
                        output.write(f'diagnostic exit={observed.returncode}\n')
                    except (OSError, subprocess.TimeoutExpired) as error:
                        output.write(f'diagnostic unavailable: {error}\n')
        except OSError as error:
            print(f'Could not retain consumer diagnostics: {error}', file=sys.stderr)
        finally:
            # Only the timed-out, still-unreaped child's own process group.
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            except OSError as error:
                print(f'Could not stop consumer process group: {error}', file=sys.stderr)
            try:
                process.wait(timeout=10)
            except (OSError, subprocess.TimeoutExpired) as error:
                print(f'Could not reap timed-out consumer: {error}', file=sys.stderr)
        raise original_timeout
    if status:
        raise subprocess.CalledProcessError(status, command)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--architecture", required=True, choices=("arm64", "x86_64"))
    args = parser.parse_args()
    host_arch = subprocess.check_output(["/usr/bin/uname", "-m"], text=True).strip()
    if host_arch != args.architecture:
        raise SystemExit(f"Expected native {args.architecture} runner, got {host_arch}")
    executable = args.app.resolve(strict=True) / "Contents/MacOS/VNote"
    clean_env = {key: value for key, value in os.environ.items()
                 if not key.upper().startswith(("CARGO", "RUST", "QT", "QML", "TYPST", "VTEXTEDIT", "LD_", "DYLD"))
                 and key not in ("CMAKE_PREFIX_PATH", "LIBRARY_PATH", "PYTHONPATH", "PYTHONHOME")}
    clean_env["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
    with tempfile.TemporaryDirectory(prefix="vnote-consumer-") as directory:
        launch_env = dict(clean_env, HOME=directory, CFFIXED_USER_HOME=directory,
                          XDG_CONFIG_HOME=directory, XDG_DATA_HOME=directory,
                          XDG_CACHE_HOME=directory)
        run_packaged_consumer(["/usr/bin/arch", f"-{host_arch}", str(executable), "--help"],
                              directory, launch_env, host_arch)
    print(f"PASS: native {host_arch} packaged VNote --help", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
