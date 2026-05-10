#include "giteeprovider.h"

#include <QByteArray>
#include <QDebug>
#include <QFileInfo>

#include <utils/pathutils.h>
#include <utils/utils.h>
#include <utils/webutils.h>

using namespace vnotex;

const QString GiteeProvider::c_apiUrl = "https://gitee.com/api/v5";

GiteeProvider::GiteeProvider(QObject *p_parent) : IImageHostProvider(p_parent) {}

QString GiteeProvider::typeId() const { return QStringLiteral("gitee"); }

QString GiteeProvider::displayName() const { return tr("Gitee Repository"); }

bool GiteeProvider::ready() const {
  return !m_personalAccessToken.isEmpty() && !m_userName.isEmpty() && !m_repoName.isEmpty();
}

bool GiteeProvider::supportsDelete() const { return true; }

QJsonObject GiteeProvider::getConfig() const {
  QJsonObject obj;
  obj[QStringLiteral("personalAccessToken")] = m_personalAccessToken;
  obj[QStringLiteral("userName")] = m_userName;
  obj[QStringLiteral("repositoryName")] = m_repoName;
  return obj;
}

QHash<QString, ConfigFieldHint> GiteeProvider::getConfigFieldHints() const
{
  return {
    {QStringLiteral("personalAccessToken"),
     {QStringLiteral("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"),
      tr("Gitee Personal Access Token with 'projects' scope. Generate at Gitee → Settings → Security Settings → Personal Access Tokens.")}},
    {QStringLiteral("userName"),
     {QStringLiteral("username"),
      tr("Your Gitee username.")}},
    {QStringLiteral("repositoryName"),
     {QStringLiteral("my-image-repo"),
      tr("Name of the Gitee repository to store images. Must already exist.")}},
  };
}

void GiteeProvider::setConfig(const QJsonObject &p_config) {
  m_personalAccessToken = p_config[QStringLiteral("personalAccessToken")].toString();
  m_userName = p_config[QStringLiteral("userName")].toString();
  m_repoName = p_config[QStringLiteral("repositoryName")].toString();

  // Do not assume the default branch.
  m_imageUrlPrefix = QStringLiteral("https://gitee.com/%1/%2/raw/").arg(m_userName, m_repoName);
}

bool GiteeProvider::testConfig(const QJsonObject &p_config, QString &p_msg) {
  p_msg.clear();

  const auto token = p_config[QStringLiteral("personalAccessToken")].toString();
  const auto userName = p_config[QStringLiteral("userName")].toString();
  const auto repoName = p_config[QStringLiteral("repositoryName")].toString();

  if (token.isEmpty() || userName.isEmpty() || repoName.isEmpty()) {
    p_msg = tr("PersonalAccessToken/UserName/RepositoryName should not be empty.");
    return false;
  }

  auto reply = getRepoInfo(token, userName, repoName);
  p_msg = QString::fromUtf8(reply.m_data);

  if (reply.m_error != QNetworkReply::NoError) {
    return false;
  }

  auto replyObj = Utils::fromJsonString(reply.m_data);
  if (replyObj.isEmpty()) {
    return false;
  }

  if (replyObj[QStringLiteral("private")].toBool()) {
    p_msg = tr("Private repository is not supported.");
    return false;
  }

  return true;
}

vte::NetworkAccess::RawHeaderPairs GiteeProvider::prepareCommonHeaders() {
  vte::NetworkAccess::RawHeaderPairs rawHeader;
  rawHeader.push_back(
      qMakePair(QByteArray("Content-Type"), QByteArray("application/json;charset=UTF-8")));
  return rawHeader;
}

QString GiteeProvider::addAccessToken(const QString &p_token, QString p_url) {
  if (p_url.contains(QLatin1Char('?'))) {
    p_url += QStringLiteral("&access_token=%1").arg(p_token);
  } else {
    p_url += QStringLiteral("?access_token=%1").arg(p_token);
  }
  return p_url;
}

vte::NetworkReply GiteeProvider::getRepoInfo(const QString &p_token, const QString &p_userName,
                                             const QString &p_repoName) const {
  auto rawHeader = prepareCommonHeaders();
  auto urlStr = QStringLiteral("%1/repos/%2/%3").arg(c_apiUrl, p_userName, p_repoName);
  auto reply = vte::NetworkAccess::request(QUrl(addAccessToken(p_token, urlStr)), rawHeader);
  return reply;
}

QString GiteeProvider::fetchResourcePath(const QString &p_prefix, const QString &p_url) {
  auto resourcePath = p_url.mid(p_prefix.size());
  // Skip the branch name.
  resourcePath = resourcePath.mid(resourcePath.indexOf(QLatin1Char('/')) + 1);
  resourcePath = WebUtils::purifyUrl(resourcePath);
  return resourcePath;
}

bool GiteeProvider::isEmptyResponse(const QByteArray &p_data) { return p_data == QByteArray("[]"); }

ImageUploadResult GiteeProvider::upload(const QByteArray &p_data, const QString &p_path) {
  ImageUploadResult result;
  result.fileName = QFileInfo(p_path).fileName();

  if (p_path.isEmpty()) {
    result.errorMessage = tr("Failed to create image with empty path.");
    return result;
  }

  if (!ready()) {
    result.errorMessage = tr("Invalid Gitee image host configuration.");
    return result;
  }

  auto rawHeader = prepareCommonHeaders();
  const auto urlStr =
      QStringLiteral("%1/repos/%2/%3/contents/%4").arg(c_apiUrl, m_userName, m_repoName, p_path);

  // Check if @p_path already exists.
  auto reply =
      vte::NetworkAccess::request(QUrl(addAccessToken(m_personalAccessToken, urlStr)), rawHeader);
  if (reply.m_error == QNetworkReply::NoError) {
    if (!isEmptyResponse(reply.m_data)) {
      result.errorMessage = tr("The resource already exists at the image host (%1).").arg(p_path);
      return result;
    }
  } else if (reply.m_error != QNetworkReply::ContentNotFoundError) {
    result.errorMessage = tr("Failed to query the resource at the image host (%1) (%2) (%3).")
                              .arg(urlStr, reply.errorStr(), reply.m_data);
    return result;
  }

  // Create the content.
  QJsonObject requestDataObj;
  requestDataObj[QStringLiteral("access_token")] = m_personalAccessToken;
  requestDataObj[QStringLiteral("message")] = QStringLiteral("VX_ADD: %1").arg(p_path);
  requestDataObj[QStringLiteral("content")] = QString::fromUtf8(p_data.toBase64());
  auto requestData = Utils::toJsonString(requestDataObj);
  reply = vte::NetworkAccess::post(QUrl(urlStr), rawHeader, requestData);
  if (reply.m_error != QNetworkReply::NoError) {
    result.errorMessage = tr("Failed to create resource at the image host (%1) (%2) (%3).")
                              .arg(urlStr, reply.errorStr(), reply.m_data);
    return result;
  }

  auto replyObj = Utils::fromJsonString(reply.m_data);
  Q_ASSERT(!replyObj.isEmpty());
  auto targetUrl = replyObj[QStringLiteral("content")]
                       .toObject()
                       .value(QStringLiteral("download_url"))
                       .toString();
  if (targetUrl.isEmpty()) {
    result.errorMessage = tr("Failed to create resource at the image host (%1) (%2) (%3).")
                              .arg(urlStr, reply.errorStr(), reply.m_data);
    return result;
  }

  targetUrl = PathUtils::encodeSpacesInPath(targetUrl);
  qDebug() << "created resource" << targetUrl;

  result.success = true;
  result.imageUrl = targetUrl;
  return result;
}

bool GiteeProvider::ownsUrl(const QString &p_url) const {
  return p_url.startsWith(m_imageUrlPrefix);
}

bool GiteeProvider::remove(const QString &p_url, QString &p_msg) {
  Q_ASSERT(ownsUrl(p_url));

  if (!ready()) {
    p_msg = tr("Invalid Gitee image host configuration.");
    return false;
  }

  const auto resourcePath = fetchResourcePath(m_imageUrlPrefix, p_url);

  auto rawHeader = prepareCommonHeaders();
  const auto urlStr = QStringLiteral("%1/repos/%2/%3/contents/%4")
                          .arg(c_apiUrl, m_userName, m_repoName, resourcePath);

  // Get the SHA of the resource.
  auto reply =
      vte::NetworkAccess::request(QUrl(addAccessToken(m_personalAccessToken, urlStr)), rawHeader);
  if (reply.m_error != QNetworkReply::NoError || isEmptyResponse(reply.m_data)) {
    p_msg = tr("Failed to fetch information about the resource (%1).").arg(resourcePath);
    return false;
  }

  auto replyObj = Utils::fromJsonString(reply.m_data);
  Q_ASSERT(!replyObj.isEmpty());
  const auto sha = replyObj[QStringLiteral("sha")].toString();
  if (sha.isEmpty()) {
    p_msg = tr("Failed to fetch SHA about the resource (%1) (%2).")
                .arg(resourcePath, QString::fromUtf8(reply.m_data));
    return false;
  }

  // Delete.
  QJsonObject requestDataObj;
  requestDataObj[QStringLiteral("access_token")] = m_personalAccessToken;
  requestDataObj[QStringLiteral("message")] = QStringLiteral("VX_DEL: %1").arg(resourcePath);
  requestDataObj[QStringLiteral("sha")] = sha;
  auto requestData = Utils::toJsonString(requestDataObj);
  reply = vte::NetworkAccess::deleteResource(QUrl(urlStr), rawHeader, requestData);
  if (reply.m_error != QNetworkReply::NoError) {
    p_msg = tr("Failed to delete resource (%1) (%2).")
                .arg(resourcePath, QString::fromUtf8(reply.m_data));
    return false;
  }

  qDebug() << "deleted resource" << resourcePath;

  return true;
}
