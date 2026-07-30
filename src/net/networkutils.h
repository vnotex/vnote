#ifndef VNOTEX_NET_NETWORKUTILS_H
#define VNOTEX_NET_NETWORKUTILS_H

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QPair>
#include <QUrl>
#include <QVector>

namespace vnotex {

// Thin wrappers around QNetworkAccessManager.
//
// This is a vendored, Qt-Core/Network-only port of the former
// <vtextedit/networkutils.h>. It lives here so that core_services does not have
// to link the VTextEdit shared library (which propagates Qt::Widgets PUBLIC).
class NetworkUtils {
public:
  NetworkUtils() = delete;

  static QNetworkRequest networkRequest(const QUrl &p_url);

  static QString networkErrorStr(QNetworkReply::NetworkError p_err);
};

struct NetworkReply {
  QString errorStr() const;

  QNetworkReply::NetworkError m_error = QNetworkReply::HostNotFoundError;

  QByteArray m_data;
};

class NetworkAccess : public QObject {
  Q_OBJECT
public:
  typedef QVector<QPair<QByteArray, QByteArray>> RawHeaderPairs;

  explicit NetworkAccess(QObject *p_parent = nullptr);

  void requestAsync(const QUrl &p_url);

  static NetworkReply request(const QUrl &p_url);

  static NetworkReply request(const QUrl &p_url, const RawHeaderPairs &p_rawHeader);

  static NetworkReply put(const QUrl &p_url, const RawHeaderPairs &p_rawHeader,
                          const QByteArray &p_data);

  static NetworkReply post(const QUrl &p_url, const RawHeaderPairs &p_rawHeader,
                           const QByteArray &p_data);

  static NetworkReply deleteResource(const QUrl &p_url, const RawHeaderPairs &p_rawHeader,
                                     const QByteArray &p_data);

signals:
  // Url is the original url of the request.
  void requestFinished(const NetworkReply &p_reply, const QString &p_url);

private:
  static void handleReply(QNetworkReply *p_reply, NetworkReply &p_myReply);

  static NetworkReply sendRequest(const QUrl &p_url, const RawHeaderPairs &p_rawHeader,
                                  const QByteArray &p_action, const QByteArray &p_data);

  QNetworkAccessManager m_netAccessMgr;
};

} // namespace vnotex

#endif // VNOTEX_NET_NETWORKUTILS_H
