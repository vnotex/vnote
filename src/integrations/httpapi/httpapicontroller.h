#ifndef VNOTEX_HTTPAPICONTROLLER_H
#define VNOTEX_HTTPAPICONTROLLER_H

#include <QDateTime>
#include <QJsonArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <memory>
#include <optional>

namespace vnotex {
class ServiceLocator;
class ApiCredentialStore;
class ApiAuth;
class ApiServer;
class VNoteApiBackend;

// Application-owned composition. HTTP and API modules know nothing about
// ServiceLocator, configuration, widgets, or VNote's service lifetimes.
class HttpApiController final : public QObject {
  Q_OBJECT
public:
  explicit HttpApiController(ServiceLocator &p_services, QObject *p_parent = nullptr);
  ~HttpApiController() override;

  void applicationReady();
  bool apply(QString *p_error = nullptr);
  void shutdown();
  bool isListening() const;
  QString statusText() const;
  QString endpoint() const;
  QJsonArray tokens() const;
  QJsonArray availableNotebooks() const;
  bool issueToken(const QString &p_label, const QSet<QString> &p_notebooks,
                  const QSet<QString> &p_scopes, const std::optional<QDateTime> &p_expires,
                  QString *p_secret, QString *p_error);
  bool revokeToken(const QString &p_id, QString *p_error);

signals:
  void stateChanged();
  void tokensChanged();

private:
  bool ensureCredentials(QString *p_error);
  void stopRuntime();
  bool fail(const QString &p_message, QString *p_error);

  ServiceLocator &m_services;
  std::unique_ptr<ApiCredentialStore> m_store;
  std::unique_ptr<ApiAuth> m_auth;
  std::unique_ptr<VNoteApiBackend> m_backend;
  std::unique_ptr<ApiServer> m_server;
  QString m_status;
  bool m_ready = false;
  bool m_stopping = false;
  bool m_credentialsLoaded = false;
};

} // namespace vnotex

#endif // VNOTEX_HTTPAPICONTROLLER_H
