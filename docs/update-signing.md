# Update signing

> **Ordinary update checks remain metadata-only and read-only for the installation.**
> `UpdateService` ignores release assets and `UpdateInfo` stays unchanged. Only
> explicit activation of Windows' **Update Now** action in the startup notification
> or manual-check dialog may launch the deployed external PowerShell updater.
> Keep **Check Release** / **Open Release Page** available, disclose downloading,
> closing and reopening VNote, and never make the dialog's updater action its default.
> Do not restore the removed C++ installer.
>
> The external updater verifies signed full packages or reconstructs the complete
> target payload from an eligible signed delta. Windows `win64` releases still
> publish a full ZIP, release manifest and minisign signature; deltas are optional.
> Signed `fullPackage` metadata is always required, but a successful eligible delta
> needs neither the full archive's presence on the selected source nor its download.

A downloaded package that the external updater will **execute** cannot be
authenticated by TLS plus a SHA-256 taken from the same origin: that proves only
that the bytes arrived intact from whoever served them, not that the party was
us. The signature is what makes a release self-authenticating, independent of
GitHub, Gitee, their CDNs and the TLS chain.

Format: [minisign](https://jedisct1.github.io/minisign/), so signatures can be
produced and audited with a stock tool rather than something invented here.

| Piece | Where |
|---|---|
| Consumer | `scripts/update-vnote.ps1`, deployed as `updater/update-vnote.ps1` |
| Verifier | Bundled minisign **0.11**, deployed as `updater/minisign.exe`; ISC license at `updater/LICENSE.minisign` |
| Preparation | `package/prepare-win-updater.cmake`, deploy-only target `prepare_win_updater` |
| Trusted public keys | Both literal keys below, embedded in the deployed script; no production override |
| Signing | `scripts/gen-update-package.ps1`, `-MinisignSecretKey` |
| CI wiring | `.github/workflows/ci-win.yml`, `MINISIGN_SECRET_KEY` secret |
| Published asset | `VNote-<ver>-win64.manifest.json.minisig` |

## External updater contract

- Snapshot the configured GitHub/Gitee source at action activation (Gitee is the
  normalization default). Require the **exact offered version**, a numeric
  three-component stable version newer than the running version, and the Qt 6
  x64 `win64` package; never resolve the latest release during installation.
- GitHub uses public assets under
  `https://github.com/vnotex/vnote/releases/download/v<version>/`, with exact names
  `VNote-<version>-win64.zip`, optional `VNote-<version>-win64.delta.zip`,
  `VNote-<version>-win64.manifest.json` and
  `VNote-<version>-win64.manifest.json.minisig`. The updater makes no release-metadata
  API request, creates no synthetic release record and needs no API token or retry.
  GitHub's mutable draft/prerelease flags are not queried or treated as an
  authentication control: the verified manifest must bind the offered version and
  `channel: stable`. Ordinary update checks for both sources use website HEAD discovery,
  not these asset downloads; see [UpdateService](../src/core/services/AGENTS.md#updateservice).
- Gitee retains exact-tag API discovery, matching tag and non-prerelease checks,
  rejection of a draft flag if present and not false, and selection of exact,
  unique manifest, signature and selected archive asset names. Resolve the archive
  only after signed eligibility is known; do not inspect ineligible delta assets
  or require a full archive when delta preparation succeeds.
- Missing required selected-source assets fail closed without a source or metadata-API
  fallback. An absent eligible delta may select the same signed full package only
  under [the delta rules below](#delta-preparation-and-full-fallback). Validate every
  HTTPS redirect against that source's disjoint host allowlist, retaining normal
  system certificate/proxy behavior.
- Run only the copied bundled verifier, never one downloaded at update time or
  resolved from PATH. Preparation pins the archive, executable and upstream
  license by SHA-256 and performs no network fetch during configure or normal
  builds. The three updater files are non-optional package contents. Prepend the
  installation root to the verifier process's PATH for its bundled runtime DLLs.
  Minisign 0.11 uses narrow Windows file APIs: invoke it from the manifest's
  directory with relative file arguments, keeping Unicode ancestors in the
  working directory rather than argv. Release filenames remain ASCII. Signing
  likewise uses the output directory and a relative path to the secret key;
  key-path components outside the shared ancestor must be representable in the
  system code page. Restore the caller's location on success and failure.
- Verify **exact manifest bytes before parsing**, using `minisign -V -P <key>
  -m <manifest> -x <signature>` with individual arguments. One embedded trusted
  key must verify both the manifest and trusted-comment signatures. Missing
  verifier/signature, unknown key or bad signature is fatal; no unsigned,
  TLS-only, hash-only or user-supplied-key production fallback is allowed.
- Validate schema/identity, fixed `variant: win64`, mandatory `fullPackage` and
  any present `delta` metadata, then the selected archive's signed size and SHA-256.
  Bound streaming downloads and validate paths and entry sets before extraction;
  reject traversal, aliases, duplicates, conflicting prefixes, reparse/symlink
  entries, undeclared files, archive bombs and any root `config` payload. Check
  extracted file sizes/hashes and the completed in-package manifest against the
  signed target file table. Delta reconstruction follows the rules below.
- Finish authentication, extraction and permission/space/path preflight **before
  asking VNote to close**. The launch-scoped pipe authenticates both process
  identities and a per-attempt token; only a complete explicit `ACCEPTED` response
  to READY authorizes installation. The normal save/close prompts may cancel.
  EOF, parent exit alone or timeout is not consent. Wait on the retained original
  process handle through save/sync drains; never force-kill a process or extend
  the general single-instance protocol with a quit opcode.
- After shutdown, clone the complete installation into an exclusive sibling,
  including portable `config` and unowned files, then overlay verified files.
  Reject reparse points and incompatible file/directory collisions rather than
  deleting unknown contents. Rehash the payload and copied portable config,
  rename the original to a retained recovery backup and rename staging into
  place; restore the backup if the second rename fails. These two renames are
  **not power-loss atomic**. Preserve the complete backup on post-swap errors.
- Never elevate, change ACLs or install dependencies. Unwritable/protected
  installations require **Check Release** manual-update guidance. Reopen root
  `vnote.exe` with the helper's inherited token and no replayed file arguments;
  check exact-path process identity and five-second survival, not just launch
  success. An immediate exit/secondary-instance handoff is **installed, but not
  reopened**, not success; keep the backup and report manual-launch guidance.
  Failures release the pipe/locks and remain visible in the console for retry.

These requirements add no settings, configuration migration, writes or installer
dependencies to ordinary checks. The root `AGENTS.md` and `src/controllers/AGENTS.md`
own the entry-point and cancellable-close boundaries.

### Delta preparation and full fallback

- The existing artifact schema and generator behavior are unchanged. Authenticate
  the release manifest before using `delta`; malformed present metadata is fatal,
  even when the delta would not be eligible. Require the exact asset name
  `VNote-<version>-win64.delta.zip`, a positive integral size no greater than 4 GiB,
  SHA-256 and a numeric three-component `baseVersion` older than the target. Prefer
  delta only when `baseVersion` exactly matches the running version and its signed
  archive size is strictly smaller than `fullPackage.size`. No delta chains.
- The delta ZIP contains only changed/new, install-root-relative files declared
  by the signed target table: no wrapper directory and no `manifest.json`. Use
  the same held-archive verification and guarded extraction as full packages,
  accepting a subset of target files and verifying every supplied file.
- Complete missing target files from the live installation into private staging
  using read-only source handles. Bound copies by signed target sizes and verify
  staged bytes against target hashes while the destination is write-locked. The
  installed manifest is not a trust source. Synthesize `manifest.json` solely from
  the authenticated target schema, identity and complete file table, without
  `fullPackage` or `delta` references. Revalidate the entire reconstructed payload
  before READY; preparation never changes the original installation.
- Select the same source's signed full package only when delta metadata is absent
  or ineligible, the eligible delta asset is absent from Gitee's exact-tag asset
  list, an eligible delta GET returns exactly HTTP 404 through an otherwise valid
  redirect path, or a required reusable local file is missing or has a target
  size/hash mismatch. A 404 uses the download's response headers, not a separate
  HEAD request or a retry. Remove only the updater-owned delta staging/archive
  before fallback and repeat the full-package space preflight. If full is needed,
  its exact selected asset must exist and pass the same signed verification.
- Corrupt signatures, invalid metadata, unsafe/duplicate selected assets, bad
  archive size/hash, unsafe ZIP entries or changed-file mismatches are fatal, not
  reasons to download full. Other HTTP statuses, redirect-policy failures,
  timeouts, truncated downloads, access/sharing errors, reparse hazards, other
  I/O failures and cancellation are also fatal. Never switch source, downgrade
  authentication or treat arbitrary download failures as absence.
- Reconstruction does not delete files omitted from the target table. The
  existing stopped-tree clone/overlay/swap, portable `config`, unowned-file,
  collision-refusal, complete-backup and rollback rules above are unchanged.
  There is no C++ or in-process delta application or new quit protocol.
- Already-installed updater scripts remain full-only until a release containing
  the new helper has been installed. Publishing a delta alone does not upgrade
  those scripts or change ordinary checks.

## Public keys

| Role | Key id (as minisign displays it) |
|---|---|
| Active — held by CI as `MINISIGN_SECRET_KEY` | `334F7ED65256CDE8` |
| Cold spare — private half stored offline, never in CI | `B56AD74F9A82C266` |

Both were shipped from the first signed release, which is what makes the
rotation story below workable. The external script MUST embed both actual key
bodies below, recovered from the previously shipped verifier, not illustrative
replacement keys. Generating a new pair does not replace this installed trust root.

## One-time setup

### 1. Generate the keypair

Do this on a trusted machine, not in CI. The **active** key is used by CI and
must have an empty password (`-W`); see step 3 for why.

```pwsh
minisign -G -W -p vnote-update.pub -s vnote-update.key
```

Generate a **second, cold-spare** keypair at the same time and store it offline
(hardware token, paper, offline media — not in CI, not in the repo). Give this
one a strong password, since it lives on removable media:

```pwsh
minisign -G -p vnote-update-spare.pub -s vnote-update-spare.key
```

### 2. Install both public keys in the verifier

The trusted-key list belongs to `scripts/update-vnote.ps1`, not a C++ verifier.
Use the **second line** of each `.pub` file (the base64 body, not the
`untrusted comment:` line). The existing release keys are:

```powershell
# Active: 334F7ED65256CDE8
'RWTozVZS1n5PM5euO7/ieR6o6daenLdTCK0EIhYnf0ACb47j6usoRtnJ'
# Cold spare: B56AD74F9A82C266
'RWRmwoKaT9dqtSFII74FcPaet3Ork43BpcJx/SOnuiX3JR6wG9864WjO'
```

An empty trusted-key list MUST be **fail-closed**: verification refuses
everything and the user is sent to the release page. An unsigned update path is
strictly worse than no update path, so "no key configured" must never be read as
"signature optional".

Ship **both from the very first signed release**. See "Rotation" for why this is
not optional.

### 3. Add the CI secret

Repository → Settings → Secrets and variables → Actions → **Repository secrets**
→ New repository secret:

- **Name:** `MINISIGN_SECRET_KEY`
- **Value:** the entire contents of `vnote-update.key` (both lines)

Only the **active** key goes into CI. The spare stays offline; putting both in
CI would defeat its purpose entirely.

> **It must be a REPOSITORY secret, not an ENVIRONMENT secret.**
>
> `${{ secrets.MINISIGN_SECRET_KEY }}` only resolves repository and organization
> secrets. The `build` job in `ci-win.yml` declares no `environment:`, so a
> secret defined under Settings → Environments resolves to an **empty string** —
> the generator would take its "no key" branch and publish an **unsigned**
> manifest while the build stayed green. Any verifying consumer would then
> refuse the release.
>
> The workflow now hard-fails a `[Release]` build when the key is missing, and
> the error message names this cause. But the failure is far easier to avoid
> than to debug: use a repository secret. Windows releases require signed
> manifests; an unsigned manifest is unusable by the external updater. Keep
> secret files outside the workspace and sign with the already prepared pinned
> minisign executable, never a second unpinned download.
>
> If you ever do want environment-scoped protection (master-only, required
> reviewers), do **not** simply add `environment:` to the `build` job — that job
> runs on every push and PR, so a deployment-branch rule would fail ordinary CI
> and required reviewers would stall it. Split signing into its own job gated on
> the release condition instead.

> **The CI key must have an EMPTY password.** minisign reads a passphrase from
> the console, not from stdin, so there is no way to feed one to an unattended
> job — a password-protected key would make the signing step hang until the CI
> timeout rather than fail. The generator detects this and errors out early.
>
> This is not a weakening: a passphrase stored next to the key in the same
> secret store protects nothing. The key's protection is the secret store.
> The **offline spare** is different — give it a strong password, because it
> lives on removable media and is only ever used by a human.

### 4. Verify a release by hand, once

```pwsh
minisign -V -p vnote-update.pub `
  -m VNote-4.3.2-win64.manifest.json `
  -x VNote-4.3.2-win64.manifest.json.minisig
```

Expected output includes `Signature and comment signature verified` and a
trusted comment of the form `VNote 4.3.2 win64 stable commit <sha>`.

## Rotation

The public keys are **embedded in the deployed updater script**, so an installed
updater that trusts exactly one key cannot accept a replacement signed only by a
new key. If the old key is lost or retired, recovery requires a manual update.
Hence the list, and hence the cold spare.

To rotate:

1. Start signing with the **spare** key (swap the CI secret).
2. In the next release, add a **new** spare to the verifier's trusted-key list.
3. Drop the retired key only after supported installed updaters have had time
   to move to a release that trusts the new pair. Otherwise they require manual
   updates; signing cannot retroactively change their embedded trust roots.

Retain at least two production trusted keys. Test fixtures may substitute an
ephemeral key only in dot-sourced test scope, never through a production CLI or
environment signature-bypass switch.

## Threat model, stated plainly

**Protects against:** a compromised or malicious CDN/mirror, a
GitHub-release-asset swap, TLS interception with a mis-issued certificate, and
corruption anywhere in transit.

**Does NOT protect against:** compromise of the signing key itself, or of the CI
job while it holds the key. `MINISIGN_SECRET_KEY` lives in GitHub Actions, so an
attacker with repository-admin access or the ability to run arbitrary workflow
code can sign a malicious update. If that is unacceptable, move to offline
signing: drop `MINISIGN_SECRET_KEY` from CI, let the workflow publish the draft
release unsigned, and have the maintainer sign the manifest locally and attach
the `.minisig` before publishing. No verifier change is needed — verification
only ever reads the published signature.

**Also does NOT protect against:** a signed-but-old manifest being replayed.
Downgrade must be blocked separately, by the consuming updater requiring the
target version to be strictly newer than the installed one and rejecting a
manifest whose `version` field does not match the version it was requested for.
The external script must enforce both checks before requesting shutdown.

## What is and is not signed

The signature covers the **release-asset manifest** —
`VNote-<ver>-win64.manifest.json` — over its exact bytes as served. Since
that manifest contains the SHA-256 and size of the full package, optional delta
archive, and every individual payload file, one signature transitively
authenticates the bytes the external updater downloads or installs. The updater
uses mandatory `fullPackage`, complete target `files` and eligible optional `delta`;
a signature alone does not establish delta eligibility or trust local base bytes.

The full ZIP's in-package `manifest.json` is *not* separately signed; the external
updater must compare its schema, identity and file table to the signed release
manifest. Delta reconstruction synthesizes that target-only manifest from the
authenticated release manifest, never the installed manifest. Only the release
manifest has `fullPackage`/optional `delta`.

The minisign `untrusted comment:` line is, as the name says, **not**
authenticated — do not rely on it. The `trusted comment:` line *is* covered, by
the global signature.
