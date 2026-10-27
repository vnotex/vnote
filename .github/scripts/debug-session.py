#!/usr/bin/env python3
"""Start, wait for, and close a key-restricted SSH-only CI debug session."""

import base64
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import sys
import tarfile
import urllib.request


VERSION = "v0.39.0"
CHECKSUMS_SHA256 = "88747180b176e29db6a73c6bc731177b5642844c4518da939d140cab0be00054"
ARCHIVES = {
    ("Windows", "amd64"): ("upterm_windows_amd64.tar.gz", "6376f19531ec38f2ccfa866953cb761da3499fa2a65b3957ce2469f42d237dd2"),
    ("Darwin", "arm64"): ("upterm_darwin_arm64.tar.gz", "9721c3b586a5de4f55d5e8d4cc8d9fcf17e1bcdc2e861cae9c31848fe204ede7"),
    ("Darwin", "amd64"): ("upterm_darwin_amd64.tar.gz", "eb31198215928ea003b42665c4ec248dba462f3ae921a7a6ff9d5a1faff30f71"),
    ("Linux", "amd64"): ("upterm_linux_amd64.tar.gz", "6149f1821dcd4abad864a2284ad8b32133a05a4032b0614abb66622860a68d17"),
}
RELAY = "ssh://uptermd.upterm.dev:22"
KNOWN_HOSTS = (
    "@cert-authority uptermd.upterm.dev ssh-ed25519 "
    "AAAAC3NzaC1lZDI1NTE5AAAAICiecex8Dq718eSe1CCLgLvDmI7AagvCtax7brPFWkh4\n"
)
XDG_KEYS = ("XDG_RUNTIME_DIR", "XDG_STATE_HOME", "XDG_CONFIG_HOME")


def scrub_environment(source):
    excluded = {"SSH_AUTH_SOCK", "SSH_AGENT_PID", "SSH_ASKPASS", "GIT_ASKPASS",
                "GIT_SSH", "GIT_SSH_COMMAND"}
    return {
        key: value for key, value in source.items()
        if key not in excluded
        and not key.startswith(("INPUT_", "GIT_CONFIG_"))
        and not re.search(r"TOKEN|SECRET|PASSWORD|CERTIFICATE|(?:^|_)KEY(?:_|$)", key.upper())
    }


def public_key(value):
    value = value.strip()
    if "\n" in value or "\r" in value:
        raise ValueError("Provide one public SSH key, not multiple lines")
    parts = value.split()
    if len(parts) < 2 or parts[0] != "ssh-ed25519":
        raise ValueError("Debugging requires an ssh-ed25519 public key")
    base64.b64decode(parts[1], validate=True)
    canonical = f"{parts[0]} {parts[1]}\n"
    checked = subprocess.run(
        ["ssh-keygen", "-lf", "-", "-E", "sha256"], input=canonical,
        text=True, capture_output=True, timeout=10,
    )
    if checked.returncode:
        raise ValueError("Invalid public SSH key")
    return canonical, checked.stdout.strip()


def private_directory(path):
    path.mkdir(mode=0o700)
    if os.name == "nt":
        identity = subprocess.check_output(
            ["whoami", "/user", "/fo", "csv", "/nh"], text=True
        )
        sid = next(csv.reader(io.StringIO(identity)))[1]
        subprocess.run(
            ["icacls", str(path), "/inheritance:r", "/grant:r", f"*{sid}:(OI)(CI)F"],
            check=True, stdout=subprocess.DEVNULL,
        )


def download(name, digest):
    url = f"https://github.com/owenthereal/upterm/releases/download/{VERSION}/{name}"
    request = urllib.request.Request(url, headers={"User-Agent": "vnote-ci-debug"})
    with urllib.request.urlopen(request, timeout=60) as response:
        data = response.read()
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError(f"Upterm checksum mismatch: {name}")
    return data


def install_client(base):
    machine = platform.machine().lower()
    arch = {"amd64": "amd64", "x86_64": "amd64", "arm64": "arm64", "aarch64": "arm64"}.get(machine)
    name, digest = ARCHIVES[(platform.system(), arch)]
    manifest = download("checksums.txt", CHECKSUMS_SHA256).decode("utf-8")
    checksums = {row.split()[1]: row.split()[0] for row in manifest.splitlines() if row.strip()}
    if checksums.get(name) != digest:
        raise ValueError("Pinned Upterm client and manifest disagree")
    data = download(name, digest)
    executable = "upterm.exe" if os.name == "nt" else "upterm"
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        matches = [entry for entry in archive.getmembers() if Path(entry.name).name == executable]
        if len(matches) != 1 or not matches[0].isfile():
            raise ValueError("Upterm archive lacks one regular client executable")
        target = base / executable
        with archive.extractfile(matches[0]) as source, target.open("wb") as output:
            shutil.copyfileobj(source, output)
    target.chmod(0o700)
    return target


def remove_checkout_auth(workspace):
    # Only the checkout and its registered submodules; never the user's global config.
    relative = subprocess.check_output(
        ["git", "-C", str(workspace), "submodule", "foreach", "--quiet", "--recursive",
         'printf "%s\\n" "$displaypath"'], text=True,
    )
    for repository in [workspace, *(workspace / entry for entry in relative.splitlines())]:
        result = subprocess.run(
            ["git", "-C", str(repository), "config", "--local", "--name-only", "--list"],
            text=True, capture_output=True,
        )
        if result.returncode:
            raise RuntimeError("Cannot inspect checkout credential configuration")
        for key in set(result.stdout.splitlines()):
            lowered = key.lower()
            header = lowered.startswith("http.") and lowered.endswith(".extraheader")
            rewrite = lowered.startswith("url.") and lowered.endswith(".insteadof") and "@" in key
            if header or rewrite:
                removed = subprocess.run(
                    ["git", "-C", str(repository), "config", "--local", "--unset-all", key],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                )
                if removed.returncode not in (0, 5):
                    # A key can itself contain credentials. Never echo it in an error.
                    raise RuntimeError("Cannot remove checkout authentication")


def session_environment(state):
    environment = scrub_environment(os.environ)
    environment.update(state["xdg"])
    environment["PATH"] = str(Path(state["client"]).parent) + os.pathsep + environment["PATH"]
    return environment


def stop(base):
    state_file = base / "state.json"
    if not state_file.is_file():
        return
    state = json.loads(state_file.read_text(encoding="utf-8"))
    result = subprocess.run(
        [state["client"], "session", "stop", state["name"]],
        env=session_environment(state), timeout=30, capture_output=True, text=True,
    )
    if result.returncode not in (0, 4):
        raise RuntimeError("Could not confirm that the SSH session stopped")


def start(base, workspace):
    canonical, fingerprint = public_key(os.environ.get("DEBUG_SSH_PUBLIC_KEY", ""))
    private_directory(base)
    client = install_client(base)
    authorized = base / "authorized_keys"
    authorized.write_text(canonical, encoding="utf-8")
    authorized.chmod(0o600)
    known_hosts = base / "known_hosts"
    known_hosts.write_text(KNOWN_HOSTS, encoding="utf-8")
    host_key = base / "host_key"
    subprocess.run(
        ["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", "vnote-runner-debug", "-f", str(host_key)],
        check=True, timeout=15,
    )
    xdg = dict(zip(XDG_KEYS, (str(base / "rt"), str(base / "state"), str(base / "config"))))
    for directory in xdg.values():
        Path(directory).mkdir(mode=0o700)
    name = "vnote"
    if len(str(Path(xdg["XDG_RUNTIME_DIR"]) / "upterm/sessions" / name / "attach.sock").encode()) > 103:
        raise ValueError("Runner temporary path is too long for the SSH session socket")
    state = {"name": name, "client": str(client), "xdg": xdg}
    (base / "state.json").write_text(json.dumps(state), encoding="utf-8")
    remove_checkout_auth(workspace)
    environment = session_environment(state)
    shell = ["pwsh", "-NoLogo", "-NoProfile"] if os.name == "nt" else ["bash", "--noprofile", "--norc"]
    command = [str(client), "host", "--detach", "--accept", "--output", "json", "--name", name,
               "--join-timeout", "15m", "--authorized-keys", str(authorized),
               "--private-key", str(host_key), "--known-hosts", str(known_hosts),
               "--server", RELAY, "--", *shell]
    try:
        launched = subprocess.run(command, cwd=workspace, env=environment, text=True,
                                  capture_output=True, timeout=120)
        if launched.returncode:
            raise RuntimeError(f"SSH session startup failed: {launched.stderr.strip()}")
        session = json.loads(launched.stdout)
        if session.get("status") != "ready" or not session.get("sshCommand"):
            raise RuntimeError("SSH session did not confirm joinable readiness")
        connection = {key: session.get(key) for key in ("name", "status", "sshCommand", "joinCommand")}
        connection["keyFingerprint"] = fingerprint
        connection["restoreEnvironment"] = {key: os.environ.get(key) for key in XDG_KEYS}
        output = base / "connection.json"
        output.write_text(json.dumps(connection, indent=2), encoding="utf-8")
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
            stream.write(f"connection_file={output}\n")
        print("Restricted SSH session ready; connection details are in the debug artifact", flush=True)
    except BaseException:
        stop(base)
        raise


def wait(base):
    state = json.loads((base / "state.json").read_text(encoding="utf-8"))
    process = subprocess.Popen(
        [state["client"], "session", "wait", state["name"]], env=session_environment(state)
    )
    try:
        return process.wait()
    finally:
        try:
            stop(base)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


def main():
    if os.environ.get("GITHUB_ACTIONS") != "true":
        raise RuntimeError("This helper runs only on an explicitly requested GitHub debug job")
    if len(sys.argv) != 2 or sys.argv[1] not in ("start", "wait", "stop"):
        raise ValueError("Usage: debug-session.py start|wait|stop")
    base = Path(os.environ["RUNNER_TEMP"]).resolve() / "vnote-debug"
    workspace = Path(os.environ["GITHUB_WORKSPACE"]).resolve()
    signal.signal(signal.SIGTERM, lambda signum, frame: sys.exit(128 + signum))
    if sys.argv[1] == "start":
        start(base, workspace)
    elif sys.argv[1] == "wait":
        return wait(base)
    else:
        stop(base)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        message = str(error).replace("\r", " ").replace("\n", " ")
        print(f"::error::{message}", file=sys.stderr)
        sys.exit(1)
