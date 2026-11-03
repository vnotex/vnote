# VNote HTTP API integration

This is the application adapter, not the HTTP or REST implementation. Read
[`../../http/AGENTS.md`](../../http/AGENTS.md) for transport and
[`../../api/AGENTS.md`](../../api/AGENTS.md) for protocol/policy. Consumer contract:
[`../../../docs/http-api.md`](../../../docs/http-api.md).

## Dependency direction

`vnote_httpapi` links the independent `vnote_api` target and VNote services. Neither
`vnote_api` nor `vnote_http` may include ServiceLocator, vxcore, editor, widget or
application configuration headers. Core services never depend on API/HTTP types.
`HttpApiController` is the GUI-owner composition and settings command interface;
`VNoteApiBackend` maps typed API operations to application services.

## Startup and shutdown

- The controller is registered after normal-primary startup passes the
  single-instance guard. Existing show/open-file IPC is unchanged.
- Only `MainWindow2::postInitFinished`, at the end of the existing session-restore
  handoff, makes the controller ready. `MainWindowAfterStart` initiates restoration
  and is too early. Argument-parse errors never connect the readiness signal.
- Disabled/invalid configurations create no listener. Bind only canonical literal
  loopback addresses; a port conflict fails, never silently changes ports.
- Remote clients use an administrator-managed HTTPS reverse proxy on the same
  host. The application installs no certificates, firewall rules or proxy service.
- API teardown is FIRST in the existing direct `aboutToQuit` handler: cancel and
  detach API response waiters before joining HTTP workers, then drain normal
  sync/save services. Never join a worker that still needs a GUI completion.
- A cancelled close leaves the API usable. Listener reconfiguration recreates the
  backend/server; authentication state survives disable/re-enable. Final shutdown
  is idempotent and prevents restart within that controller's lifetime.

## Credentials

`ApiCredentialStore` implements the API's storage interface for a dedicated
`http-api/tokens.json` profile directory. It stores only token verifiers and grants,
not bearer secrets. It is not notebook/session/sync data and not a replacement for
`SyncCredentialsStore`'s keychain policy.

- An absent read never creates the store. Explicit owner provisioning works while
  the HTTP listener is disabled.
- Reject corrupt/oversized JSON, symlinks/reparse points, unexpected ownership and
  broad permissions. Do not heal a corrupt registry or replace it with defaults.
- POSIX directories/files require owner-only permissions. Windows requires an
  explicit owner DACL; `QFile::setPermissions` is not a Windows ACL substitute.
- `QSaveFile` direct-write fallback stays disabled. Verify temporary/final file
  protections and report persistence failures before publishing successful grants.
- Issue secrets once. Invalid grants and token-quota rejection leave the listener running;
  storage/protection failures stop it. Revocation removes in-memory authority immediately; if it
  cannot be persisted, stop HTTP and report that revocation is not durable.
- Do not log credentials, verifiers, note names/bodies, queries or filesystem paths. This
  includes shared buffer, node-resolution, metadata-persistence, hook, comment and search diagnostics,
  not just the router: debug logs and ripgrep commands must remain payload-free. Use
  existing stable IDs and error codes; the controller supplies translated storage errors.

## Backend ownership

- All live services, buffer/editor callbacks and metadata are accessed on the GUI
  owner. Worker reads use owned snapshot inputs and no-follow path handles, never
  a borrowed service or Buffer2.
- Resolve persistent node UUIDs afresh. A relative path is metadata, not identity.
  Exclude app configuration/control files and protected nodes, including unlocked
  protected editors. Metadata capability flags do not authorize binary reads.
- Saved and live content are distinct. Capture live writers without saving or
  changing the core buffer. A missing writer may explicitly fall back to saved;
  a failed writer capture must return an error instead.
- Search reuses the one existing SearchService and drain pool. Track only API-owned
  request tokens; filter all signals and call `cancel(token)`, never `cancel()`.
  Restrict candidates before search and verify saved snapshots before returning
  results. Do not leak candidates, snippets or counts from denied resources.
- Shared `NoteOperationService` owns mutation semantics. It uses existing buffer
  reservations, save queue and NotebookIoGate, refuses dirty editor conflicts and
  rechecks authorization at commitment. HTTP handlers do not write files.
- Raw and bundled notebooks share ordinary read/search/create/edit/rename routes. Raw
  UUIDs are profile-backed and must survive context restart; explicit notebook removal
  is a different lifecycle and may discard that identity. Test nested folder discovery
  and creation by UUID, not just the synthetic root.
- Raw trash remains unsupported. An authorized admitted DELETE ends in
  `422/unsupported_trash` with no committed/saved changes; do not call legacy deletion.
- Strict trash uses `vxcore_node_trash`, never legacy deletion: legacy bundled
  delete can permanently remove a file if recycling fails. Keep recovery-required,
  committed and saved outcomes distinct.

## Configuration and settings

`HttpApiConfig` is a typed MainConfig child in both app and core_configs builds.
Access it only through ConfigMgr2's merged configuration. Its persisted listener fields are
`enabled`, `address` and `port`; public HTTPS URLs belong in proxy/client configuration, not here.
Undeclared input keys are ignored and omitted by normal serialization/save. Missing-after-merge,
null or malformed values stay invalid through unrelated preference saves; validate
before reading listener settings even when enabled is false.

The Settings page is a view. Issue/revoke are explicit immediate controller
commands; resetting the page changes no grants. Default token choices are read-only
and no notebooks selected. Expiry and grants are explicit. The page reports actual
runtime state when applying settings fails, not merely the persisted enabled flag.

## Verification

`test_httpapilifecycle` covers real protected registry persistence, revocation and
HTTP listener lifecycle. `test_vnoteapibackend` covers real service-backed snapshots,
authorization and mutations for both raw and bundled notebooks, including raw identity
restoration and nonmutating trash rejection. The real HTTP smoke runner's `--expect-no-trash`
mode verifies the raw contract; its default mode must still verify bundled recycling.
Transport/API policy have separate independent tests.
Use the isolated portable-profile rules in [`../../../tests/AGENTS.md`](../../../tests/AGENTS.md)
for actual application smoke runs; `--no-single-instance` alone is not isolation.
