#ifndef VNOTEX_HTTPTYPES_H
#define VNOTEX_HTTPTYPES_H

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVector>

namespace vnotex {

struct HttpRequest {
  QByteArray method;
  // The original request target, including its query; never percent-decoded.
  QByteArray target;
  QVector<QPair<QByteArray, QByteArray>> headers;
  QByteArray body;
  QString peerAddress;

  QByteArray header(const QByteArray &p_name) const {
    for (const auto &field : headers) {
      if (field.first.compare(p_name, Qt::CaseInsensitive) == 0) {
        return field.second;
      }
    }
    return {};
  }

  int headerCount(const QByteArray &p_name) const {
    int count = 0;
    for (const auto &field : headers) {
      if (field.first.compare(p_name, Qt::CaseInsensitive) == 0) {
        ++count;
      }
    }
    return count;
  }
};

struct HttpResponse {
  int status = 200;
  QByteArray contentType = "application/json";
  QVector<QPair<QByteArray, QByteArray>> headers;
  QByteArray body;
};

struct HttpServerOptions {
  QString address = QStringLiteral("127.0.0.1");
  quint16 port = 18765;
  qsizetype maxBodyBytes = 1024 * 1024;
  int workerCount = 4;
  int maxQueuedConnections = 16;
  int ioTimeoutSeconds = 5;
  int keepAliveSeconds = 2;
  int keepAliveRequests = 10;
};

} // namespace vnotex

#endif // VNOTEX_HTTPTYPES_H
