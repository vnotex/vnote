# HTTP transport

`vnote_http` is a static, generic HTTP/1.1 transport. Its only production link
requirements are Qt Core, Threads and the platform socket library. Public headers
contain owned Qt values and standard callbacks, never httplib, API, notebook,
ServiceLocator, GUI or VTextEdit types. `tests/http/test_httpserver.cpp` exercises
real TCP sockets without a Qt event loop on the server owner thread.

## Dependency boundary

CMake fetches MIT cpp-httplib v0.64.0 at commit
`24e58ce5b25d55045af4cd6f463e83a8082fbe6b`. Upstream's CMake is deliberately not run:
no TLS/compression discovery, global dependency options, or optional SSL runtime.
The fetched `LICENSE` is exposed through target property
`VNOTE_HTTP_LICENSE_FILE`; application packaging owns installing it.

The private adapter subclasses `httplib::Server` and uses its parser, chunk
decoder, buffered stream, fixed thread pool and socket lifecycle helpers. Those
private upstream seams are version-specific: do not change the pin without
reviewing them and running the real-socket transport suite. In particular, do not
replace early admission with a normal post-body route or turn on default
`SO_REUSEPORT`, dynamic workers, decompression, HTTP upgrades, or permissive LF
parsing.

## Public contract

- `HttpRequest::target` is the untouched target including the query. Header field
  order, spelling and duplicates survive conversion; values have the upstream
  parser's whitespace normalization. Helpers compare names case-insensitively
  and `header()` returns the first occurrence. Trailers never become ordinary
  headers or alter the admission decision.
- `Admission` executes on a transport worker after syntactic/framing validation
  and before accumulating any request body. It sees an empty `body`. Returning
  any response closes the connection without waiting for or draining the body in
  the parser and never invokes `Handler`. A fixed upstream socket read buffer
  can already contain payload bytes received with the headers; this is not an
  application body allocation. A bounded 100 ms socket close drain may discard
  already-sent bytes to preserve delivery of an early error response.
- An admitted handler receives the complete bounded binary body, regardless of
  method or media type. There is no multipart/form transformation or route
  interpretation here. Response body copies happen only at the Qt/upstream
  ownership boundary. Exceptions produce a generic 500 without exception text.
- The transport owns response framing, connection lifetime and HEAD/no-body
  status semantics. Application-provided hop-by-hop/framing headers cannot
  override them; valid repeated end-to-end response fields are retained.
- Only canonical literal IPv4 127/8 or `::1` may bind. Defaults are
  `127.0.0.1:18765`; hostnames, wildcard/public binds, IPv4 shorthand, leading
  zeros and IPv6 scope IDs are rejected. Port zero exists solely for isolated
  transport tests. Production API configuration must demand a nonzero port.
  A bind conflict fails rather than changing the requested port.
- Host/Origin/token policy belongs to the API, not this module. The peer address
  is the socket peer; forwarded headers never rewrite it. TLS belongs to the
  same-host HTTPS reverse proxy, not the embedded listener.

## Access observation

`HttpServerOptions::accessObserver` receives owned, payload-free `HttpAccessRecord` values,
not raw requests/responses. It is optional and synchronous on workers (or the accept loop for
queue rejection); captures must survive stop/join and observers must not call lifecycle APIs.
The transport assigns one lazy UUID and an allowlisted method. Admission/Handler may annotate
only verified `HttpRequest::audit.principalId` and a static `routeTemplate`, not raw fields.
With an observer installed, post-routing replaces all X-Request-Id fields with that one UUID.

The pinned httplib logger fires BEFORE writing. It only records selected status/method; the
outer process_request scope emits after the write attempt, including exceptions and partial
input. `responseWritten` is write completion, not client receipt. A complete ignored leading
CRLF followed by EOF/timeout is idle; partial nonempty input, invalid framing and unfinished CR
are attempts. Reset worker-local pointers on every exit. Observer exceptions produce a fixed
safe warning and must not escape scope cleanup. Queue/stopping/socket-setup rejections have a
fixed enum reason and no fabricated HTTP identity/status. No API/logging dependency lives here.

## Finite resources and shutdown

Defaults: four fixed workers, sixteen queued accepted connections, 1 MiB decoded
body, 16 KiB aggregate initial headers plus chunk trailers, 4 KiB raw target,
100 initial header fields, five-second socket I/O phases, two-second keepalive,
ten requests per connection. Bounds are enforced at stream consumption before
unbounded header accumulation; cpp-httplib owns framing/chunk parsing and chunk
extension bounds. Duplicate Content-Length (even equal), any Content-Length plus
Transfer-Encoding, unsupported transfer/content coding, invalid framing,
non-origin targets and upgrades are rejected. No rejected or partial body is
reused as another request. Queue overflow closes an unparsed connection.

Options remain finite: body 1 byte–64 MiB, workers 1–64, queued connections
1–1024, I/O 1–300 seconds, keepalive 1–60 seconds/1–1000 requests. These are
transport safety ceilings, not an authorization policy or administrator promise
of adequate throughput. Initial headers and the body each have an aggregate
read deadline; response writing has its own deadline. A backend callback can
legitimately take longer than socket I/O, so no socket timer cancels application
work. Its owner must separately bound handler execution.

Start/stop/destruction belong to one owner thread, never a callback. All workers
exist before a successful start is published. `stop()` closes admission/listener,
interrupts active sockets, closes queued sockets as workers drain the fixed
queue, and joins the listener and workers without Qt events. It is idempotent and
a stopped server can start again. Socket shutdown/close share a registry lock so
no shutdown touches an OS-reused descriptor. Callback captures remain alive
until joining finishes; worker-local stream context never escapes its stack.
Accepted sockets are nonblocking. Response writes use short readiness waits and retry only
would-block/interrupted sends until the overall write deadline, so Windows shutdown does not
wait for a slow reader's full I/O timeout. Do not replace this with a blocking send and assume
`shutdown()` cancels it; `stopInterruptsSlowReader` exercises that failure mode.

**Before calling stop, the owner must settle/detach any backend promises that a
handler is waiting on.** Stop cannot cancel arbitrary user C++ code. In the
application this is ApiServer's responsibility before transport join and before
its auth/backend dependencies are destroyed.

## Verification ownership

Build targets: `vnote_http`, `test_httpserver`. Run the complete
`ctest -R "^test_httpserver$" --output-on-failure` suite on Windows, Linux and
macOS, including IPv6 loopback. Coverage includes fragmented requests, raw
values/duplicates, early rejection and Expect, binary/chunked bodies and limits,
malformed framing, aggregate trailers, slow header/body deadlines, blocked
response writes, overload, keepalive/HEAD, conflicts, restart and exception
containment. Delegated implementation does not itself constitute an executed
build or test result.
