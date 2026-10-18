#!/usr/bin/env python3
"""Compile real Typst output using an extracted package's helper.

Usage: python scripts/e2e/smoke_typst_package.py --helper <packaged-helper>
Run with developer Cargo/Qt/Typst overrides removed. CI supplies only packaged
library search paths. The helper receives an absolute disposable project root;
no source, font, or package is downloaded or installed by this smoke.
"""

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import time


TIMEOUT = 120
MAX_HEADER = 16 * 1024 * 1024
MAX_BINARY = 128 * 1024 * 1024
MAX_ID = (1 << 53) - 1
SOURCE = "#set page(width: 200pt, height: 100pt, margin: 10pt)\n= Package smoke\n"


class ProtocolError(RuntimeError):
    pass


def smoke(helper):
    helper = helper.resolve(strict=True)
    if not helper.is_file() or not os.access(helper, os.X_OK):
        raise ProtocolError(f"Helper is not an executable file: {helper}")

    with tempfile.TemporaryDirectory(prefix="vnote-typst-package-") as directory, \
            tempfile.TemporaryFile() as errors:
        deadline = time.monotonic() + TIMEOUT
        process = subprocess.Popen(
            [str(helper)], cwd=helper.parent, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=errors,
        )
        expired = threading.Event()

        def remaining():
            seconds = deadline - time.monotonic()
            if expired.is_set() or seconds <= 0:
                raise TimeoutError(f"Typst compiler exceeded {TIMEOUT} seconds")
            return seconds

        def expire():
            expired.set()
            # This process handle belongs only to the smoke, never to a VNote tab.
            try:
                process.kill()
            except ProcessLookupError:
                pass

        timer = threading.Timer(max(0, deadline - time.monotonic()), expire)
        timer.daemon = True
        timer.start()
        try:
            def send(operation, request, **fields):
                remaining()
                header = json.dumps(dict(
                    v=1, epoch=1, generation=1, request=request, op=operation, **fields,
                ), separators=(",", ":")).encode("utf-8")
                if not 0 < len(header) <= MAX_HEADER:
                    raise ProtocolError("Request header exceeds the protocol limit")
                process.stdin.write(struct.pack(">II", len(header), 0) + header)
                process.stdin.flush()
                remaining()

            def read_exact(size):
                data = bytearray()
                while len(data) < size:
                    remaining()
                    chunk = process.stdout.read(min(size - len(data), 64 * 1024))
                    if not chunk:
                        remaining()
                        raise ProtocolError("Unexpected EOF from the Typst helper")
                    data.extend(chunk)
                remaining()
                return data

            def receive(operation, request, binary=False):
                header_size, binary_size = struct.unpack(">II", read_exact(8))
                if not 0 < header_size <= MAX_HEADER or binary_size > MAX_BINARY:
                    raise ProtocolError("Response frame exceeds protocol size limits")
                if binary_size and not binary:
                    raise ProtocolError(f"Unexpected binary payload for {operation}")
                header = json.loads(read_exact(header_size))
                if not isinstance(header, dict):
                    raise ProtocolError("Response header is not a JSON object")
                if header.get("op") == "error":
                    raise ProtocolError(f"Compiler protocol error: {header.get('message')}")
                if header.get("op") != operation:
                    raise ProtocolError(f"Expected {operation}, received {header.get('op')!r}")
                for field, expected in (("v", 1), ("epoch", 1), ("generation", 1),
                                        ("request", request)):
                    if type(header.get(field)) is not int or header[field] != expected:
                        raise ProtocolError(f"Invalid response {field}: {header.get(field)!r}")
                if operation in ("parsed", "compiled") and header.get("diagnostics") != []:
                    raise ProtocolError(f"Compiler diagnostics: {header.get('diagnostics')!r}")
                return header, read_exact(binary_size)

            config = {
                "projectRoot": str(Path(directory).resolve()),
                "mainFile": "main.typst",
                "allowDownloads": False,
            }
            send("initialize", 1, config=config)
            receive("ready", 1)
            send("compile", 2, config=config, activeFile="main.typst", snapshots=[],
                 overlays=[{"file": "main.typst", "text": SOURCE}])
            receive("parsed", 2)
            compiled, _ = receive("compiled", 2)
            if compiled.get("pages") != [[200, 100]]:
                raise ProtocolError(f"Expected one 200x100pt page: {compiled.get('pages')!r}")
            snapshot = compiled.get("snapshot")
            if type(snapshot) is not int or not 1 <= snapshot <= MAX_ID:
                raise ProtocolError("Compilation did not produce a valid snapshot")
            start = SOURCE.index("= Package smoke")
            expected_heading = {
                "title": "Package smoke", "level": 1, "file": "main.typst",
                "start": start, "end": start + len("= Package smoke"),
            }
            headings = compiled.get("headings")
            if not isinstance(headings, list) or len(headings) != 1 or not isinstance(headings[0], dict):
                raise ProtocolError(f"Expected one evaluated heading: {headings!r}")
            if any(headings[0].get(key) != value for key, value in expected_heading.items()):
                raise ProtocolError(f"Incorrect evaluated heading/source range: {headings[0]!r}")

            send("pdf", 3, snapshot=snapshot)
            pdf_header, pdf = receive("pdf", 3, binary=True)
            if pdf_header.get("snapshot") != snapshot or not pdf.startswith(b"%PDF-"):
                raise ProtocolError("Snapshot export did not return its PDF")
            send("shutdown", 4)
            process.stdin.close()
            code = process.wait(timeout=remaining())
            remaining()
            if code != 0:
                raise ProtocolError(f"Compiler shutdown exited with {code}")
            if process.stdout.read(1):
                raise ProtocolError("Unexpected output after compiler shutdown")
            print(f"PASS: {helper}: 200x100pt PDF, evaluated heading/source range, shutdown exit 0")
        except (OSError, ValueError, ProtocolError, TimeoutError, subprocess.TimeoutExpired):
            errors.seek(0)
            detail = errors.read(8192).decode("utf-8", errors="replace").strip()
            if detail:
                print(f"Typst helper stderr: {detail}", file=sys.stderr)
            raise
        finally:
            timer.cancel()
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
            process.stdin.close()
            process.stdout.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--helper", required=True, type=Path)
    args = parser.parse_args()
    try:
        smoke(args.helper)
        return 0
    except (OSError, ValueError, ProtocolError, TimeoutError, subprocess.TimeoutExpired) as error:
        print(f"Typst package smoke failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
