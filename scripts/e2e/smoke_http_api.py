#!/usr/bin/env python3
"""Exercise a running, disposable VNote HTTP API; never use a production profile.

The caller creates .http-api-smoke.json in its disposable fixture root with:
  {"notebookId": "...", "disposable": true}
Tokens are issued through that VNote's Settings page and saved inside the fixture.
This runner does not install trust roots, open firewall ports, or launch VNote.
"""

import argparse
import json
import pathlib
import ssl
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid


REQUIRED_TESTS = {
    "test_httpserver",
    "test_apiauth",
    "test_restapi",
    "test_vnoteapibackend",
    "test_httpapilifecycle",
}


def check_registration(build):
    result = subprocess.run(
        ["ctest", "--test-dir", str(build), "--show-only=json-v1"],
        check=True, capture_output=True, text=True, encoding="utf-8",
    )
    registered = {test["name"] for test in json.loads(result.stdout)["tests"]}
    missing = sorted(REQUIRED_TESTS - registered)
    if missing:
        raise RuntimeError("HTTP API tests missing: " + ", ".join(missing))
    print("HTTP API test registration: all five required targets present")


class Client:
    def __init__(self, base, token, ca):
        self.base = base.rstrip("/")
        self.token = token
        parsed = urllib.parse.urlsplit(self.base)
        if parsed.username or parsed.password or parsed.query or parsed.fragment:
            raise ValueError("Use an API base URL without credentials, query or fragment")
        if parsed.path != "/api/v1":
            raise ValueError("The base URL must end in /api/v1")
        if parsed.scheme == "http":
            import ipaddress
            if not ipaddress.ip_address(parsed.hostname).is_loopback:
                raise ValueError("Plain HTTP smoke is restricted to literal loopback addresses")
        elif parsed.scheme != "https":
            raise ValueError("Use loopback HTTP or certificate-verified HTTPS")
        context = ssl.create_default_context(cafile=str(ca) if ca else None)
        # Never inherit a proxy that could forward the fixture token elsewhere.
        self.opener = urllib.request.build_opener(
            urllib.request.ProxyHandler({}), urllib.request.HTTPSHandler(context=context)
        )

    def call(self, method, path, body=None, version=None, token=None, headers=None):
        fields = {"Authorization": "Bearer " + (self.token if token is None else token)}
        if headers:
            fields.update(headers)
        if version is not None:
            fields["If-Match"] = '"' + version + '"'
        data = None
        if body is not None:
            fields["Content-Type"] = "application/json"
            data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        request = urllib.request.Request(self.base + path, data=data, headers=fields, method=method)
        try:
            response = self.opener.open(request, timeout=30)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            raw = response.read(2 * 1024 * 1024)
            payload = json.loads(raw) if raw else {}
            return response.status, payload, response.headers

    def get(self, path):
        status, body, _ = self.call("GET", path)
        if status != 200:
            raise RuntimeError("GET failed: " + str(status) + " " + body.get("code", ""))
        return body

    def mutate(self, method, path, body=None, version=None, expected_error=None):
        status, result, headers = self.call(method, path, body, version)
        if status != 202:
            if expected_error and status == expected_error:
                return result
            raise RuntimeError("Mutation admission failed: " + str(status) + " "
                               + result.get("code", ""))
        location = headers.get("Location", "")
        if not location.startswith("/api/v1/operations/"):
            raise RuntimeError("Mutation did not return a local operation URL")
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            operation = self.get(location.removeprefix("/api/v1"))
            if operation["state"] in {"succeeded", "failed", "cancelled"}:
                if expected_error:
                    if operation.get("error", {}).get("status") != expected_error:
                        raise RuntimeError("Mutation did not report the expected conflict")
                    return operation
                if operation["state"] != "succeeded" or not operation.get("committed"):
                    raise RuntimeError("Mutation failed: "
                                       + operation.get("error", {}).get("code", "unknown"))
                return operation
            time.sleep(0.03)
        raise RuntimeError("Mutation completion exceeded its smoke deadline; do not replay it")


def fixture_token(root, path):
    resolved = pathlib.Path(path).resolve(strict=True)
    if not resolved.is_relative_to(root):
        raise ValueError("Keep smoke token files inside the disposable fixture root")
    token = resolved.read_text(encoding="utf-8").strip()
    if not token or "\n" in token or "\r" in token:
        raise ValueError("The fixture token file must contain one issued bearer token")
    return token


def smoke(args):
    root = args.fixture_root.resolve(strict=True)
    marker = root / ".http-api-smoke.json"
    if marker.is_symlink():
        raise ValueError("The disposable-fixture marker cannot be a symlink")
    manifest = json.loads(marker.read_text(encoding="utf-8"))
    if manifest.get("disposable") is not True or not manifest.get("notebookId"):
        raise ValueError("An explicitly disposable notebook fixture is required")
    notebook = manifest["notebookId"]
    token = fixture_token(root, args.token_file)
    client = Client(args.base_url, token, args.ca_file)
    if args.expect_revoked:
        status, _, _ = client.call("GET", "/info")
        if status != 401:
            raise RuntimeError("The revoked token still has API access")
        print("Revoked token: denied after owner revocation")
        return
    if args.read_token_file is None:
        raise ValueError("The full smoke requires a second, read-only fixture token")
    readonly = fixture_token(root, args.read_token_file)
    status, _, _ = client.call("GET", "/info", token="invalid")
    if status != 401:
        raise RuntimeError("Invalid credentials were not rejected")
    info = client.get("/info")
    if info.get("apiVersion") != "1":
        raise RuntimeError("Unexpected API version")
    notebooks = client.get("/notebooks")["items"]
    visible = {item["id"] for item in notebooks}
    if visible != {notebook}:
        raise RuntimeError("The token's notebook allowlist is not isolated to its fixture")
    if notebooks[0]["capabilities"]["trash"] != (not args.expect_no_trash):
        raise RuntimeError("Notebook trash capability does not match the smoke mode")
    status, _, _ = client.call("GET", "/notebooks/not-granted/notes/not-granted")
    if status != 404:
        raise RuntimeError("Unauthorized notebook identity was disclosed")
    print("Authentication and notebook isolation: passed")

    notes = "/notebooks/" + urllib.parse.quote(notebook, safe="") + "/notes"
    marker_text = "VNoteApiSmoke" + uuid.uuid4().hex
    name = marker_text + ".md"
    create = {"name": name, "format": "markdown"}
    status, _, _ = client.call("POST", notes, create, token=readonly)
    if status != 403:
        raise RuntimeError("Read-only token admitted a mutation")
    created = client.mutate("POST", notes, create)
    node = created["resource"]["id"]
    note = notes + "/" + urllib.parse.quote(node, safe="")
    nodes = "/notebooks/" + urllib.parse.quote(notebook, safe="") + "/nodes?limit=200"
    listed = [item for item in client.get(nodes)["items"] if item["id"] == node]
    if len(listed) != 1 or listed[0]["name"] != name:
        raise RuntimeError("Created note is missing from its notebook listing")
    initial = client.get(note + "/content")
    if initial["text"] != "" or initial["view"] != "saved":
        raise RuntimeError("New note is not an empty persisted resource")
    text = "# " + marker_text + "\n\nRésumé 東京\n"
    written = client.mutate("PUT", note + "/content", {"text": text}, initial["contentVersion"])
    if not written.get("saved"):
        raise RuntimeError("Successful content replacement was not persisted")
    current = client.get(note + "/content")
    if current["text"] != text:
        raise RuntimeError("Persisted note content differs from the submitted text")
    client.mutate("PUT", note + "/content", {"text": "must not overwrite"},
                  initial["contentVersion"], expected_error=412)
    if client.get(note + "/content")["text"] != text:
        raise RuntimeError("A stale write changed the note")
    extra = "\nAppended through the HTTP API\n"
    client.mutate("POST", note + "/append", {"text": extra}, current["contentVersion"])
    text += extra
    current = client.get(note + "/content")
    if current["text"] != text:
        raise RuntimeError("Append changed more than the requested text")
    for search_content in (False, True):
        status, results, _ = client.call("POST", "/search", {
            "query": marker_text, "notebookIds": [notebook],
            "searchContent": search_content, "limit": 10,
        })
        if status != 200 or not any(item["id"] == node for item in results["items"]):
            raise RuntimeError("Search did not find the persisted fixture note")
    print("Create, conditional replace, stale-write rejection, append and search: passed")

    metadata = client.get(note)
    if metadata["capabilities"]["trash"] != (not args.expect_no_trash):
        raise RuntimeError("Note trash capability does not match the smoke mode")
    renamed = marker_text + "-renamed.md"
    client.mutate("PATCH", note, {"name": renamed}, metadata["metadataVersion"])
    after = client.get(note)
    if after["id"] != node or after["name"] != renamed:
        raise RuntimeError("Rename did not preserve the node's identity")
    listed = [item for item in client.get(nodes)["items"] if item["id"] == node]
    if len(listed) != 1 or listed[0]["name"] != renamed:
        raise RuntimeError("Rename did not update the notebook listing")
    matches = [path for path in root.rglob(renamed) if path.is_file() and not path.is_symlink()]
    if len(matches) != 1:
        raise RuntimeError("Cannot uniquely locate the fixture note for disk verification")
    saved_path = matches[0]
    if saved_path.read_bytes().decode("utf-8-sig") != text:
        raise RuntimeError("HTTP results do not match the actual on-disk note")
    current = client.get(note + "/content")
    if args.expect_no_trash:
        before_bytes = saved_path.read_bytes()
        before_tree = set(saved_path.parent.rglob("*"))
        rejected = client.mutate("DELETE", note, version=current["noteStateVersion"],
                                 expected_error=422)
        if (rejected.get("state") != "failed"
                or rejected.get("error", {}).get("code") != "unsupported_trash"
                or rejected.get("committed") is not False or rejected.get("saved") is not False):
            raise RuntimeError("Raw trash did not fail as a noncommitted asynchronous operation")
        retained = client.get(note + "/content")
        if (retained["id"] != node or retained["text"] != text
                or retained["contentVersion"] != current["contentVersion"]
                or retained["noteStateVersion"] != current["noteStateVersion"]
                or saved_path.read_bytes() != before_bytes
                or set(saved_path.parent.rglob("*")) != before_tree):
            raise RuntimeError("Rejected trash changed the raw note or created a recycle entry")
        print("Rename, stable identity, disk persistence and nonmutating trash rejection: passed")
        print("HTTP API smoke passed; unsupported raw trash remains unsupported")
        return
    client.mutate("DELETE", note, version=current["noteStateVersion"])
    if saved_path.exists():
        raise RuntimeError("Trash left the original note in place")
    recovered = [path for path in root.rglob(renamed) if path.is_file() and not path.is_symlink()]
    if not any(path.read_bytes().decode("utf-8-sig") == text for path in recovered):
        raise RuntimeError("Trash did not preserve recoverable note bytes")
    status, _, _ = client.call("GET", note)
    if status != 404:
        raise RuntimeError("A trashed note remained available through its live resource URL")
    print("Rename, stable identity, disk persistence and reversible trash: passed")
    print("HTTP API smoke passed; GUI conflict, revocation and shutdown require their owner steps")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-registration", type=pathlib.Path, metavar="BUILD")
    parser.add_argument("--base-url")
    parser.add_argument("--fixture-root", type=pathlib.Path)
    parser.add_argument("--token-file", type=pathlib.Path)
    parser.add_argument("--read-token-file", type=pathlib.Path)
    parser.add_argument("--ca-file", type=pathlib.Path)
    parser.add_argument("--expect-revoked", action="store_true")
    parser.add_argument("--expect-no-trash", action="store_true",
                        help="Require unsupported trash without mutation (raw notebooks)")
    args = parser.parse_args()
    if args.check_registration is not None:
        check_registration(args.check_registration)
    else:
        if not args.base_url or args.fixture_root is None or args.token_file is None:
            parser.error("smoke requires --base-url, --fixture-root and --token-file")
        smoke(args)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, ValueError, OSError, subprocess.SubprocessError) as error:
        print("HTTP API smoke failed:", error, file=sys.stderr)
        sys.exit(1)
