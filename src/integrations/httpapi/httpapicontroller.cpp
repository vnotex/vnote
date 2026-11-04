#include "httpapicontroller.h"

#include <QDir>
#include <QJsonObject>
#include <QThread>
#include <QUrl>

#include "apicredentialstore.h"
#include "vnoteapibackend.h"
#include <api/apiauth.h>
#include <api/apiserver.h>
#include <core/configmgr2.h>
#include <core/httpapiconfig.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/notificationservice.h>
#include <core/sessionconfig.h>
#include <http/httptypes.h>
#include <vxcore/notebook_json_keys.h>

using namespace vnotex;

HttpApiController::HttpApiController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services), m_status(tr("Service disabled")) {
  // Loading an absent store never creates it. Keeping the registry independent
  // of the listener allows the owner to manage tokens while HTTP is disabled.
  QString error;
  if (!ensureCredentials(&error)) {
    m_status = error;
  }
}

HttpApiController::~HttpApiController() { shutdown(); }

bool HttpApiController::ensureCredentials(QString *p_error) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (m_credentialsLoaded) {
    return true;
  }
  auto *config = m_services.get<ConfigMgr2>();
  if (!config) {
    if (p_error) {
      *p_error = tr("HTTP API configuration is unavailable");
    }
    return false;
  }
  if (!m_store) {
#ifdef Q_OS_WIN
    const auto localPath = config->getUserConfigPath();
    if (localPath.isEmpty()) {
      if (p_error) {
        *p_error = tr("HTTP API configuration is unavailable");
      }
      return false;
    }
    m_store =
        std::make_unique<ApiCredentialStore>(QDir(localPath).filePath(QStringLiteral("http-api")));
#else
    // The local-data namespace is a cache directory on these platforms.
    m_store = std::make_unique<ApiCredentialStore>(
        config->getFileFromConfigFolder(QStringLiteral("http-api")));
#endif
    m_auth = std::make_unique<ApiAuth>(*m_store);
  }
  QString error;
  if (!m_auth->load(&error)) {
    if (p_error) {
      *p_error = tr("Could not load HTTP API credentials (%1)").arg(error);
    }
    return false;
  }
  m_credentialsLoaded = true;
  return true;
}

void HttpApiController::applicationReady() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (m_stopping || m_ready) {
    return;
  }
  m_ready = true;
  apply();
}

void HttpApiController::stopRuntime() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (m_server) {
    // ApiServer detaches response promises before it joins HTTP workers.
    m_server->stop();
    m_server.reset();
  }
  if (m_backend) {
    m_backend->stop();
    m_backend.reset();
  }
}

bool HttpApiController::fail(const QString &p_message, QString *p_error) {
  stopRuntime();
  m_status = p_message;
  if (p_error) {
    *p_error = p_message;
  }
  emit stateChanged();
  return false;
}

bool HttpApiController::apply(QString *p_error) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (p_error) {
    p_error->clear();
  }
  if (m_stopping) {
    return fail(tr("HTTP API is shutting down"), p_error);
  }
  auto *config = m_services.get<ConfigMgr2>();
  if (!config) {
    return fail(tr("HTTP API configuration is unavailable"), p_error);
  }
  const auto &settings = config->getSessionConfig().getHttpApiConfig();
  QString error;
  if (!settings.isValid(&error)) {
    return fail(tr("Invalid HTTP API settings: %1").arg(error), p_error);
  }
  if (!settings.isEnabled()) {
    stopRuntime();
    m_status = tr("Service disabled");
    emit stateChanged();
    return true;
  }
  if (!m_ready) {
    m_status = tr("Waiting for application startup");
    emit stateChanged();
    return true;
  }
  if (!ensureCredentials(&error)) {
    return fail(error, p_error);
  }
  stopRuntime();
  m_backend = std::make_unique<VNoteApiBackend>(m_services);
  m_server = std::make_unique<ApiServer>(*m_backend, *m_auth);
  HttpServerOptions options;
  options.address = settings.getAddress();
  options.port = static_cast<quint16>(settings.getPort());
  if (!m_server->start(options, &error)) {
    return fail(tr("Could not start HTTP API (%1)").arg(error), p_error);
  }
  m_status = tr("Listening on %1").arg(endpoint());
  emit stateChanged();
  return true;
}

void HttpApiController::shutdown() {
  Q_ASSERT(QThread::currentThread() == thread());
  if (m_stopping) {
    return;
  }
  m_stopping = true;
  stopRuntime();
  m_status = tr("Stopped");
  emit stateChanged();
}

bool HttpApiController::isListening() const { return m_server && m_server->isListening(); }

QString HttpApiController::statusText() const { return m_status; }

QString HttpApiController::endpoint() const {
  auto *config = m_services.get<ConfigMgr2>();
  if (!config) {
    return {};
  }
  const auto &settings = config->getSessionConfig().getHttpApiConfig();
  if (!settings.isValid()) {
    return {};
  }
  QUrl url;
  url.setScheme(QStringLiteral("http"));
  url.setHost(settings.getAddress());
  url.setPort(settings.getPort());
  url.setPath(QStringLiteral("/api/v1"));
  return url.toString(QUrl::FullyEncoded);
}

QJsonArray HttpApiController::tokens() const {
  return m_auth && m_credentialsLoaded ? m_auth->tokens() : QJsonArray();
}

QJsonArray HttpApiController::availableNotebooks() const {
  QJsonArray result;
  const auto *notebooks = m_services.get<NotebookCoreService>();
  if (!notebooks) {
    return result;
  }
  for (const auto &value : notebooks->listNotebooks()) {
    const auto entry = value.toObject();
    const auto id = entry.value(QLatin1String(vxcore::kJsonKeyId)).toString();
    if (!id.isEmpty()) {
      result.append(QJsonObject{
          {QStringLiteral("id"), id},
          {QStringLiteral("name"), entry.value(QLatin1String(vxcore::kJsonKeyName)).toString()}});
    }
  }
  return result;
}

bool HttpApiController::issueToken(const QString &p_label, const QSet<QString> &p_notebooks,
                                   const QSet<QString> &p_scopes,
                                   const std::optional<QDateTime> &p_expires, QString *p_secret,
                                   QString *p_error) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (p_secret) {
    p_secret->clear();
  }
  if (!p_secret || m_stopping) {
    if (p_error) {
      *p_error = tr("HTTP API is shutting down");
    }
    return false;
  }
  QString error;
  if (!ensureCredentials(&error)) {
    return fail(error, p_error);
  }
  QSet<QString> available;
  for (const auto &value : availableNotebooks()) {
    available.insert(value.toObject().value(QStringLiteral("id")).toString());
  }
  if (p_notebooks.isEmpty() || !(p_notebooks - available).isEmpty()) {
    if (p_error) {
      *p_error = tr("Select at least one available notebook");
    }
    return false;
  }
  ApiTokenGrant grant;
  grant.label = p_label;
  grant.notebookIds = p_notebooks;
  grant.scopes = p_scopes;
  grant.expiresAt = p_expires;
  ApiIssuedToken issued;
  const auto result = m_auth->issue(grant, &issued, &error);
  if (result != ApiAuth::IssueResult::Issued) {
    const auto message = tr("Could not issue an HTTP API token (%1)").arg(error);
    if (result == ApiAuth::IssueResult::Unavailable) {
      return fail(message, p_error);
    }
    if (p_error) {
      *p_error = message;
    }
    return false;
  }
  *p_secret = std::move(issued.secret);
  emit tokensChanged();
  return true;
}

bool HttpApiController::revokeToken(const QString &p_id, QString *p_error) {
  Q_ASSERT(QThread::currentThread() == thread());
  auto reportFailure = [this, p_error](const QString &p_message) {
    fail(p_message, p_error);
    if (auto *notifications = m_services.get<NotificationService>()) {
      NotificationMessage message;
      message.m_category = QStringLiteral("httpapi");
      message.m_title = tr("Token revocation failed");
      message.m_text = p_message;
      message.m_severity = NotificationMessage::Severity::Error;
      message.m_attention = NotificationMessage::Attention::Interrupt;
      message.m_duration = NotificationMessage::Duration::Persist;
      notifications->notify(std::move(message));
    }
    return false;
  };
  QString error;
  if (m_stopping || !ensureCredentials(&error)) {
    return reportFailure(error.isEmpty() ? tr("HTTP API is shutting down") : error);
  }
  const bool ok = m_auth->revoke(p_id, &error);
  emit tokensChanged();
  if (!ok) {
    return reportFailure(
        tr("Token access is blocked for this session, but revocation could not be saved "
           "(%1). The HTTP API has been stopped.")
            .arg(error));
  }
  return true;
}
