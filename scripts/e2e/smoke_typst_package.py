#!/usr/bin/env python3
"""Compile real Typst output through an extracted package's C ABI.

Usage: python scripts/e2e/smoke_typst_package.py --library <packaged-library>
The parent supervises a disposable Python worker with a wall-clock timeout. This
is test isolation, not a product executable fallback. No downloads are enabled.
"""

import argparse
import ctypes
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile


TIMEOUT = 120
MAX_HEADER = 16 * 1024 * 1024
MAX_BINARY = 128 * 1024 * 1024
MAX_ID = (1 << 53) - 1
SOURCE = "#set page(width: 200pt, height: 100pt, margin: 10pt)\n= Package smoke\n"
BYTE_POINTER = ctypes.POINTER(ctypes.c_uint8)
CALLBACK = ctypes.CFUNCTYPE(ctypes.c_int32, ctypes.c_void_p, ctypes.c_void_p)


class AbiError(RuntimeError):
    pass


def require_ok(status, operation):
    if status != 0:
        raise AbiError(f"{operation} returned ABI status {status}")


def smoke_worker(library):
    # CDLL uses cdecl, including on Windows. Load this exact packaged file only.
    dll_directory = os.add_dll_directory(str(library.parent)) if os.name == "nt" else None
    try:
        native = ctypes.CDLL(str(library))
        native.vte_typst_abi_version.argtypes = []
        native.vte_typst_abi_version.restype = ctypes.c_uint32
        native.vte_typst_session_create_v1.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
        native.vte_typst_session_create_v1.restype = ctypes.c_int32
        native.vte_typst_session_destroy_v1.argtypes = [ctypes.c_void_p]
        native.vte_typst_session_destroy_v1.restype = ctypes.c_int32
        native.vte_typst_session_execute_v1.argtypes = [
            ctypes.c_void_p, BYTE_POINTER, ctypes.c_size_t, CALLBACK, ctypes.c_void_p,
        ]
        native.vte_typst_session_execute_v1.restype = ctypes.c_int32
        native.vte_typst_scan_image_references_v1.argtypes = [
            BYTE_POINTER, ctypes.c_size_t, CALLBACK, ctypes.c_void_p,
        ]
        native.vte_typst_scan_image_references_v1.restype = ctypes.c_int32
        for name in ("header", "payload"):
            accessor = getattr(native, f"vte_typst_reply_{name}")
            accessor.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
            accessor.restype = BYTE_POINTER
        native.vte_typst_reply_release.argtypes = [ctypes.c_void_p]
        native.vte_typst_reply_release.restype = ctypes.c_int32
        if native.vte_typst_abi_version() != 1:
            raise AbiError("The packaged Typst library does not implement ABI 1")

        session = ctypes.c_void_p()
        owned_replies = []
        callback_errors = []

        @CALLBACK
        def receive(_context, reply):
            # Ownership transfers on entry even when the callback reports failure.
            try:
                if not reply:
                    raise AbiError("Callback received a null reply")
                owned_replies.append(reply)
                return 0
            except BaseException as error:
                if reply:
                    native.vte_typst_reply_release(reply)
                callback_errors.append(error)
                return 1

        def span(reply, kind, limit):
            length = ctypes.c_size_t()
            pointer = getattr(native, f"vte_typst_reply_{kind}")(reply, ctypes.byref(length))
            if length.value > limit or (length.value and not pointer):
                raise AbiError(f"Invalid reply {kind} span: {length.value} bytes")
            return ctypes.string_at(pointer, length.value) if length.value else b""

        def execute(operation, request, **fields):
            data = json.dumps(dict(v=1, epoch=1, generation=1, request=request,
                                   op=operation, **fields), separators=(",", ":")).encode("utf-8")
            if not 0 < len(data) <= MAX_HEADER:
                raise AbiError("Request exceeds the ABI metadata limit")
            buffer = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
            start = len(owned_replies)
            status = native.vte_typst_session_execute_v1(session, buffer, len(data), receive, None)
            if callback_errors:
                raise AbiError(f"Reply callback failed: {callback_errors[0]}")
            require_ok(status, operation)
            events = []
            for reply in owned_replies[start:]:
                header = json.loads(span(reply, "header", MAX_HEADER))
                if not isinstance(header, dict):
                    raise AbiError("Response header is not a JSON object")
                for field, expected in (("v", 1), ("epoch", 1), ("generation", 1), ("request", request)):
                    if type(header.get(field)) is not int or header[field] != expected:
                        raise AbiError(f"Invalid response {field}: {header.get(field)!r}")
                if header.get("op") == "error":
                    raise AbiError(f"Compiler error: {header.get('message')}")
                if header.get("op") in ("parsed", "compiled") and header.get("diagnostics") != []:
                    raise AbiError(f"Compiler diagnostics: {header.get('diagnostics')!r}")
                if header.get("op") != "pdf" and span(reply, "payload", MAX_BINARY):
                    raise AbiError(f"Unexpected binary payload for {header.get('op')}")
                events.append((header, reply))
            return events

        try:
            require_ok(native.vte_typst_session_create_v1(ctypes.byref(session)), "session create")
            if not session:
                raise AbiError("Session creation returned a null handle")
            with tempfile.TemporaryDirectory(prefix="vnote-typst-package-") as directory:
                project = Path(directory).resolve()
                (project / "main.typst").write_text(SOURCE, encoding="utf-8")
                config = {"projectRoot": str(project), "mainFile": "main.typst", "allowDownloads": False}
                ready = execute("initialize", 1, config=config)
                if [header.get("op") for header, _ in ready] != ["ready"]:
                    raise AbiError("Initialization did not produce exactly one ready event")
                events = execute("compile", 2, config=config, activeFile="main.typst", snapshots=[],
                                 overlays=[{"file": "main.typst", "text": SOURCE}])
                if [header.get("op") for header, _ in events] != ["parsed", "compiled"]:
                    raise AbiError("Compilation did not stream parsed then compiled")
                compiled = events[1][0]
                if compiled.get("pages") != [[200, 100]]:
                    raise AbiError(f"Expected one 200x100pt page: {compiled.get('pages')!r}")
                snapshot = compiled.get("snapshot")
                if type(snapshot) is not int or not 1 <= snapshot <= MAX_ID:
                    raise AbiError("Compilation did not produce a valid snapshot")
                start = SOURCE.index("= Package smoke")
                expected_heading = {
                    "title": "Package smoke", "level": 1, "file": "main.typst",
                    "start": start, "end": start + len("= Package smoke"),
                }
                headings = compiled.get("headings")
                if not isinstance(headings, list) or len(headings) != 1 or not isinstance(headings[0], dict):
                    raise AbiError(f"Expected one evaluated heading: {headings!r}")
                if any(headings[0].get(key) != value for key, value in expected_heading.items()):
                    raise AbiError(f"Incorrect evaluated heading/source range: {headings[0]!r}")
                exports = execute("pdf", 3, snapshot=snapshot)
                if len(exports) != 1 or exports[0][0].get("op") != "pdf":
                    raise AbiError("Export did not produce exactly one PDF reply")
                pdf_header, pdf_reply = exports[0]
                if pdf_header.get("snapshot") != snapshot:
                    raise AbiError("PDF export returned a different snapshot")

                # Reply storage must survive the session, with no borrowed Rust state.
                retired, session = session, ctypes.c_void_p()
                require_ok(native.vte_typst_session_destroy_v1(retired), "session destroy")
                pdf = span(pdf_reply, "payload", MAX_BINARY)
                if not pdf.startswith(b"%PDF-") or b"%%EOF" not in pdf[-1024:]:
                    raise AbiError("Snapshot export did not return a complete PDF after session retirement")
        finally:
            failures = []
            if session:
                status = native.vte_typst_session_destroy_v1(session)
                if status:
                    failures.append(f"session destroy: {status}")
            for reply in owned_replies:
                status = native.vte_typst_reply_release(reply)
                if status:
                    failures.append(f"reply release: {status}")
            if failures:
                raise AbiError("; ".join(failures))
        print(f"PASS: {library}: ABI 1, 200x100pt PDF, heading/source range, retired-session payload, released handles")
    finally:
        if dll_directory is not None:
            dll_directory.close()


def smoke(library):
    clean_env = {key: value for key, value in os.environ.items()
                 if not key.upper().startswith(("CARGO", "RUST", "QT", "QML", "TYPST", "VTEXTEDIT", "LD_", "DYLD"))
                 and key not in ("CMAKE_PREFIX_PATH", "LIBRARY_PATH", "PYTHONPATH", "PYTHONHOME")}
    if os.name == "nt":
        system = Path(os.environ["SystemRoot"])
        clean_env["PATH"] = os.pathsep.join(map(str, (library.parent, system / "System32", system)))
    else:
        clean_env["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
        if sys.platform != "darwin":
            clean_env["LD_LIBRARY_PATH"] = str(library.parent)
    command = [sys.executable, str(Path(__file__).resolve()), "--library", str(library), "--worker"]
    if sys.platform == "darwin":
        # A universal Python must preserve the selected parent slice in its worker.
        command = ["/usr/bin/arch", f"-{platform.machine()}"] + command
    subprocess.run(command, cwd=library.parent, env=clean_env, check=True, timeout=TIMEOUT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True, type=Path)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        library = args.library.resolve(strict=True)
        if not library.is_file() or library.stat().st_size == 0:
            raise AbiError(f"Library is not a nonempty file: {library}")
        if args.worker:
            smoke_worker(library)
        else:
            smoke(library)
        return 0
    except (OSError, ValueError, AttributeError, AbiError, subprocess.SubprocessError) as error:
        print(f"Typst package smoke failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
