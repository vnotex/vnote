#include "httpserver.h"

// Optional upstream features must not leak in from application-wide flags.
#if defined(CPPHTTPLIB_OPENSSL_SUPPORT) || defined(CPPHTTPLIB_MBEDTLS_SUPPORT) ||                  \
    defined(CPPHTTPLIB_WOLFSSL_SUPPORT) || defined(CPPHTTPLIB_ZLIB_SUPPORT) ||                     \
    defined(CPPHTTPLIB_BROTLI_SUPPORT) || defined(CPPHTTPLIB_ZSTD_SUPPORT) ||                      \
    defined(CPPHTTPLIB_ALLOW_LF_AS_LINE_TERMINATOR)
#error vnote_http requires strict HTTP without TLS or compression
#endif
#include <httplib.h>

#include <QDebug>
#include <QUuid>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace vnotex {
namespace {

using Clock = std::chrono::steady_clock;
constexpr size_t MaxHeaderBytes = 16 * 1024;
constexpr size_t MaxTargetBytes = 4 * 1024;

HttpResponse failure(int p_status) {
  HttpResponse response;
  response.status = p_status;
  response.contentType = "text/plain; charset=utf-8";
  response.body = "HTTP request rejected.\n";
  return response;
}

bool isLoopback(const QString &p_address) {
  if (p_address == QStringLiteral("::1")) {
    return true;
  }
  const auto parts = p_address.toLatin1().split('.');
  if (parts.size() != 4 || parts[0] != "127") {
    return false;
  }
  for (const auto &part : parts) {
    bool ok = false;
    const auto value = part.toUInt(&ok);
    if (!ok || value > 255 || QByteArray::number(value) != part) {
      return false;
    }
  }
  return true;
}

// A byte budget around the upstream parser, NOT another HTTP parser. The
// library still parses the request line, fields, lengths, chunks and trailers.
// Counting bytes at consumption (including its buffered fast path) bounds
// allocation before the parser has accumulated a complete header section.
class BoundedStream final : public httplib::Stream {
public:
  BoundedStream(httplib::Stream &p_stream, int p_timeout, const std::atomic_bool &p_stopping)
      : m_stream(p_stream), m_timeout(p_timeout), m_stopping(p_stopping) {
    m_stream.set_read_interrupt([this] { return m_stopping.load(); });
  }

  ~BoundedStream() override { m_stream.set_read_interrupt({}); }

  bool is_readable() const override { return !failed() && m_stream.is_readable(); }
  bool wait_readable() const override { return !failed() && m_stream.wait_readable(); }
  bool wait_writable() const override { return m_stream.wait_writable(); }
  bool is_peer_alive() const override { return m_stream.is_peer_alive(); }
  socket_t socket() const override { return m_stream.socket(); }
  time_t duration() const override { return m_stream.duration(); }
  void get_remote_ip_and_port(std::string &p_ip, int &p_port) const override {
    m_stream.get_remote_ip_and_port(p_ip, p_port);
  }
  void get_local_ip_and_port(std::string &p_ip, int &p_port) const override {
    m_stream.get_local_ip_and_port(p_ip, p_port);
  }

  void set_max_timeout(time_t p_msec) override {
    m_readDeadline =
        p_msec > 0 ? Clock::now() + std::chrono::milliseconds(p_msec) : Clock::time_point::max();
    m_stream.set_max_timeout(p_msec);
  }

  void beginBody() {
    m_phase = Phase::Body;
    set_max_timeout(static_cast<time_t>(m_timeout) * 1000);
  }

  void beginTrailers() {
    // Initial fields and trailers share the same aggregate header budget.
    m_phase = Phase::Headers;
    m_lineBytes = 0;
    m_previous = 0;
  }

  void beginResponse() {
    set_max_timeout(0);
    m_writeDeadline = Clock::time_point::max();
  }

  bool failed() const { return m_invalid || m_stopping.load() || Clock::now() >= m_readDeadline; }

  bool tooLarge() const { return m_tooLarge; }

  // One complete leading CRLF followed by EOF/timeout is idle, not an access.
  bool hasRequestInput() const { return m_nonEmptyInput || m_invalid || m_previous == '\r'; }

  ssize_t read(char *p_data, size_t p_size) override {
    if (failed()) {
      return -1;
    }
    p_size = std::min(p_size, remaining());
    if (p_size == 0) {
      m_tooLarge = m_invalid = true;
      return -1;
    }
    const auto count = m_stream.read(p_data, p_size);
    if (count > 0) {
      account(p_data, static_cast<size_t>(count));
    }
    return m_invalid ? -1 : count;
  }

  const char *buffered_data(size_t &p_size) const override {
    p_size = 0;
    if (failed()) {
      return nullptr;
    }
    const auto data = m_stream.buffered_data(p_size);
    p_size = std::min(p_size, remaining());
    return p_size ? data : nullptr;
  }

  void consume_buffered(size_t p_size) override {
    size_t available = 0;
    const auto data = m_stream.buffered_data(available);
    account(data, p_size);
    m_stream.consume_buffered(p_size);
  }

  ssize_t write(const char *p_data, size_t p_size) override {
    if (m_writeDeadline == Clock::time_point::max()) {
      m_writeDeadline = Clock::now() + std::chrono::seconds(m_timeout);
    }
    while (!m_stopping.load()) {
      const auto left =
          std::chrono::duration_cast<std::chrono::microseconds>(m_writeDeadline - Clock::now())
              .count();
      if (left <= 0) {
        return -1;
      }
      // Winsock shutdown does not reliably interrupt a blocking send/select on
      // another thread. Accepted sockets are nonblocking; keep readiness waits
      // short without shortening the configured overall write deadline.
      const auto slice = std::min<std::chrono::microseconds::rep>(left, 100000);
      const auto ready = httplib::detail::select_write(socket(), 0, static_cast<time_t>(slice));
      if (m_stopping.load()) {
        return -1;
      }
      if (ready == 0) {
        continue;
      }
      if (ready < 0) {
#ifdef _WIN32
        const bool interrupted = WSAGetLastError() == WSAEINTR;
#else
        const bool interrupted = errno == EINTR;
#endif
        if (interrupted) {
          continue;
        }
        return -1;
      }
      const auto written = httplib::detail::send_socket(
          socket(), p_data, std::min(p_size, size_t(16384)), CPPHTTPLIB_SEND_FLAGS);
      if (written >= 0) {
        return written;
      }
#ifdef _WIN32
      const auto error = WSAGetLastError();
      const bool retryable = error == WSAEWOULDBLOCK || error == WSAEINTR;
#else
      const bool retryable = errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
      if (!retryable) {
        return -1;
      }
    }
    return -1;
  }

private:
  enum class Phase { RequestLine, Headers, Body };

  size_t remaining() const {
    if (m_phase == Phase::RequestLine) {
      return MaxTargetBytes + 32 - m_lineBytes;
    }
    return m_phase == Phase::Headers ? MaxHeaderBytes - m_headerBytes
                                     : (std::numeric_limits<size_t>::max)();
  }

  void account(const char *p_data, size_t p_size) {
    for (size_t i = 0; i < p_size && m_phase != Phase::Body; ++i) {
      const char ch = p_data[i];
      m_nonEmptyInput = m_nonEmptyInput || (ch != '\r' && ch != '\n');
      if (remaining() == 0) {
        m_tooLarge = m_invalid = true;
        return;
      }
      if (ch == '\0' || (ch == '\n' && m_previous != '\r') || (m_previous == '\r' && ch != '\n')) {
        m_invalid = true;
        return;
      }
      ++m_lineBytes;
      if (m_phase == Phase::Headers) {
        ++m_headerBytes;
      }
      if (ch == '\n') {
        if (m_phase == Phase::RequestLine) {
          m_phase = Phase::Headers;
        } else if (m_lineBytes == 2) {
          m_phase = Phase::Body;
        }
        m_lineBytes = 0;
      }
      m_previous = ch;
    }
  }

  httplib::Stream &m_stream;
  int m_timeout;
  const std::atomic_bool &m_stopping;
  Phase m_phase = Phase::RequestLine;
  size_t m_lineBytes = 0;
  size_t m_headerBytes = 0;
  char m_previous = 0;
  bool m_nonEmptyInput = false;
  bool m_invalid = false;
  bool m_tooLarge = false;
  Clock::time_point m_readDeadline = Clock::time_point::max();
  Clock::time_point m_writeDeadline = Clock::time_point::max();
};

// Each upstream callback executes synchronously inside process_request() on
// this worker. The scoped pointer never escapes the callback or its stack.
thread_local BoundedStream *currentStream = nullptr;
thread_local HttpAccessRecord *currentAccess = nullptr;

void ensureRequestId() {
  if (currentAccess && currentAccess->context.requestId.isEmpty()) {
    currentAccess->context.requestId = QUuid::createUuid().toString(QUuid::Id128);
  }
}

QByteArray auditMethod(const std::string &p_method) {
  static const QByteArray methods[] = {
      QByteArrayLiteral("GET"),     QByteArrayLiteral("HEAD"),  QByteArrayLiteral("POST"),
      QByteArrayLiteral("PUT"),     QByteArrayLiteral("PATCH"), QByteArrayLiteral("DELETE"),
      QByteArrayLiteral("OPTIONS"), QByteArrayLiteral("TRACE"), QByteArrayLiteral("CONNECT")};
  for (const auto &method : methods) {
    if (p_method == method.constData()) {
      return method;
    }
  }
  return p_method.empty() ? QByteArray() : QByteArrayLiteral("OTHER");
}

void setResponse(httplib::Response &p_target, HttpResponse p_response, bool p_close = false) {
  if (p_response.status < 200 || p_response.status > 599) {
    p_response = failure(500);
    p_close = true;
  }
  p_target.status = p_response.status;
  p_target.headers.clear();
  for (const auto &field : p_response.headers) {
    // The transport owns response framing; callers cannot inject a second
    // length, hop-by-hop protocol, or switch the connection into an upgrade.
    if (field.first.compare("Content-Length", Qt::CaseInsensitive) == 0 ||
        field.first.compare("Transfer-Encoding", Qt::CaseInsensitive) == 0 ||
        field.first.compare("Connection", Qt::CaseInsensitive) == 0 ||
        field.first.compare("Keep-Alive", Qt::CaseInsensitive) == 0 ||
        field.first.compare("Upgrade", Qt::CaseInsensitive) == 0 ||
        field.first.compare("Trailer", Qt::CaseInsensitive) == 0 ||
        field.first.compare("Content-Type", Qt::CaseInsensitive) == 0) {
      continue;
    }
    p_target.set_header(field.first.toStdString(), field.second.toStdString());
  }
  p_target.set_header("Content-Type", p_response.contentType.toStdString());
  p_target.body.assign(p_response.body.constData(), static_cast<size_t>(p_response.body.size()));
  if (p_close) {
    p_target.set_header("Connection", "close");
  }
  if (currentStream) {
    currentStream->beginResponse();
  }
}

class Transport final : public httplib::Server {
public:
  Transport(HttpServerOptions p_options, HttpServer::Admission p_admission,
            HttpServer::Handler p_handler)
      : m_options(std::move(p_options)), m_admission(std::move(p_admission)),
        m_handler(std::move(p_handler)) {
    set_address_family(m_options.address == QStringLiteral("::1") ? AF_INET6 : AF_INET);
    set_ipv6_v6only(true);
    // Upstream's default SO_REUSEPORT permits two listeners on the same port.
    // Fail closed on conflicts instead; on Windows require exclusive ownership.
    set_socket_options([](socket_t p_socket) {
#ifdef _WIN32
      httplib::set_socket_opt(p_socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 1);
#else
      httplib::set_socket_opt(p_socket, SOL_SOCKET, SO_REUSEADDR, 1);
#endif
    });
    set_read_timeout(m_options.ioTimeoutSeconds);
    set_write_timeout(m_options.ioTimeoutSeconds);
    set_keep_alive_timeout(m_options.keepAliveSeconds);
    set_keep_alive_max_count(static_cast<size_t>(m_options.keepAliveRequests));
    set_payload_max_length(static_cast<size_t>(m_options.maxBodyBytes));
    set_compress(false);
    set_pre_routing_handler(
        [this](const httplib::Request &p_request, httplib::Response &p_response) {
          // process_request owns a mutable Request; upstream exposes its admission
          // callback as const. Only its body-consumption bookkeeping and trailer
          // parser state are changed here, never its original method/target/fields.
          handle(const_cast<httplib::Request &>(p_request), p_response);
          return HandlerResponse::Handled;
        });
    set_exception_handler([](const httplib::Request &, httplib::Response &p_response,
                             std::exception_ptr) { setResponse(p_response, failure(500), true); });
    set_error_handler([](const httplib::Request &, httplib::Response &p_response) {
      if (p_response.body.empty()) {
        setResponse(p_response, failure(p_response.status), true);
      }
    });
    if (m_options.accessObserver) {
      set_post_routing_handler([](const httplib::Request &, httplib::Response &p_response) {
        ensureRequestId();
        if (currentAccess) {
          // set_header emplaces: replace all values, including application-provided ones.
          p_response.headers.erase("X-Request-Id");
          p_response.set_header("X-Request-Id", currentAccess->context.requestId.toStdString());
        }
      });
      set_logger([](const httplib::Request &p_request, const httplib::Response &p_response) {
        if (currentAccess) {
          currentAccess->status = p_response.status;
          if (currentAccess->context.method.isEmpty()) {
            currentAccess->context.method = auditMethod(p_request.method);
          }
        }
      });
      set_error_logger([this](const httplib::Error &p_error, const httplib::Request *) {
        if (p_error == httplib::Error::ResourceExhaustion) {
          rejectConnection(HttpAccessRecord::Rejection::QueueFull);
        }
      });
    }
    m_sockets.reserve(static_cast<size_t>(m_options.workerCount));
    new_task_queue = [this] { return m_pool.release(); };
    // Create all workers before binding or publishing listener readiness.
    // Upstream joins partially-created workers if thread creation throws.
    m_pool = std::make_unique<httplib::ThreadPool>(
        static_cast<size_t>(m_options.workerCount), static_cast<size_t>(m_options.workerCount),
        static_cast<size_t>(m_options.maxQueuedConnections));
  }

  ~Transport() override {
    // A bind/start failure never transfers the pool to the accept loop.
    if (m_pool) {
      m_pool->shutdown();
    }
  }

  void interrupt() {
    m_stopping.store(true);
    httplib::Server::stop();
    std::lock_guard<std::mutex> lock(m_socketsMutex);
    for (const auto socket : m_sockets) {
      httplib::detail::shutdown_socket(socket);
    }
  }

private:
  void observe(const HttpAccessRecord &p_record) const noexcept {
    if (!m_options.accessObserver) {
      return;
    }
    try {
      m_options.accessObserver(p_record);
    } catch (...) {
      qWarning("HTTP access observer failed");
    }
  }

  void rejectConnection(HttpAccessRecord::Rejection p_reason) const {
    if (m_options.accessObserver) {
      HttpAccessRecord record;
      record.rejection = p_reason;
      observe(record);
    }
  }

  bool process_and_close_socket(socket_t p_socket) override {
    {
      std::lock_guard<std::mutex> lock(m_socketsMutex);
      if (m_stopping.load()) {
        rejectConnection(HttpAccessRecord::Rejection::Stopping);
        httplib::detail::shutdown_socket(p_socket);
        httplib::detail::close_socket(p_socket);
        return false;
      }
#ifdef _WIN32
      u_long nonblocking = 1;
      const bool configured = ioctlsocket(p_socket, FIONBIO, &nonblocking) == 0;
#else
      const auto flags = fcntl(p_socket, F_GETFL, 0);
      const bool configured = flags >= 0 && fcntl(p_socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
      if (!configured) {
        rejectConnection(HttpAccessRecord::Rejection::SocketSetupFailed);
        httplib::detail::close_socket(p_socket);
        return false;
      }
      m_sockets.push_back(p_socket);
    }
    std::string remote, local;
    int remotePort = 0, localPort = 0;
    httplib::detail::get_remote_ip_and_port(p_socket, remote, remotePort);
    httplib::detail::get_local_ip_and_port(p_socket, local, localPort);
    const bool result = serve_guarded([&] {
      return httplib::detail::process_server_socket(
          svr_sock_, p_socket, keep_alive_max_count_, keep_alive_timeout_sec_, read_timeout_sec_,
          read_timeout_usec_, write_timeout_sec_, write_timeout_usec_,
          [&](httplib::Stream &p_stream, bool p_close, bool &p_closed) {
            BoundedStream bounded(p_stream, m_options.ioTimeoutSeconds, m_stopping);
            HttpAccessRecord access;
            const auto started = m_options.accessObserver ? Clock::now() : Clock::time_point{};
            currentStream = &bounded;
            currentAccess = m_options.accessObserver ? &access : nullptr;
            const auto reset = httplib::detail::scope_exit([&] {
              if (currentAccess && (bounded.hasRequestInput() || access.status != 0)) {
                ensureRequestId();
                access.durationMs =
                    std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started)
                        .count();
                observe(access);
              }
              currentAccess = nullptr;
              currentStream = nullptr;
            });
            access.responseWritten =
                process_request(bounded, remote, remotePort, local, localPort, p_close, p_closed,
                                [](httplib::Request &p_request) {
                                  // Byte ranges are application semantics, not an
                                  // invitation to slice a generic handler's output.
                                  p_request.ranges.clear();
                                });
            return access.responseWritten;
          });
    });
    {
      // Shutdown and close share this lock: stop must never shutdown a descriptor
      // after close has allowed the OS to reuse it for another connection.
      std::lock_guard<std::mutex> lock(m_socketsMutex);
      if (m_stopping.load()) {
        httplib::detail::shutdown_socket(p_socket);
        httplib::detail::close_socket(p_socket);
      } else {
        httplib::detail::drain_and_close_socket(p_socket); // upstream's 100 ms / 1 MiB cap
      }
      m_sockets.erase(std::find(m_sockets.begin(), m_sockets.end(), p_socket));
    }
    return result;
  }

  void handle(httplib::Request &p_request, httplib::Response &p_response) {
    auto reject = [&](int p_status) { setResponse(p_response, failure(p_status), true); };
    if (m_stopping.load()) {
      reject(503);
      return;
    }
    if (!currentStream || currentStream->failed()) {
      reject(currentStream && currentStream->tooLarge() ? 431 : 400);
      return;
    }
    if (p_request.version != "HTTP/1.1") {
      reject(505);
      return;
    }
    if (p_request.target.empty() || p_request.target.front() != '/' ||
        p_request.target.size() > MaxTargetBytes || p_request.method == "CONNECT" ||
        p_request.method == "PRI" || p_request.has_header("Upgrade") ||
        httplib::detail::has_header_token(p_request.headers, "Connection", "upgrade") ||
        p_request.get_header_value_count("Content-Length") > 1 ||
        (p_request.has_header("Content-Length") && p_request.has_header("Transfer-Encoding"))) {
      reject(400);
      return;
    }
    if (p_request.has_header("Content-Encoding")) {
      reject(415);
      return;
    }
    if (p_request.has_header("Expect") &&
        (p_request.get_header_value_count("Expect") != 1 ||
         !httplib::detail::case_ignore::equal(p_request.get_header_value("Expect"),
                                              "100-continue"))) {
      reject(417);
      return;
    }

    HttpRequest request;
    if (currentAccess) {
      ensureRequestId();
      currentAccess->context.method = auditMethod(p_request.method);
      request.audit = currentAccess->context;
    }
    const auto retainAudit = httplib::detail::scope_exit([&] {
      if (currentAccess) {
        currentAccess->context.principalId = std::move(request.audit.principalId);
        currentAccess->context.routeTemplate = std::move(request.audit.routeTemplate);
      }
    });
    request.method = QByteArray::fromStdString(p_request.method);
    request.target = QByteArray::fromStdString(p_request.target);
    request.peerAddress = QString::fromStdString(p_request.remote_addr);
    request.headers.reserve(static_cast<qsizetype>(p_request.headers.size()));
    for (const auto &field : p_request.headers) {
      request.headers.append(
          {QByteArray::fromStdString(field.first), QByteArray::fromStdString(field.second)});
    }
    if (m_admission) {
      if (auto response = m_admission(request)) {
        setResponse(p_response, std::move(*response), true);
        return;
      }
    }
    if (m_stopping.load()) {
      reject(503);
      return;
    }
    const auto length = p_request.get_header_value_u64("Content-Length");
    if (length > static_cast<size_t>(m_options.maxBodyBytes)) {
      reject(413);
      return;
    }
    if (p_request.has_header("Content-Length") || p_request.has_header("Transfer-Encoding")) {
      currentStream->beginBody();
      if (p_request.expect_100_continue_pending_) {
        p_request.expect_100_continue_pending_ = false;
        if (!httplib::detail::write_data(*currentStream, "HTTP/1.1 100 Continue\r\n\r\n", 25)) {
          reject(400);
          return;
        }
      }
      p_request.body_read_started_ = true;
      const int status = readBody(p_request, request.body, length);
      if (status != 0) {
        reject(status);
        return;
      }
      p_request.body_consumed_ = true;
    }
    currentStream->beginResponse();
    if (m_stopping.load()) {
      reject(503);
      return;
    }
    setResponse(p_response, m_handler(request));
  }

  int readBody(httplib::Request &p_request, QByteArray &p_body, size_t p_length) {
    const auto limit = static_cast<size_t>(m_options.maxBodyBytes);
    if (!p_request.has_header("Transfer-Encoding")) {
      const auto result = httplib::detail::read_content_with_length(
          *currentStream, p_length, nullptr,
          [&](const char *p_data, size_t p_size, size_t, size_t) {
            p_body.append(p_data, static_cast<qsizetype>(p_size));
            return true;
          },
          limit);
      return result == httplib::detail::ReadContentResult::Success ? 0 : 400;
    }
    httplib::detail::ChunkedDecoder decoder(*currentStream);
    char buffer[16384];
    for (;;) {
      size_t offset = 0, total = 0;
      const auto count = decoder.read_payload(buffer, sizeof(buffer), offset, total);
      if (count < 0) {
        return 400;
      }
      if (count == 0) {
        currentStream->beginTrailers();
        if (!decoder.parse_trailers_into(p_request.trailers, p_request.headers) ||
            currentStream->failed()) {
          return currentStream->tooLarge() ? 431 : 400;
        }
        return 0;
      }
      // Check the declared chunk as soon as the decoder exposes it as well as
      // cumulative decoded bytes; no over-limit byte is appended to the body.
      const auto used = static_cast<size_t>(p_body.size());
      if (static_cast<size_t>(count) > limit - used || total - offset > limit - used) {
        return 413;
      }
      p_body.append(buffer, static_cast<qsizetype>(count));
    }
  }

  HttpServerOptions m_options;
  HttpServer::Admission m_admission;
  HttpServer::Handler m_handler;
  std::atomic_bool m_stopping{false};
  std::mutex m_socketsMutex;
  std::vector<socket_t> m_sockets;
  std::unique_ptr<httplib::ThreadPool> m_pool;
};

} // namespace

class HttpServer::Impl {
public:
  std::unique_ptr<Transport> server;
  std::thread listener;
  std::atomic<quint16> boundPort{0};
  std::mutex startupMutex;
  std::condition_variable startup;
  bool ready = false;
  bool finished = false;
};

HttpServer::HttpServer() : m_impl(std::make_unique<Impl>()) {}
HttpServer::~HttpServer() { stop(); }

bool HttpServer::start(const HttpServerOptions &p_options, Admission p_admission, Handler p_handler,
                       QString *p_error) {
  if (p_error) {
    p_error->clear();
  }
  auto fail = [&](const char *p_message) {
    if (p_error) {
      *p_error = QString::fromLatin1(p_message);
    }
    return false;
  };
  if (m_impl->server) {
    return fail("HTTP server is already started.");
  }
  if (!isLoopback(p_options.address) || !p_handler || p_options.maxBodyBytes <= 0 ||
      p_options.maxBodyBytes > 64 * 1024 * 1024 || p_options.workerCount < 1 ||
      p_options.workerCount > 64 || p_options.maxQueuedConnections < 1 ||
      p_options.maxQueuedConnections > 1024 || p_options.ioTimeoutSeconds < 1 ||
      p_options.ioTimeoutSeconds > 300 || p_options.keepAliveSeconds < 1 ||
      p_options.keepAliveSeconds > 60 || p_options.keepAliveRequests < 1 ||
      p_options.keepAliveRequests > 1000) {
    return fail("Invalid HTTP listener options.");
  }
  try {
    m_impl->server =
        std::make_unique<Transport>(p_options, std::move(p_admission), std::move(p_handler));
    auto &server = *m_impl->server;
    int boundPort = p_options.port;
    const auto address = p_options.address.toStdString();
    if (p_options.port == 0) {
      boundPort = server.bind_to_any_port(address);
    } else if (!server.bind_to_port(address, p_options.port)) {
      boundPort = -1;
    }
    if (boundPort <= 0) {
      stop();
      return fail("Could not bind the HTTP listener.");
    }
    m_impl->ready = m_impl->finished = false;
    server.set_start_handler([this] {
      std::lock_guard<std::mutex> lock(m_impl->startupMutex);
      m_impl->ready = true;
      m_impl->startup.notify_one();
    });
    m_impl->listener = std::thread([this] {
      try {
        m_impl->server->listen_after_bind();
      } catch (...) {
        m_impl->server->interrupt();
      }
      std::lock_guard<std::mutex> lock(m_impl->startupMutex);
      m_impl->finished = true;
      m_impl->startup.notify_one();
    });
    {
      std::unique_lock<std::mutex> lock(m_impl->startupMutex);
      m_impl->startup.wait(lock, [this] { return m_impl->ready || m_impl->finished; });
      if (m_impl->finished) {
        lock.unlock();
        stop();
        return fail("Could not start the HTTP listener.");
      }
    }
    m_impl->boundPort.store(static_cast<quint16>(boundPort));
    return true;
  } catch (...) {
    stop();
    return fail("Could not start the HTTP listener.");
  }
}

void HttpServer::stop() {
  if (m_impl->server) {
    m_impl->server->interrupt();
  }
  if (m_impl->listener.joinable()) {
    m_impl->listener.join();
  }
  m_impl->server.reset();
  m_impl->boundPort.store(0);
}

bool HttpServer::isListening() const { return m_impl->server && m_impl->server->is_running(); }

quint16 HttpServer::port() const { return m_impl->boundPort.load(); }

} // namespace vnotex
