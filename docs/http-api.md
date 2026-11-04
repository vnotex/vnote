# VNote HTTP API

VNote exposes an opt-in note-automation API for non-browser clients, including a separately
maintained MCP server. It is not an MCP server, login/account service, filesystem gateway,
headless product, or public TLS listener. The machine-readable contract is
[OpenAPI 3.1](http-api.openapi.yaml).

## Enable and authorize a client

1. Open **Settings → HTTP API** in the normal primary VNote application. Keep a notebook open.
2. Under **Client Tokens**, click **New**. In **Issue Client Token**, enter a label, select
   notebooks, choose permissions and an expiry preset, then click **Issue Token**. Choose
   **1 day, 7 days, 30 days, 60 days, 90 days, 180 days, or Never**. New grants default to
   read-only and 90 days; select notebooks deliberately.
   A grant never automatically includes notebooks opened or created later.
3. Click **Copy** when the token is shown. This copies the entire token without closing the
   dialog. Copying is explicit; closing the dialog does not clear the clipboard. The token is
   displayed only once. Store it in the client's secret store, not in notebook content, a URL,
   a command history, source control or an MCP tool argument.
4. Enable the listener on `127.0.0.1:18765` (or another explicit port). `::1` is an explicit
   alternative. Wildcard/public binds and automatic port fallback are not supported.
5. Send `Authorization: Bearer <id.secret>` on every request, including `/info` and operations.
   There is no remote token-management endpoint. Revoke a client from Settings.

Listener settings (`enabled`, `address`, `port`) persist under `httpApi` in the local
`session.json`, across restarts. They are not stored in the main `vnotex.json`; any HTTP API
settings there are ignored, not migrated. An absent section defaults to disabled.

Token issuance is available while the listener is disabled. Merely leaving the feature disabled
creates neither a listener nor a credential registry. The verifier registry is separate from
ordinary debounced preferences, protected for the OS owner, and contains SHA-256 verifiers,
not recoverable bearer secrets. At most 64 unexpired enabled tokens may be issued; revocation
frees a slot and later issuance prunes expired/disabled records. Issuance is published only after
durable storage succeeds. Revocation takes effect in memory immediately; if persistence fails,
the listener stops and VNote posts a persistent error notification; the listener status also
reflects the failure. Successful issuance, copying and revocation add no status footer or
notification. Do not treat failed persistence as durable revocation: fix storage protections
before re-enabling.

Finite expiry is calculated when the token is issued, using the selected number of local calendar
days. The token list displays the local expiry date without time-of-day detail. **Never** disables
automatic expiry, not authorization: notebook grants, scopes, disabled state and revocation still
apply. Never tokens also count toward the 64-token limit. Precise finite UTC timestamps remain
internal; a non-expiring token is represented explicitly, not by a distant placeholder date.

On Windows, the registry is `%LOCALAPPDATA%/VNote/http-api/tokens.json`, not the roaming
configuration directory. Portable Windows profiles use `config/local/http-api/tokens.json`
beside the executable. Other platforms keep it in their application-data profile, not a cache.
This storage choice avoids ordinary Windows profile roaming; it is not cryptographic machine
binding, and deliberately copying a valid registry can still make its tokens usable elsewhere.

The bearer combines a random ID and an OS-random secret. Its verifier is an unkeyed SHA-256 hash
of the complete `id.secret` value; no separate signing, encryption or HMAC key is used. Validation
compares digests in constant time and separately enforces expiry, enabled state and grants.

Scopes are independent grants:

| Scope | Permission |
| --- | --- |
| `notes:read` | Notebook/node listings, metadata, saved content and saved-content search |
| `notes:read-live` | Additional permission for `view=live`; also requires `notes:read` |
| `notes:write` | Empty create, replace, exact append and rename |
| `notes:trash` | Strict reversible trash of supported ordinary notes |

`GET /info` and caller-owned operation polling require a valid token; polling additionally
rechecks the operation's notebook/action grant. An absent/expired/revoked/malformed token gives
401. A notebook outside the grant is indistinguishable from an absent notebook (404).
Insufficient action scope within a granted notebook gives 403. Protected/encrypted notes are
omitted from listings/search (including names, snippets and counts), even when unlocked in an
editor. Direct authorized access fails with `protected_note_unavailable` (422). Binary/specialized
notes may have metadata with `contentReadable=false`; content access gives
`unsupported_note_type` (422). Capability flags, not a filename extension, determine support.

## Access auditing

While the listener is enabled, normal launches write structured JSON messages through the
`vnote.api.audit` INFO category to the existing `vnote.log` in VNote's local-data directory
(portable: `config/local/vnote.log`). No `--verbose` flag is required. The normal log prefix
precedes each JSON object; this is not a separate JSON-lines file.

- `api.access` records each nonempty HTTP attempt observed by a worker, including denied,
  malformed, partial and timed-out requests. Fields are UTC `timestamp`, server `requestId`,
  authenticated `tokenId`, allowlisted `method`, matched route template `route`, HTTP `status`,
  monotonic `durationMs` and `responseWritten`. Unknown fields are JSON null. Unknown methods
  use `OTHER`; no raw method string is retained. The ID matches the single `X-Request-Id`
  response header and any problem-body `requestId`; client-supplied IDs are ignored.
- `tokenId` is null until authentication succeeds, and `route` is null until route matching.
  The existing Host/Origin/Cookie checks run before authentication. Templates replace resource
  IDs with placeholders, and never include a query string.
- `api.operation` records an asynchronous mutation result, joined to its accepting request by
  `requestId`, with `operationId`, `state`, `status`, `durationMs`, `outcomeKnown`, `committed`
  and `saved`. A 202 access record means accepted, not committed. Failure can still mean
  `committed: true`, including a save or post-save failure; never infer rollback from status.
- Shutdown before dispatch records a known cancellation. Shutdown of a running mutation emits
  `api.operation_interrupted` with unknown outcome. A retained completion may subsequently emit
  one `api.operation`: a positive commitment preserves its committed/saved facts; a generic
  post-stop failure remains `outcomeKnown: false` with null committed/saved. Work detached from
  the API can finish without a final observable API result; reconcile it rather than replaying.
- `api.connection_rejected` records accepted connections rejected before HTTP parsing, with
  fixed reason `queue_full`, `stopping` or `socket_setup_failed`. It invents no request identity
  or HTTP status. Empty connections, idle keepalive EOF (including one ignored empty line),
  and connections dropped outside the application are not HTTP access records.

Audit messages contain no bearer secrets, verifier hashes, labels, note names/content, search
queries, raw URLs, filesystem paths, request/response headers or arbitrary backend error text.
`responseWritten` means the server completed its write, not proof the client received it;
`status` is null if no response was prepared. Durations exclude time waiting in the socket queue.

These are **operational logs, not tamper-proof or crash-durable security records**. `--quiet`
or INFO category filters suppress them; debug builds and `--log-stderr` use stderr. An
unwritable log file falls back to stderr. The existing logger truncates `vnote.log` on the next
startup if it is at least 5 MiB; it does not cap growth during a session or retain rotated files.
Preserve logs externally when retention is required. See [logging](logging.md).

## Notebook types

Granted, currently open **raw and bundled notebooks** use the same listing, saved/live read,
search, create, replace, append and rename endpoints. Ordinary Markdown/text notes still require
usable stable IDs and the appropriate scopes; read-only and protected-note restrictions apply.

Raw identities live in the local profile's metadata database. Restarting VNote with that profile
preserves them; removing a raw notebook and adding it again is not identity-preserving. Do not
substitute filesystem paths for node IDs or automatically grant a newly added notebook access.

Raw notebooks report `capabilities.trash=false`. An otherwise valid DELETE with `notes:trash`
is admitted as 202, then its operation fails with `error.status=422`, `code=unsupported_trash`,
`committed=false` and `saved=false`. The file is neither moved nor deleted. Bundled notebooks
retain the strict reversible-trash implementation; neither backend gets a permanent-delete fallback.

## HTTPS for other machines

Use a separately installed reverse proxy on the **same host**:

```text
HTTPS client → Caddy / administrator-managed TLS → 127.0.0.1:18765 → VNote
```

Start from [http-api.Caddyfile.example](http-api.Caddyfile.example), replace the public hostname,
and configure your own certificate/renewal, authentication-secret handling, connection/per-IP
rate limits, firewall and DNS. Caddy is not bundled, launched, or privileged by VNote. Never expose
the embedded HTTP port publicly or redirect a public port directly to it. Configure the public
HTTPS URL in the reverse proxy and its clients; VNote only needs its loopback address and port.

The gateway must preserve Authorization and set the upstream Host to exactly `127.0.0.1:18765`
(or `[::1]:PORT` for explicit IPv6). VNote rejects every Origin-bearing request, any Cookie header,
wrong/duplicate Host and duplicate Authorization. There is no CORS, cookie authentication,
browser UI, Swagger mount, WebSocket or SSE. Do not use a browser-based OpenAPI “try it” against
this API. `Forwarded` and `X-Forwarded-*` never determine identity, authorization, grants or URLs.
Operation Location values are relative. Protect request/response bodies and bearer values from
proxy logs; the example does not enable access logs. Apply external-client IP quotas at the
proxy, since VNote sees the proxy's socket address.

The example limits request bodies to 1 MiB, header read to 5 seconds, total request read to
10 seconds, and upstream response headers to 25 seconds (above the API's 20-second backend
budget). Review these finite budgets with your gateway administrator. Configure perimeter
connection/rate limits appropriate to the deployment; Caddy's standard distribution does not
supply a universal per-IP rate-limit directive. Local verified-certificate HTTPS smoke proves
the proxy contract, not Internet routing, firewall exposure or unattended certificate renewal.

## Common protocol rules

Base path: `/api/v1`. Bodies are UTF-8 JSON objects with `Content-Type: application/json`
(optional `charset=utf-8`). Content encodings/compression are not accepted. Unknown members,
duplicate members (including escaped spellings), duplicate/unknown query parameters, invalid
UTF-8, invalid paths and wrong scalar types are rejected. GET/DELETE accept no request body.
No trailing-slash aliases, encoded path segments, arbitrary file paths, weak/wildcard/list
preconditions, automatic redirects or silent numbered-name collision handling are provided.
IDs use the stable notebook/node identity, not a relative path. Names and display paths are
metadata only; never construct an OS path from them.
Indexed entries without a usable stable identity have `id=null`, `stableId=false` and no
address/content/mutation capabilities. Protected nodes remain omitted. A direct folder listing
is bounded to 10000 visible entries per snapshot; larger folders return 413 `listing_too_large`.
Creation requires an explicit extension matching the configured Markdown/Text file-type mapping;
the API does not add or change an extension for the requested format.
Names also reject Windows device aliases such as `CON.md`, `NUL.txt` and `COM1.md`
on every platform; those names must never reach a filesystem mutation.

Every API JSON/problem response has `Cache-Control: no-store`, `X-Content-Type-Options: nosniff`
and `X-Request-Id`. Problems use `application/problem+json` with `type`, `status`, stable `code`,
`title` and `requestId`; they do not expose core exceptions, note text or absolute paths.
Transport rejection before a request is parsed may close the connection instead of returning
an API problem. API 429/503 responses carry `Retry-After: 1`; this is not permission to replay a
mutation whose admission outcome is unknown. No automatic mutation retries are performed.

Default transport bounds: four fixed HTTP workers, 16 queued connections, 16 KiB headers,
4 KiB request target, 1 MiB body, 5-second socket read/write timeout, 2-second keepalive idle and
10 requests per connection. Owner-thread read/search work has a separate 20-second deadline.
Only one API search runs in the adapter, with at most two queued; additional searches get 429.
Stopping rejects admission with 503, cancels queued work and detaches HTTP waiters before joining
workers. Re-enabling creates a new server/backend instance while retaining the credential registry.

## Endpoints

| Method and path | Request and result |
| --- | --- |
| `GET /info` | Version, instance ID, capabilities and API-level effective limits; no host/install details |
| `GET /notebooks` | `{items:[...]}` containing granted, currently opened notebooks only |
| `GET /notebooks/{nb}/nodes` | Direct indexed children; optional `parentId`, `limit` (default 50, max 200), `cursor`; no parent means root |
| `GET /notebooks/{nb}/notes/{id}` | Metadata and `metadataVersion`, without automatically reading content |
| `GET /notebooks/{nb}/notes/{id}/content` | `view=saved` (default) or `live`, optional UTF-8 byte `offset`, `limit` (default 65536, max 1048576), `cursor` |
| `POST /search` | `{query,notebookIds?,searchContent?,limit?}`; literal plain-text query, `searchContent=true` by default, limit default/max 50; false searches names |
| `POST /notebooks/{nb}/notes` | `{name,format,parentId?}`, format `markdown` or `text`; creates an **empty** note; returns 202 |
| `PUT /notebooks/{nb}/notes/{id}/content` | `{text}` and saved `contentVersion` in If-Match; returns 202 |
| `POST /notebooks/{nb}/notes/{id}/append` | `{text}` and saved `contentVersion`; exact text, no separator; returns 202 |
| `PATCH /notebooks/{nb}/notes/{id}` | `{name}` only and `metadataVersion`; returns 202 |
| `DELETE /notebooks/{nb}/notes/{id}` | Strong `noteStateVersion`; strict recycle-bin move only; returns 202 |
| `GET /operations/{id}` | Owner-only pending/running/succeeded/failed/cancelled operation |

Search uses only saved ordinary Markdown/plain-text content and narrows notebook selection
**before execution**. If `notebookIds` is omitted it uses the captured grant; an explicit empty
array is invalid. Queries are nonempty, at most 1024 Unicode code units, and case-insensitive
literal text rather than regular expressions. Returned items include identities, paths and snippets, with
`truncated` indicating bounds. The VNote adapter additionally bounds search to 1000 candidate
files, 10000 indexed nodes and 64 MiB of candidate content. Protected files are never counted as
returned/searchable results. Shared GUI searches keep their own cancellation/result ownership.

### Saved, live and pagination

Saved reads are coherent persisted snapshots, not automatically saved editor buffers. Live
reads capture the current active writer without save/flush side effects and return `dirty`.
If no active writer is available, an explicit `view=saved` fallback is returned; it is not
mislabelled live. `text` is decoded Unicode; `encoding` and `lineEnding` describe the source.
Replacement preserves existing encoding, BOM and line endings; no conversion option exists.
Unsupported/non-round-trippable encoding is 422. The VNote adapter refuses snapshots exceeding
16 MiB rather than reading unbounded files into a single request.

Content pages contain `offset`, `returnedBytes`, `totalBytes`, `nextCursor`, `contentVersion`,
and, for saved snapshots only, `noteStateVersion`. Byte counts describe UTF-8 **text output**,
not JSON escaping, source-encoding bytes or Qt UTF-16 positions. Advance offset by returnedBytes
and send nextCursor unchanged (URL-encode it). Any offset above zero requires a cursor. Offsets
must fall at UTF-8 character boundaries; a limit too small for the next whole character returns
400. A null cursor marks the end. Listing cursors likewise carry a snapshot and position; pass
only the opaque cursor returned by the server. Changed snapshots return 412; restart pagination
instead of concatenating pages from different versions. Cursors are not credentials.

### Validators and conflict recovery

The HTTP `ETag` hashes the **exact JSON response/page**. It is not the full-note validator used
for mutations. Use these payload fields, wrapped in quotes in `If-Match`:

| Action | Payload validator |
| --- | --- |
| Replace/append | `contentVersion` from `view=saved` |
| Rename | `metadataVersion` from metadata |
| Trash | `noteStateVersion` from saved content |

Validators are opaque strings. The VNote adapter distinguishes `saved-`, `live-`, `metadata-`
and `state-` representations; clients must not synthesize or strip these prefixes. Missing
If-Match returns 428. Weak, wildcard, multiple or malformed If-Match returns 400. A valid but
stale/wrong-representation validator causes final operation failure 412. For an asynchronous
mutation the HTTP admission can still be 202: inspect the final operation's error.status/code.

On 412, fetch current metadata/content, reconcile the intended change, and submit a newly
considered operation with the fresh validator. On `unsaved_changes` (409), the owner must resolve
the dirty editor; the API neither overwrites nor silently saves it. Name conflicts are 409.
Readonly/unsupported/protected capabilities fail explicitly; they are never no-op success.

### Asynchronous mutations and partial outcomes

Example (use a client secret store; the placeholders below are not valid credentials):

```http
PUT /api/v1/notebooks/NOTEBOOK_ID/notes/NODE_ID/content HTTP/1.1
Host: 127.0.0.1:18765
Authorization: Bearer ID.SECRET
Content-Type: application/json
If-Match: "saved-OPAQUE_VALUE_FROM_SAVED_READ"

{"text":"replacement text"}
```

```http
HTTP/1.1 202 Accepted
Location: /api/v1/operations/INSTANCE.OPERATION
Content-Type: application/json

{"id":"INSTANCE.OPERATION","state":"pending","committed":false,"saved":false}
```

Poll Location using the same client token. Terminal success includes resource identity and
available fresh versions. `committed` and `saved` report actual domain outcomes, not merely
admission. A failed save may leave changed in-memory content (`committed=true,saved=false`).
An after-save hook failure may be a failed operation with `saved=true`; it does not erase the
successful persistence. If concurrent changes prevent returning matching new versions,
`resource.refreshRequired=true` directs a fresh read without falsifying committed/saved flags.

There are at most 16 active operations globally and four per token. Active operations are never
evicted. Up to 256 completed operations are retained for 15 minutes. Other tokens cannot see
an operation (404). Expired/evicted/previous-instance identifiers return 410, not a replay.
Revocation/expiry cancels work before commitment; already irreversible commitments may finish.
It cannot recall previously sent bytes or roll back a persisted save. Never auto-retry a
mutation after a dropped 202 response: the outcome is unknown and requires resource reconciliation.
There is no crash-persistent operation journal or exactly-once guarantee.

Create and initialize are deliberately **two** operations:

1. POST an empty note and poll until success; retain its new stable identity.
2. GET saved content to obtain the empty note's saved contentVersion.
3. PUT initial text with that If-Match and poll again.

If step 3 fails, the identifiable empty note from step 1 still exists. Report that partial result
honestly to the human/MCP consumer. Do not invent an atomic create-with-content operation or
blindly repeat create under another name.

## Error summary and unsupported operations

Status families: 400 invalid/malformed input; 401 token invalid; 403 scope/browser rejection;
404 absent/ungranted resource; 405 method; 409 name/dirty/busy conflict; 410 operation gone;
412 version/snapshot changed; 413 request/snapshot too large; 415 media/content encoding;
422 protected/type/text encoding unavailable; 428 required precondition; 429 finite admission
limit; 503 stopping/unavailable/deadline/save failure. Unexpected domain failures,
`after_hook_failed`, and `refresh_failed` use 500 with a safe generic title. Backend errors appear in the
operation's `error`, while syntax/policy/admission errors are immediate HTTP problems.

Not exposed: notebook creation or open-by-path, arbitrary files, binary/attachment export,
rendered HTML, folder mutation, tags/config JSON mutation, cross-notebook moves, protected-note
unlock, synchronization controls, shell/tasks/plugins, application quit or UI commands. The
existing single-instance IPC remains unrelated. Capability support is constrained by the real
notebook backend; strict trash never falls back to permanent deletion.
