#!/usr/bin/env python3
"""Interactive Windows VNote Git-sync diagnostic (Python 3.8+, no packages).

Run: python scripts/diagnose_git_sync.py
Uses native WinHTTP, as VNote does on Windows with either Qt 5.15 or Qt 6.
Health checks cover runtime, proxy/TLS, HTTPS clock reference, credentials and disk.
Only Git discovery GETs are sent: no clone, commit, push, or keychain access.
No Windows settings are changed. Raw response headers and proxy values are not logged.
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
    global base64, ct, wt, datetime, timezone, getpass, os, Path, stat, tempfile
    global quote, unquote, urlsplit, warnings, parsedate_to_datetime, monotonic, shutil
    import base64
    import ctypes as ct
    from ctypes import wintypes as wt
    from datetime import datetime, timezone
    from email.utils import parsedate_to_datetime
    from time import monotonic
    import shutil
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
        self.clock_sample = None
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

    def check_proxy(self):
        report("STEP: Checking WinHTTP proxy configuration")

        class ProxyInfo(ct.Structure):
            _fields_ = [("access", wt.DWORD), ("proxy", ct.c_void_p),
                        ("bypass", ct.c_void_p)]

        class BrowserProxyInfo(ct.Structure):
            _fields_ = [("auto_detect", wt.BOOL), ("pac", ct.c_void_p),
                        ("proxy", ct.c_void_p), ("bypass", ct.c_void_p)]

        kernel = ct.WinDLL("kernel32", use_last_error=True)
        kernel.GlobalFree.argtypes = [ct.c_void_p]
        kernel.GlobalFree.restype = ct.c_void_p
        outcomes = []
        for api, structure, fields in (
                ("WinHttpGetDefaultProxyConfiguration", ProxyInfo, ("proxy", "bypass")),
                ("WinHttpGetIEProxyConfigForCurrentUser", BrowserProxyInfo,
                 ("pac", "proxy", "bypass"))):
            info = structure()
            fn = getattr(self.dll, api)
            fn.argtypes, fn.restype = [ct.POINTER(structure)], wt.BOOL
            if not fn(ct.byref(info)):
                report("INCOMPLETE: %s unavailable; Windows error=%d." % (api, ct.get_last_error()))
                outcomes.append("INCOMPLETE")
                continue
            try:
                if structure is ProxyInfo:
                    if info.access in (1, 3):
                        report("PASS: Default WinHTTP proxy configuration readable; mode=%s." %
                               ("direct" if info.access == 1 else "named proxy"))
                    else:
                        report("INCOMPLETE: Unrecognized default WinHTTP proxy mode=%d." % info.access)
                        outcomes.append("INCOMPLETE")
                else:
                    report("INFO: Browser proxy settings: manual=%s, PAC=%s, auto-detect=%s." %
                           (bool(info.proxy), bool(info.pac), bool(info.auto_detect)))
                    report("  Browser/per-user proxy settings are not applied by this diagnostic.")
            finally:
                # Opaque pointers: never decode proxy URLs (they can contain credentials).
                for field in fields:
                    pointer = getattr(info, field)
                    if pointer:
                        kernel.GlobalFree(pointer)
        for name in ("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY"):
            report("INFO: %s environment variable: %s (not applied by this diagnostic)." %
                   (name, "set" if os.environ.get(name) else "unset"))
        report("INFO: Connectivity is tested through WinHTTP, not a direct DNS/TCP socket bypass.")
        return summarize(outcomes)

    def probe(self, url, service, username="", pat="", check_clock=False):
        flags, handles = wt.DWORD(), []
        self.clock_sample = None

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
            if option(session, 84, 0x80 | 0x200 | 0x800 | 0x2000):
                report("INFO: WinHTTP TLS protocol mask accepted (includes TLS 1.3).")
            elif option(session, 84, 0x80 | 0x200 | 0x800):
                report("INFO: WinHTTP TLS protocol mask accepted without TLS 1.3.")
            else:
                report("INFO: TLS protocol mask unavailable; using Windows defaults.")
            report("INFO: TLS negotiation still depends on Windows policy; certificate checks stay on.")
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
            headers = "Cache-Control: no-cache\r\nPragma: no-cache\r\n"
            if pat:
                basic = base64.b64encode((username + ":" + pat).encode("utf-8")).decode("ascii")
                headers += "Authorization: Basic " + basic + "\r\n"
            started = datetime.now(timezone.utc)
            tick = monotonic()
            check(self.SendRequest(request, headers, len(headers), None, 0, 0, 0), "send")
            check(self.ReceiveResponse(request, None), "receive")
            finished, elapsed = datetime.now(timezone.utc), monotonic() - tick
            if check_clock:
                # Only the anonymous HTTPS response is used; raw headers never leave this check.
                self.clock_sample = (header(request, 9), header(request, 48),
                                     started, finished, elapsed)
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


def report_network(client, url, service, username="", pat="", check_clock=False):
    label = "fetch" if service == "git-upload-pack" else "push discovery (GET only)"
    report("STEP:", label, "with PAT" if pat else "without PAT")
    try:
        status, git = client.probe(url, service, username, pat, check_clock=check_clock)
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
    report("PASS: HTTPS response received; Windows accepted TLS and the endpoint certificate.")
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


def summarize(outcomes):
    return "FAIL" if "FAIL" in outcomes else "INCOMPLETE" if "INCOMPLETE" in outcomes else "PASS"


def report_clock(sample):
    report("STEP: Checking UTC clock against the anonymous HTTPS response")
    if sample is None:
        report("INCOMPLETE: No HTTPS clock reference; verify Windows date/time and time zone manually.")
        return "INCOMPLETE"
    date, age, started, finished, elapsed = sample
    try:
        reference = parsedate_to_datetime(date)
        if reference.tzinfo is None:
            raise ValueError
        reference = reference.astimezone(timezone.utc)
    except (TypeError, ValueError, OverflowError):
        report("INCOMPLETE: HTTPS clock reference missing or malformed; no raw header is logged.")
        return "INCOMPLETE"
    if age and (not age.isascii() or not age.isdigit() or int(age) != 0):
        report("INCOMPLETE: HTTPS clock reference may be cached or has an invalid cache age.")
        report("  Verify Windows date/time manually; a cached response is not a fresh time reference.")
        return "INCOMPLETE"
    wall_elapsed = (finished - started).total_seconds()
    if elapsed < 0 or abs(wall_elapsed - elapsed) > 2:
        report("INCOMPLETE: Local clock changed during the request; retry after time synchronization.")
        return "INCOMPLETE"
    offset = (reference - started).total_seconds() - elapsed / 2
    if abs(offset) > 300 + elapsed / 2:
        report("INCOMPLETE: Local clock is about %d seconds %s the HTTPS reference." %
               (round(abs(offset)), "behind" if offset > 0 else "ahead of"))
        report("  Check Windows date/time, time zone and Sync now; server/proxy time may also be wrong.")
        return "INCOMPLETE"
    report("PASS: UTC clock agrees with HTTPS reference within 5 minutes plus request uncertainty.")
    report("INFO: This is a server/proxy time sanity check, not authoritative NTP synchronization.")
    return "PASS"


def check_username(url, username):
    report("STEP: Checking Git login selection")
    report("INFO: Git login is %s; value is not logged." %
           ("provided in the URL" if url.username else "defaulted to x-access-token"))
    if url.hostname.lower() == "gitee.com" and username == "x-access-token":
        report("INCOMPLETE: Gitee needs the PAT owner's account login, not the default GitHub login.")
        report("  Use https://LOGIN@gitee.com/OWNER/REPO.git; LOGIN need not be OWNER. No PAT in URL.")
        return "INCOMPLETE"
    report("PASS: Git login selection checked; account identity and permissions remain unverified.")
    return "PASS"


def check_pat(pat):
    if not pat:
        report("INCOMPLETE: PAT checks skipped; no token entered.")
        return "INCOMPLETE"
    if any(c.isspace() or ord(c) < 32 or ord(c) == 127 for c in pat):
        report("FAIL: PAT input contains whitespace/control characters; token was not sent.")
        report("  Copy the token exactly and re-enter it; no input is logged or automatically trimmed.")
        return "FAIL"
    report("PASS: PAT input format checked; this does not establish token validity.")
    return "PASS"


def check_disk_space(root):
    try:
        free = shutil.disk_usage(root).free
    except OSError as error:
        report("INCOMPLETE: Free-space query unavailable; Windows error=%s, errno=%s." %
               (getattr(error, "winerror", None), error.errno))
        return "INCOMPLETE"
    report("INFO: Notebook volume available space: %d MiB." % (free // (1024 * 1024)))
    if free == 0:
        report("FAIL: No available disk space for Git objects, index or temporary files.")
        return "FAIL"
    if free < 100 * 1024 * 1024:
        report("INCOMPLETE: Less than 100 MiB available; required space depends on notebook/history size.")
        return "INCOMPLETE"
    report("PASS: Available disk space exceeds the 100 MiB warning threshold; capacity is not guaranteed.")
    return "PASS"


def check_git_metadata(gitdir):
    outcomes = []
    for name in ("HEAD", "config", "index"):
        try:
            path = gitdir / name
            if path.exists():
                info = path.lstat()
                readonly = bool(getattr(info, "st_file_attributes", 0) & 1)
                with path.open("rb") as stream:
                    stream.read(1)  # Check read access only, never print or modify contents.
                report("PASS: Git %s readable; readonly=%s." % (name, readonly))
                if readonly:
                    report("INCOMPLETE: Readonly Git metadata may prevent updates; no attributes changed.")
                    outcomes.append("INCOMPLETE")
            try:
                (gitdir / (name + ".lock")).lstat()
            except FileNotFoundError:
                continue
            report("INCOMPLETE: Git %s.lock exists; an active VNote/Git operation may own it." % name)
            report("  Retry after normal operations finish; this check does not delete lock files.")
            outcomes.append("INCOMPLETE")
        except OSError as error:
            report("FAIL: Git %s metadata access: Windows error=%s, errno=%s." %
                   (name, getattr(error, "winerror", None), error.errno))
            outcomes.append("FAIL")
    return outcomes


def check_notebook(folder, write_probe):
    report("STEP: Checking notebook filesystem")
    root = Path(folder)
    outcomes = []
    try:
        if not root.is_dir():
            report("FAIL: Notebook folder does not exist or is not a directory.")
            return ["FAIL"]
        outcomes.append(check_disk_space(root))
        config = root / "vx_notebook" / "config.json"
        if not config.is_file():
            report("FAIL: vx_notebook/config.json is missing; this is not a bundled notebook root.")
            outcomes.append("FAIL")
        else:
            with config.open("rb") as stream:
                stream.read(1)
            report("PASS: Notebook config is readable (contents are not logged or validated).")
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
            if path.is_dir():
                outcomes.extend(check_git_metadata(path))
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
    report("STEP: Checking diagnostic runtime")
    if sys.platform != "win32" or sys.version_info < (3, 8):
        report("FAIL: This diagnostic requires native Windows and Python 3.8 or newer.")
        return 1
    report("PASS: Windows/Python runtime supported by this diagnostic.")
    report("VNote Git sync diagnostic: Windows WinHTTP, independent of Qt 5.15/Qt 6.")
    report("Windows:", sys.getwindowsversion(), "Python bits:", ct.sizeof(ct.c_void_p) * 8)
    report("Local time:", datetime.now().astimezone().isoformat())
    report("UTC time:", datetime.now(timezone.utc).isoformat())
    report("INFO: Git CLI, Python packages and Qt OpenSSL DLLs are not required by these probes.")
    report("No clone/push/keychain access. PAT is not saved. Do not put a PAT in the URL.")
    report("Default WinHTTP proxy; diagnostic-only 15s stage timeouts; no redirects.")
    report("INFO: Windows settings are inspected only, never changed.")
    while True:
        try:
            url, username = parse_url(input("Exact HTTPS clone URL used in VNote: ").strip())
            break
        except ValueError as error:
            report(error)
    report("Target host:", url.hostname)
    checks = {"Runtime": "PASS", "Git login selection": check_username(url, username)}
    folder = input("Existing notebook folder (blank to skip disk checks): ").strip().strip('"')
    if folder:
        consent = input("Create/remove unique temporary disk probes there? [y/N]: ").lower() == "y"
        checks["Notebook filesystem"] = summarize(check_notebook(folder, consent))
    else:
        report("INCOMPLETE: Notebook filesystem checks skipped; no notebook folder entered.")
        checks["Notebook filesystem"] = "INCOMPLETE"
    report("STEP: Initializing WinHTTP")
    client = None
    try:
        client = WinHTTP()
        report("PASS: Native WinHTTP library and required request APIs loaded.")
        checks["WinHTTP runtime"] = "PASS"
    except (OSError, AttributeError) as error:
        report("FAIL: Native WinHTTP runtime unavailable; check Windows installation/updates.")
        report_exception(error)
        checks["WinHTTP runtime"] = "FAIL"
    reachable = False
    checks["Fetch discovery"] = "INCOMPLETE"
    if client is not None:
        try:
            checks["Proxy inspection"] = client.check_proxy()
        except (OSError, AttributeError) as error:
            report("INCOMPLETE: Proxy configuration inspection unavailable.")
            report_exception(error)
            checks["Proxy inspection"] = "INCOMPLETE"
        checks["Fetch discovery"], reachable = report_network(
            client, url, "git-upload-pack", check_clock=True)
        checks["UTC clock reference"] = report_clock(client.clock_sample)
    else:
        report("INCOMPLETE: Proxy, HTTPS discovery and clock reference skipped; WinHTTP unavailable.")
        checks.update({"Proxy inspection": "INCOMPLETE", "UTC clock reference": "INCOMPLETE"})
    checks["PAT input"] = "INCOMPLETE"
    checks["Push discovery"] = "INCOMPLETE"
    if reachable and input("Test with your PAT on this host? [y/N]: ").lower() == "y":
        # Windows getpass can block on console input even when stdin is redirected.
        if not sys.stdin.isatty():
            report("INCOMPLETE: No secure input terminal; PAT checks skipped. Run in a console.")
        else:
            report("STEP: Reading PAT securely (input is not logged)")
            with warnings.catch_warnings():
                warnings.simplefilter("error", getpass.GetPassWarning)
                pat = getpass.getpass("PAT (hidden; Enter to skip): ")
            checks["PAT input"] = check_pat(pat)
            if checks["PAT input"] == "PASS":
                result, reachable = report_network(client, url, "git-upload-pack", username, pat)
                # Expected anonymous 401/403/404 must not taint successful authenticated discovery.
                if checks["Fetch discovery"] != "FAIL":
                    checks["Fetch discovery"] = result
                if reachable:
                    checks["Push discovery"], _ = report_network(
                        client, url, "git-receive-pack", username, pat)
            pat = ""
    else:
        report("INCOMPLETE: PAT checks skipped; consent declined or endpoint unavailable.")
    if checks["Push discovery"] == "INCOMPLETE":
        report("INCOMPLETE: Push discovery was skipped or could not complete; no upload was attempted.")
    report("\nHealth check summary:")
    for label, result in checks.items():
        report("  %s: %s" % (label, result))
    report("\nLimits: discovery is not a clone/push; even HTTP 200 does not prove PAT validity")
    report("or branch write permission. Existing-file write locks and VNote's keychain are not tested.")
    report("NOT TESTED: VNote keychain build support/entry, runtime sync registration or proxy overrides.")
    report("INFO: Windows roots/TLS are checked only through this endpoint, not a system-wide audit.")
    report("For 'Password entry not found', re-enter the PAT in VNote after fixing TLS/disk errors.")
    report("Share this output, not the PAT. No raw response headers/body, proxy values or full URL are printed.")
    summary = summarize(checks.values())
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
