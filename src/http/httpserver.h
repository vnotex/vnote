#ifndef VNOTEX_HTTPSERVER_H
#define VNOTEX_HTTPSERVER_H

#include "httptypes.h"

#include <functional>
#include <memory>
#include <optional>

namespace vnotex {

// Lifecycle calls belong to one owner thread, never a transport callback.
// Callbacks run on fixed workers. All captured state must survive stop().
class HttpServer final {
public:
  using Admission = std::function<std::optional<HttpResponse>(HttpRequest &)>;
  using Handler = std::function<HttpResponse(HttpRequest &)>;

  HttpServer();
  ~HttpServer();
  HttpServer(const HttpServer &) = delete;
  HttpServer &operator=(const HttpServer &) = delete;

  // Port zero is available for isolated tests; production callers must reject it.
  // Callbacks may annotate audit identity/route, never modify raw request fields.
  // Admission sees headers and an empty body. Returning a response closes the
  // connection without consuming the body or calling Handler.
  bool start(const HttpServerOptions &p_options, Admission p_admission, Handler p_handler,
             QString *p_error);
  // Settle/detach promises awaited by callbacks BEFORE calling stop(). It joins
  // workers and interrupts sockets synchronously, without processing Qt events.
  void stop();
  bool isListening() const;
  quint16 port() const;

private:
  class Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace vnotex

#endif // VNOTEX_HTTPSERVER_H
