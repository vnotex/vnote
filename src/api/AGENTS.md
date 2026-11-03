# Standalone REST API

`vnote_api` links only `vnote_http` and Qt Core. It must remain usable with an independent
`IApiBackend` and `IApiCredentialStore`: no VNote services, configuration, GUI, filesystem,
Qt Network, or transport implementation headers belong here. Public DTOs own their values.
The consumer contract is [OpenAPI](../../docs/http-api.openapi.yaml) and the
[HTTP API guide](../../docs/http-api.md); update both when changing wire behavior.

## Security ownership

- `ApiAuth` alone validates the strict, bounded verifier registry. Storage calls are confined
  to its constructing/admin thread. HTTP workers only use locked in-memory records.
- `ApiAuth::issue` returns `Issued`, `Rejected` (invalid grant or quota), or `Unavailable`
  (admin-thread/storage failure). Rejected issuance must not interrupt existing clients;
  the integration stops on unavailable credentials without parsing human error strings.
- Token expiry is `std::optional<QDateTime>`: explicit `nullopt` means Never. The default grant
  retains an engaged invalid date so omitted expiry cannot silently become unlimited. Present
  dates must be valid/future at issue. Registry `expiresAt` remains required: explicit JSON null
  means Never, otherwise a valid ISO UTC date after `issuedAt`; missing/wrong/malformed values
  invalidate the registry. Never still obeys enabled state, scopes, grants and revocation and
  counts toward 64. Only finite dates expire; disabled records of either kind are still pruned.
- Issue persists before publishing the full bearer value once; revoke removes authority before
  persistence. A failed revoke save requires the controller to stop the listener. Never roll
  back in-memory revocation or put a token/verifier in diagnostics, URLs, normal preferences,
  responses, or notebook data. Keep OS system-generator entropy and fixed-size digest comparison.
- Authorization callbacks capture weak lifetime state and immutable policy fields, not raw auth
  pointers or request text. Recheck expiry, grant, scope, stop/cancellation at dispatch and at
  backend commitment. Filter before search, and do not publish a newly completed revoked read.
- Pre-body admission checks literal upstream Host, rejects Origin and Cookie, authenticates,
  validates route/headers/query, and checks resource grants/scopes. There is no CORS, browser
  authentication, token-management route, forwarded-header trust, or public bind.
- The backend owns resource existence, protected/type/read-only checks and stable-ID resolution.
  Unauthorized resource IDs return 404; valid permitted resources with insufficient scope return
  403. Never convert backend errors to raw filesystem/core messages.

## Threading and shutdown

`ApiServer::start/stop` and all backend execute/stop calls belong to the QObject owner thread.
Only fixed HTTP workers wait for read completion (20-second budget); never block the GUI waiting
for queued GUI work or pump a nested GUI loop. Mutation admission creates a token-owned bounded
operation before queuing the backend command. Active records are never evicted.

Stop closes shared admission, cancels operations, settles and detaches all read waiters, then
stops the backend and joins transport workers. Retained completions contain only shared response
state and cannot access a destroyed server/backend/auth. A stopped server/backend is replaced
on re-enable; the controller retains the auth registry. Committed saves may drain after API
promises are detached and must report actual committed/saved outcomes in domain state.

## Access auditing

`apiaudit.{h,cpp}` projects only trusted scalar metadata to the API-local `vnote.api.audit`
INFO category. The transport observer reports every observed HTTP attempt, including parser
rejections; API admission adds the authenticated principal ID and existing parser's static
route template without extra authentication or path parsing. Preserve pre-body policy order;
null identity/route before those stages is intentional. Reuse the transport ID in response,
problem body and ApiRequestContext; never trust a client correlation header.

ApiOperations stores safe audit context and monotonic timing, not request payloads. Normal
completion and pre-dispatch cancellation emit one terminal result. Stop of dispatched work
emits an unknown interruption; the first retained completion can emit one result without
reviving the registry or touching destroyed services. Generic negative post-stop results are
unknown (null committed/saved); only positive commitment proves those facts after detachment.
Duplicate callbacks and already-terminal running() calls are silent. 202 remains acceptance.

Never serialize raw method/target/query/headers, principal labels, resource JSON, error messages
or arbitrary backend codes into audit output. Fixed templates and token IDs are not secrets.
Use existing Qt logging, not application/core dependencies or a second filesystem sink. Sink
filters/retention/durability limits are documented in the consumer guide, not security guarantees.

## Wire invariants and tests

Reject unknown/duplicate JSON members, non-object or invalid UTF-8 bodies, duplicate query keys,
weak/list/wildcard preconditions, path forms outside stable IDs, and unsupported encodings.
The HTTP ETag hashes the exact JSON response; it is not a mutation precondition. Content,
metadata and note-state versions come from the backend and bind their full representation.
Offsets/limits are UTF-8 bytes; a continuation must carry the backend's snapshot cursor.

`test_apiauth` covers durable publication, corruption, expiry/revocation, bounds and scopes.
`test_restapi` exercises real loopback sockets against a stateful in-memory note backend.
`test_vnoteapibackend` and `test_httpapilifecycle` live alongside them but are integration-owned;
their links must not leak into `vnote_api`. Main integration verification also covers real saved
buffers, view refresh, strict trash, shutdown, and the HTTPS reverse-proxy example.
