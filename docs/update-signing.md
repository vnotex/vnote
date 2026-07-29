# Update signing

VNote's incremental updater downloads files and then **executes** them. TLS plus
a SHA-256 taken from the same origin proves only that the bytes arrived intact
from whoever served them — it says nothing about whether that party is us. The
signature is what makes a release self-authenticating, independent of GitHub,
the CDN and the TLS chain.

Format: [minisign](https://jedisct1.github.io/minisign/), so signatures can be
produced and audited with a stock tool rather than something invented here.

| Piece | Where |
|---|---|
| Verifier | `src/core/manifestsignature.{h,cpp}` |
| Crypto (vendored, verify-only) | `libs/minicrypto/` — TweetNaCl + BLAKE2b |
| Trusted public keys | `trustedPublicKeyStrings()` in `manifestsignature.cpp` |
| Signing | `scripts/gen-update-package.ps1`, `-MinisignSecretKey` |
| CI wiring | `.github/workflows/ci-win.yml`, `MINISIGN_SECRET_KEY` secret |
| Published asset | `VNote-<ver>-<variant>.manifest.json.minisig` |

## Current state

The active and cold-spare public keys are installed in
`trustedPublicKeyStrings()`:

| Role | Key id (as minisign displays it) |
|---|---|
| Active — held by CI as `MINISIGN_SECRET_KEY` | `334F7ED65256CDE8` |
| Cold spare — private half stored offline, never in CI | `B56AD74F9A82C266` |

`test_manifestsignature` asserts both are present, valid and distinct, so an
accidental deletion or a typo fails the build rather than shipping quietly.

The updater becomes active once `MINISIGN_SECRET_KEY` exists as a **repository**
secret (step 3) and a release is built with it. Until then a release build fails
loudly rather than publishing something no client would accept.

## What "unconfigured" means

If `trustedPublicKeyStrings()` were ever emptied:

- `ManifestSignature::verify()` returns `NoTrustedKeys` for everything;
- `UpdateService::checkEligibility()` reports the install ineligible with
  *"This build has no update signing key configured"*;
- the UI offers the releases page instead of an in-app update.

That is **fail-closed** by design. An unsigned update path is strictly worse
than no update path, so "no key configured" must never be read as "signature
optional".

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

### 2. Install both public keys in the client

Paste the **second line** of each `.pub` file (the base64 body, not the
`untrusted comment:` line) into `trustedPublicKeyStrings()`:

```cpp
QStringList trustedPublicKeyStrings() {
  return QStringList{
      QStringLiteral("RWQf6LRCGA9i53mlYecO4IzT51TGPpvWucNSCh1CBM0QTaLn73Y7GFO3"), // active
      QStringLiteral("RWSpNBgvarErINGfzyYvN8eldfAcodQJWLw6/p0fsiTv3QEKLaPI/ALS"), // cold spare
  };
}
```

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
> manifest while the build stayed green. Every client would then refuse the
> release and updates would break silently.
>
> The workflow now hard-fails a `[Release]` build when the key is missing, and
> the error message names this cause. But the failure is far easier to avoid
> than to debug: use a repository secret.
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

The public keys are **compiled into the binary**, so a build that trusts exactly
one key can never be migrated: if that key is lost or retired, every already
installed client is cut off from updates permanently and can only be recovered
by a manual reinstall. Hence the list, and hence the cold spare.

To rotate:

1. Start signing with the **spare** key (swap the CI secret).
2. In the next release, add a **new** spare to `trustedPublicKeyStrings()`.
3. Drop the retired key only once telemetry/soak time says the population has
   moved to a build that trusts the new pair.

`test_manifestsignature` enforces the invariant: once any real key is
configured, the suite fails unless at least two are present.

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
the `.minisig` before publishing. The client needs no change — it only ever
verifies.

**Also does NOT protect against:** a signed-but-old manifest being replayed.
Downgrade is blocked separately, by `UpdateService` requiring the target version
to be strictly newer than the installed one, and by `fetchVerifiedManifest()`
rejecting a manifest whose `version` field does not match the version it was
requested for.

## What is and is not signed

The signature covers the **release-asset manifest** —
`VNote-<ver>-<variant>.manifest.json` — over its exact bytes as served. Since
that manifest contains the SHA-256 and size of the full package, of the delta
archive, and of every individual file, one signature transitively authenticates
every byte the updater downloads or installs.

The in-package `manifest.json` (inside the ZIP) is *not* separately signed; it
is checked against the signed release manifest during staging.

The minisign `untrusted comment:` line is, as the name says, **not**
authenticated — do not rely on it. The `trusted comment:` line *is* covered, by
the global signature.
