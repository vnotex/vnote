#!/usr/bin/env python3
"""Interactive Windows VNote Git-sync diagnostic (Python 3.8+, no packages).

Run: python scripts/diagnose_git_sync.py
Uses native WinHTTP, as VNote does on Windows with either Qt 5.15 or Qt 6.
Only Git discovery GETs are sent: no clone, commit, push, or keychain access.
Optional disk probes touch only newly created temporary directories.
Exit codes: 0 = checked probes passed, 1 = problem found, 2 = incomplete/cancelled.
Passing discovery does NOT prove a PAT is valid or that a push will succeed.
A unique vnote-git-sync-*.log is saved beside this script, or in the temporary
folder if that directory is not writable. Inputs and tokens are not logged.
Interactive runs wait for Enter before closing; redirected runs and --help do not.
Native crashes may leave a fault stack in the report, but cannot be caught/paused.
"""

import sys


_log_file = None
_log_failed = False


def _write(stream, text):
    if stream is None:
        return False
    try:
        try:
            stream.write(text)
        except UnicodeError:
            encoding = getattr(stream, "encoding", None) or "ascii"
            stream.write(text.encode(encoding, "backslashreplace").decode(encoding))
        stream.flush()
        return True
    except Exception:
        return False


def report(*values):
    """Flush trusted diagnostic text only; never transcribe input or exception messages."""
    global _log_failed
    text = " ".join(str(value) for value in values) + "\n"
    if _log_file is not None and not _log_failed and not _write(_log_file, text):
        _log_failed = True
        _write(sys.stderr, "WARNING: Report file write failed; remaining output is console-only.\n")
    if not _write(sys.stdout, text):
        _write(sys.stderr, text)


def report_exception(error):
    # Exception messages, source lines and locals can contain the PAT or Basic header.
    report("Exception type:", type(error).__name__)
    for name in ("winerror", "errno"):
        value = getattr(error, name, None)
        if isinstance(value, int):
            report("  %s=%d" % (name, value))
    frame = error.__traceback__
    while frame is not None:
        code = frame.tb_frame.f_code
        filename = code.co_filename.replace("\\", "/").rsplit("/", 1)[-1]
        report("  at %s:%d in %s" % (filename, frame.tb_lineno, code.co_name))
        frame = frame.tb_next


def load_runtime():
    # Keep imports (including native _ctypes loading) inside the logged exception boundary.
    global base64, ct, wt, datetime, getpass, os, Path, stat, tempfile
    global quote, unquote, urlsplit, warnings
    import base64
    import ctypes as ct
    from ctypes import wintypes as wt
    from datetime import datetime
    import getpass
    import os
    from pathlib import Path
    import stat
    import tempfile
    from urllib.parse import quote, unquote, urlsplit
    import warnings


# WinHTTP SDK constants: keep credential/redirect policy names explicit.
WINHTTP_OPTION_AUTOLOGON_POLICY = 77
WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH = 2
WINHTTP_OPTION_REDIRECT_POLICY = 88
WINHTTP_OPTION_REDIRECT_POLICY_NEVER = 0

CERT_FLAGS = {
    0x00000001: "Revocation check failed: check access to the CA's CRL/OCSP servers.",
    0x00000002: "Certificate is invalid: inspect the server/proxy certificate chain.",
    0x00000004: "Certificate was revoked: stop and contact the server administrator.",
    0x00000008: "Unknown certificate authority: check Windows roots and HTTPS inspection.",
    0x00000010: "Certificate hostname mismatch: check the URL and HTTPS interception.",
    0x00000020: "Certificate date invalid: check the clock and certificate validity dates.",
    0x80000000: "TLS/security-channel failure: check Windows updates and TLS policy.",
}
NETWORK_ERRORS = {
    12002: "Diagnostic timeout (15 seconds per stage); check network/proxy reachability.",
    12007: "DNS name could not be resolved; check hostname, DNS and proxy settings.",
    12029: "Connection failed; check firewall, proxy and server availability.",
    12030: "Connection was interrupted; check proxy/firewall and TLS compatibility.",
    12037: CERT_FLAGS[0x20],
    12038: CERT_FLAGS[0x10],
    12044: "Server requests a client certificate; a PAT alone cannot satisfy this.",
    12045: CERT_FLAGS[0x08],
    12057: CERT_FLAGS[0x01],
    12157: CERT_FLAGS[0x80000000],
    12169: CERT_FLAGS[0x02],
    12170: CERT_FLAGS[0x04],
    12175: "Windows rejected TLS/certificate validation (VNote's rc=-17 path).",
    12179: "Certificate has the wrong usage; contact the server/proxy administrator.",
}


class TransportError(Exception):
    def __init__(self, stage, code, flags):
        self.stage, self.code, self.flags = stage, code, flags


class WinHTTP:
    """Typed synchronous WinHTTP calls; retain the callback through handle closure."""

    def __init__(self):
        self.dll = ct.WinDLL("winhttp", use_last_error=True)
        self.callback_type = ct.WINFUNCTYPE(None, wt.HANDLE, ct.c_size_t,
                                           wt.DWORD, ct.c_void_p, wt.DWORD)
        handle, ptr, dword = wt.HANDLE, ct.c_void_p, wt.DWORD
        signatures = {
            "Open": (handle, [wt.LPCWSTR, dword, wt.LPCWSTR, wt.LPCWSTR, dword]),
            "Connect": (handle, [handle, wt.LPCWSTR, wt.WORD, dword]),
            "OpenRequest": (handle, [handle, wt.LPCWSTR, wt.LPCWSTR, wt.LPCWSTR,
                                     wt.LPCWSTR, ct.POINTER(wt.LPCWSTR), dword]),
            "SetOption": (wt.BOOL, [handle, dword, ptr, dword]),
            "SetTimeouts": (wt.BOOL, [handle, ct.c_int, ct.c_int, ct.c_int, ct.c_int]),
            "SetStatusCallback": (ptr, [handle, self.callback_type, dword, ct.c_size_t]),
            "SendRequest": (wt.BOOL, [handle, wt.LPCWSTR, dword, ptr, dword,
                                     dword, ct.c_size_t]),
            "ReceiveResponse": (wt.BOOL, [handle, ptr]),
            "QueryHeaders": (wt.BOOL, [handle, dword, wt.LPCWSTR, ptr,
                                      ct.POINTER(dword), ct.POINTER(dword)]),
            "ReadData": (wt.BOOL, [handle, ptr, dword, ct.POINTER(dword)]),
            "CloseHandle": (wt.BOOL, [handle]),
        }
        for name, (result, args) in signatures.items():
            fn = getattr(self.dll, "WinHttp" + name)
            fn.restype, fn.argtypes = result, args
            setattr(self, name, fn)

    def probe(self, url, service, username="", pat=""):
        flags, handles = wt.DWORD(), []

        @self.callback_type
        def callback(handle, context, status, info, length):
            if status == 0x10000 and info and length >= ct.sizeof(wt.DWORD):
                flags.value |= ct.cast(info, ct.POINTER(wt.DWORD)).contents.value

        def check(value, stage):
            if not value:
                raise TransportError(stage, ct.get_last_error(), flags.value)
            return value

        def option(handle, key, value):
            data = wt.DWORD(value)
            return self.SetOption(handle, key, ct.byref(data), ct.sizeof(data))

        def header(request, key):
            # Fixed bounded buffer; never print server-controlled headers or bodies.
            text = ct.create_unicode_buffer(256)
            size = wt.DWORD(ct.sizeof(text))
            if not self.QueryHeaders(request, key, None, text, ct.byref(size), None):
                return ""
            return text.value

        try:
            session = check(self.Open("VNote-sync-diagnostic/1", 0, None, None, 0), "session")
            handles.append(session)  # WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, as in libgit2.
            # Match libgit2's best-effort TLS 1.0/1.1/1.2/1.3 mask and fallback.
            if not option(session, 84, 0x80 | 0x200 | 0x800 | 0x2000):
                option(session, 84, 0x80 | 0x200 | 0x800)
            check(self.SetTimeouts(session, 15000, 15000, 15000, 15000), "timeouts")
            connection = check(self.Connect(session, url.hostname.encode("idna").decode(),
                                            url.port or 443, 0), "connect")
            handles.append(connection)
            path = quote(url.path.rstrip("/"), safe="/%:@!$&'()*+,;=-._~")
            path += "/info/refs?service=" + service
            request = check(self.OpenRequest(connection, "GET", path, None, None,
                                             None, 0x800000), "request")
            handles.append(request)
            check(option(request, WINHTTP_OPTION_AUTOLOGON_POLICY,
                         WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH), "disable automatic Windows login")
            check(option(request, WINHTTP_OPTION_REDIRECT_POLICY,
                         WINHTTP_OPTION_REDIRECT_POLICY_NEVER), "disable redirects")
            previous = self.SetStatusCallback(request, callback, 0x10000, 0)
            if previous == ct.c_void_p(-1).value:
                raise TransportError("certificate callback", ct.get_last_error(), flags.value)
            headers = ""
            if pat:
                basic = base64.b64encode((username + ":" + pat).encode("utf-8")).decode("ascii")
                headers = "Authorization: Basic " + basic + "\r\n"
            check(self.SendRequest(request, headers, len(headers), None, 0, 0, 0), "send")
            check(self.ReceiveResponse(request, None), "receive")
            status, size = wt.DWORD(), wt.DWORD(ct.sizeof(wt.DWORD))
            check(self.QueryHeaders(request, 19 | 0x20000000, None, ct.byref(status),
                                    ct.byref(size), None), "HTTP status")
            content_type = header(request, 1).split(";", 1)[0].strip().lower()
            # Check just the service announcement, not the full refs or repository content.
            announcement = ("# service=" + service + "\n").encode("ascii")
            expected = ("%04x" % (len(announcement) + 4)).encode("ascii") + announcement + b"0000"
            body = bytearray()
            if status.value == 200:
                while len(body) < len(expected):
                    buffer = ct.create_string_buffer(len(expected) - len(body))
                    count = wt.DWORD()
                    check(self.ReadData(request, buffer, len(buffer), ct.byref(count)), "read")
                    if not count.value:
                        break
                    body.extend(buffer.raw[:count.value])
            return status.value, (content_type == "application/x-" + service + "-advertisement"
                                  and body == expected)
        finally:
            for handle in reversed(handles):
                self.CloseHandle(handle)
            # callback remains strongly referenced until all handles have closed.


def parse_url(text):
    if any(ord(c) < 33 or ord(c) == 127 for c in text) or "\\" in text:
        raise ValueError("Use an HTTPS clone URL without whitespace or backslashes.")
    try:
        url = urlsplit(text)
        if (url.scheme != "https" or not url.hostname or not url.path.strip("/")
                or url.password is not None or "?" in text or "#" in text
                or (url.port is not None and not 1 <= url.port <= 65535)):
            raise ValueError
        username = unquote(url.username or "x-access-token")
        if ":" in username or any(ord(c) < 32 or ord(c) == 127 for c in unquote(text)):
            raise ValueError
        url.hostname.encode("idna")
    except (ValueError, UnicodeError):
        raise ValueError("Use an HTTPS repository URL, optionally with username@host; "
                         "no password/token, query, fragment, or invalid port.") from None
    return url, username


def report_network(client, url, service, username="", pat=""):
    label = "fetch" if service == "git-upload-pack" else "push discovery (GET only)"
    report("STEP:", label, "with PAT" if pat else "without PAT")
    try:
        status, git = client.probe(url, service, username, pat)
    except TransportError as error:
        report("FAIL: %s: WinHTTP %s at %s; secure flags=0x%08x" %
              (label, error.code, error.stage, error.flags))
        report("  " + NETWORK_ERRORS.get(error.code, "WinHTTP setup/transport failed; report this code."))
        for bit, message in CERT_FLAGS.items():
            if error.flags & bit:
                report("  " + message)
        report("  Check Windows clock/root certificates and proxy/antivirus HTTPS inspection.")
        report("  Do not disable certificate verification. Qt OpenSSL DLLs do not fix WinHTTP.")
        return "FAIL", False
    if 300 <= status < 400:
        report("INCOMPLETE: %s: HTTP %s redirect; not followed to protect credentials." % (label, status))
        report("  Use the final HTTPS clone URL. VNote may follow an initial redirect.")
        return "INCOMPLETE", False
    if status == 200 and git:
        report("PASS: %s: TLS and Git service advertisement received." % label)
        return "PASS", True
    if status == 401 and not pat:
        report("INCOMPLETE: %s: TLS works; HTTP 401 requests authentication." % label)
        return "INCOMPLETE", True
    messages = {
        200: "Not a Git advertisement; check clone URL, login portal or proxy interception.",
        401: "Login/token rejected or unsupported. Check account login AND PAT, not just PAT.",
        403: "Access forbidden: check token scope, repository rights, account or network policy.",
        404: "Wrong repository URL OR private repository hidden from these credentials.",
        407: "Proxy authentication required; this is not a Gitee PAT error.",
        429: "Server rate limit; retry later.",
    }
    message = messages.get(status, "Server/proxy failure." if status >= 500 else "Unexpected HTTP response.")
    outcome = "INCOMPLETE" if not pat and status in (403, 404) else "FAIL"
    report("%s: %s: HTTP %s. %s" % (outcome, label, status, message))
    return outcome, status in (200, 401, 403, 404)


def check_notebook(folder, write_probe):
    report("STEP: Checking notebook filesystem")
    root = Path(folder)
    outcomes = []
    try:
        if not root.is_dir():
            report("FAIL: Notebook folder does not exist or is not a directory.")
            return ["FAIL"]
        config = root / "vx_notebook" / "config.json"
        if not config.is_file():
            report("FAIL: vx_notebook/config.json is missing; this is not a bundled notebook root.")
            outcomes.append("FAIL")
        for name in (".git", "vx_notebook/vx_sync"):
            path = root / name
            try:
                info = path.lstat()
            except FileNotFoundError:
                continue
            attrs = getattr(info, "st_file_attributes", 0)
            if name == ".git":
                report("INFO: Existing .git: directory=%s, readonly=%s, hidden=%s, reparse=%s." %
                      (stat.S_ISDIR(info.st_mode), bool(attrs & 1), bool(attrs & 2), bool(attrs & 1024)))
                report("  This is the gitlink path implicated by 'template .git: Access denied'.")
                report("  Existing directories/attributes/locks may block reinitialization; back up first.")
                outcomes.append("INCOMPLETE")
            elif not path.is_dir() or not (path / "HEAD").is_file():
                report("FAIL: vx_notebook/vx_sync exists without a usable HEAD; partial Git setup.")
                outcomes.append("FAIL")
        if not write_probe:
            report("INCOMPLETE: Disk write checks skipped.")
            return outcomes + ["INCOMPLETE"]
        kernel = ct.WinDLL("kernel32", use_last_error=True)
        kernel.SetFileAttributesW.argtypes = [wt.LPCWSTR, wt.DWORD]
        kernel.SetFileAttributesW.restype = wt.BOOL
        for parent in (root, root / "vx_notebook", root / "vx_notebook" / "vx_sync"):
            if not parent.is_dir():
                continue
            with tempfile.TemporaryDirectory(prefix=".vnote-sync-check-", dir=str(parent)) as temp:
                probe = Path(temp) / ".git"
                probe.write_bytes(b"gitdir: diagnostic-only\n")
                if not kernel.SetFileAttributesW(str(probe), 2):
                    raise ct.WinError(ct.get_last_error())
                probe.rename(Path(temp) / "renamed")
            report("PASS: Temporary create/write/hide/rename/delete in %s." %
                  ("notebook root" if parent == root else parent.relative_to(root)))
        report("INFO: Temporary probes do not prove existing .git files are writable or unlocked.")
    except OSError as error:
        report("FAIL: Local file operation: Windows error=%s, errno=%s." %
              (getattr(error, "winerror", None), error.errno))
        report("  Check permissions, readonly/hidden attributes, locks and antivirus protection.")
        report("  If cleanup was blocked, inspect only .vnote-sync-check-* temporary directories.")
        outcomes.append("FAIL")
    return outcomes or ["PASS"]


def main():
    if sys.platform != "win32":
        report("This diagnostic must run on the affected Windows machine (native WinHTTP).")
        return 2
    report("VNote Git sync diagnostic: Windows WinHTTP, independent of Qt 5.15/Qt 6.")
    report("Windows:", sys.getwindowsversion(), "Python bits:", ct.sizeof(ct.c_void_p) * 8)
    report("Local time:", datetime.now().astimezone().isoformat(), "-- verify this clock")
    report("No clone/push/keychain access. PAT is not saved. Do not put a PAT in the URL.")
    report("Default WinHTTP proxy; diagnostic-only 15s stage timeouts; no redirects.")
    while True:
        try:
            url, username = parse_url(input("Exact HTTPS clone URL used in VNote: ").strip())
            break
        except ValueError as error:
            report(error)
    report("Target host:", url.hostname)
    if not url.username:
        report("INFO: VNote uses login x-access-token for this URL. Gitee may require the PAT")
        report("  owner's account login: https://LOGIN@gitee.com/OWNER/REPO.git (no PAT in URL).")
    folder = input("Existing notebook folder (blank to skip disk checks): ").strip().strip('"')
    outcomes = []
    if folder:
        consent = input("Create/remove unique temporary disk probes there? [y/N]: ").lower() == "y"
        outcomes.extend(check_notebook(folder, consent))
    else:
        outcomes.append("INCOMPLETE")
    report("STEP: Initializing WinHTTP")
    client = WinHTTP()
    result, reachable = report_network(client, url, "git-upload-pack")
    if reachable and input("Test with your PAT on this host? [y/N]: ").lower() == "y":
        # Windows getpass can block on console input even when stdin is redirected.
        if not sys.stdin.isatty():
            raise getpass.GetPassWarning("A real console is required")
        report("STEP: Reading PAT securely (input is not logged)")
        with warnings.catch_warnings():
            warnings.simplefilter("error", getpass.GetPassWarning)
            pat = getpass.getpass("PAT (hidden; Enter to skip): ")
        if pat:
            result, reachable = report_network(client, url, "git-upload-pack", username, pat)
            outcomes.append(result)
            if reachable:
                result, _ = report_network(client, url, "git-receive-pack", username, pat)
                outcomes.append(result)
            pat = ""
        else:
            outcomes.extend([result, "INCOMPLETE"])
    else:
        outcomes.extend([result, "INCOMPLETE"])
    report("\nLimits: discovery is not a clone/push; even HTTP 200 does not prove PAT validity")
    report("or branch write permission. Existing-file locks and VNote's keychain are not tested.")
    report("For 'Password entry not found', re-enter the PAT in VNote after fixing TLS/disk errors.")
    report("Share this output, not the PAT. No response headers/body or full URL are printed.")
    summary = "FAIL" if "FAIL" in outcomes else "INCOMPLETE" if "INCOMPLETE" in outcomes else "PASS"
    report("RESULT:", summary)
    return {"PASS": 0, "FAIL": 1, "INCOMPLETE": 2}[summary]


def run():
    global _log_file, _log_failed
    exit_code = 2
    log_path = None
    fault_handler = None
    interactive = False
    try:
        if len(sys.argv) > 1:
            report(__doc__)
            return 0 if sys.argv[1:] in (["--help"], ["-h"]) else 2

        try:
            interactive = all(stream is not None and stream.isatty()
                              for stream in (sys.stdin, sys.stdout))
        except Exception:
            pass

        # Initialize the report before importing ctypes or loading Windows DLLs.
        import os
        import tempfile
        for directory in (os.path.dirname(os.path.abspath(__file__)), None):
            try:
                descriptor, candidate = tempfile.mkstemp(
                    prefix="vnote-git-sync-", suffix=".log", dir=directory)
                try:
                    _log_file = os.fdopen(descriptor, "w", encoding="utf-8")
                except BaseException:
                    os.close(descriptor)
                    raise
                log_path = candidate
                break
            except OSError as error:
                report("WARNING: Could not create report beside script." if directory else
                       "WARNING: Could not create temporary report; continuing console-only.")
                report_exception(error)
        if log_path is not None:
            report("Diagnostic report:", log_path)
            report("Native crashes may write a fault stack here but cannot wait for Enter.")
            try:
                import faulthandler
                faulthandler.enable(file=_log_file, all_threads=True)
                fault_handler = faulthandler
            except Exception as error:
                report("WARNING: Native fault logging is unavailable.")
                report_exception(error)

        report("Python:", sys.version.replace("\n", " "))
        report("Platform:", sys.platform)
        report("STEP: Loading diagnostic runtime")
        load_runtime()
        exit_code = main()
    except (KeyboardInterrupt, EOFError) as error:
        report("INCOMPLETE: Cancelled or input console closed.")
        report_exception(error)
    except BaseException as error:
        report("INCOMPLETE: Diagnostic stopped unexpectedly; share the report below.")
        if type(error).__name__ == "GetPassWarning":
            report("No secure input terminal; no token will be echoed. Run in a console.")
        report_exception(error)
    finally:
        # --help/invalid arguments return before creating a report or prompting.
        if len(sys.argv) == 1:
            report("Diagnostic exit code:", exit_code)
            if log_path is not None:
                report("Diagnostic report (may be partial):" if _log_failed else
                       "Diagnostic report:", log_path)
            else:
                report("No report file available; copy the console output.")
            if interactive:
                try:
                    input("Press Enter to close this diagnostic...")
                except (KeyboardInterrupt, EOFError):
                    pass
                except BaseException as error:
                    report("INCOMPLETE: Could not wait for console input.")
                    report_exception(error)
                    exit_code = 2
                    report("Diagnostic exit code:", exit_code)
        if fault_handler is not None:
            try:
                fault_handler.disable()
            except Exception as error:
                # Leave its descriptor alive if disabling failed.
                report("WARNING: Could not disable native fault logging.")
                report_exception(error)
            else:
                fault_handler = None
        if _log_file is not None and fault_handler is None:
            try:
                _log_file.close()
            except Exception:
                _write(sys.stderr, "WARNING: Could not close the diagnostic report.\n")
            _log_file = None
    return exit_code


if __name__ == "__main__":
    sys.exit(run())
