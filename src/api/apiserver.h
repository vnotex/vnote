#ifndef VNOTEX_APISERVER_H
#define VNOTEX_APISERVER_H

#include <QObject>
#include <http/httptypes.h>
#include <memory>

namespace vnotex {
class IApiBackend;
class ApiAuth;
class ApiServer : public QObject {
  Q_OBJECT
public:
  ApiServer(IApiBackend &p_backend, ApiAuth &p_auth, QObject *p_parent = nullptr);
  ~ApiServer() override;
  bool start(const HttpServerOptions &p_options, QString *p_error);
  // Owner-thread only: cancels/detaches response waiters before transport joins.
  void stop();
  bool isListening() const;
  QString instanceId() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace vnotex
#endif
