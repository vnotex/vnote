# Update signing

> **The VNote client no longer verifies or applies manifests.** The built-in
> incremental updater was removed: VNote now only *checks* for a newer release
> and points the user at the release page. It downloads nothing and never
> modifies its own install directory (see the root `AGENTS.md`, "Update Check").
>
> Release CI nevertheless keeps producing manifests, delta ZIPs and minisign
> signatures **unchanged**. They exist as the artifact interface for a future
> *external* updater, which is the only component that would consume them.
> Everything below therefore describes the SIGNING side, which is still live,
> and the (now removed) client-side verifier only for historical context.

A downloaded package that some future updater would **execute** cannot be
authenticated by TLS plus a SHA-256 taken from the same origin: that proves only
that the bytes arrived intact from whoever served them, not that the party was
us. The signature is what makes a release self-authenticating, independent of
GitHub, the CDN and the TLS chain.

Format: [minisign](https://jedisct1.github.io/minisign/), so signatures can be
produced and audited with a stock tool rather than something invented here.

| Piece | Where |
|---|---|
| Verifier | **removed** — was `src/core/manifestsignature.{h,cpp}`; a future external updater owns this |
| Crypto (vendored, verify-only) | **removed** — was `libs/minicrypto/` (TweetNaCl + BLAKE2b) |
| Trusted public keys | see "Public keys" below; no client currently embeds them |
| Signing | `scripts/gen-update-package.ps1`, `-MinisignSecretKey` |
| CI wiring | `.github/workflows/ci-win.yml`, `MINISIGN_SECRET_KEY` secret |
| Published asset | `VNote-<ver>-<variant>.manifest.json.minisig` |

## Public keys

| Role | Key id (as minisign displays it) |
|---|---|
| Active — held by CI as `MINISIGN_SECRET_KEY` | `334F7ED65256CDE8` |
| Cold spare — private half stored offline, never in CI | `B56AD74F9A82C266` |

Both were shipped from the first signed release, which is what makes the
rotation story below workable. Any external updater MUST embed both.

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

There is no client-side verifier today; this step belongs to whatever external
updater is built. Paste the **second line** of each `.pub` file (the base64
body, not the `untrusted comment:` line) into its trusted-key list, e.g.:

```cpp
QStringList trustedPublicKeyStrings() {
  return QStringList{
      QStringLiteral("RWQf6LRCGA9i53mlYecO4IzT51TGPpvWucNSCh1CBM0QTaLn73Y7GFO3"), // active
      QStringLiteral("RWSpNBgvarErINGfzyYvN8eldfAcodQJWLw6/p0fsiTv3QEKLaPI/ALS"), // cold spare
  };
}
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
> than to debug: use a repository secret. (No client verifies today, so an
> unsigned manifest breaks nothing immediately — which makes it *more*
> important that the build fails loudly rather than shipping one quietly.)
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

The public keys are **compiled into the verifying binary**, so a build that
trusts exactly one key can never be migrated: if that key is lost or retired,
every already installed client is cut off from updates permanently and can only
be recovered by a manual reinstall. Hence the list, and hence the cold spare.

To rotate:

1. Start signing with the **spare** key (swap the CI secret).
2. In the next release, add a **new** spare to the verifier's trusted-key list.
3. Drop the retired key only once telemetry/soak time says the population has
   moved to a build that trusts the new pair.

Keep a test in the verifying project that enforces the invariant: once any real
key is configured, at least two must be present.

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
(The removed client did both; a future external updater must too.)

## What is and is not signed

The signature covers the **release-asset manifest** —
`VNote-<ver>-<variant>.manifest.json` — over its exact bytes as served. Since
that manifest contains the SHA-256 and size of the full package, of the delta
archive, and of every individual file, one signature transitively authenticates
every byte an updater would download or install.

The in-package `manifest.json` (inside the ZIP) is *not* separately signed; a
consumer is expected to check it against the signed release manifest.

The minisign `untrusted comment:` line is, as the name says, **not**
authenticated — do not rely on it. The `trusted comment:` line *is* covered, by
the global signature.
