#include "githubprovider.h"

#include <QByteArray>
#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <utils/utils.h>
#include <utils/webutils.h>

using namespace vnotex;

const QString GitHubProvider::c_apiUrl = "https://api.github.com";

GitHubProvider::GitHubProvider(QObject *p_parent) : IImageHostProvider(p_parent) {}

QString GitHubProvider::typeId() const { return QStringLiteral("github"); }

QString GitHubProvider::displayName() const { return tr("GitHub Repository"); }

bool GitHubProvider::ready() const {
  return !m_personalAccessToken.isEmpty() && !m_userName.isEmpty() && !m_repoName.isEmpty();
}

bool GitHubProvider::supportsDelete() const { return true; }

QJsonObject GitHubProvider::getConfig() const {
  QJsonObject obj;
  obj[QStringLiteral("personalAccessToken")] = m_personalAccessToken;
  obj[QStringLiteral("userName")] = m_userName;
  obj[QStringLiteral("repositoryName")] = m_repoName;
  return obj;
}

QHash<QString, ConfigFieldHint> GitHubProvider::getConfigFieldHints() const
{
  return {
    {QStringLiteral("personalAccessToken"),
     {QStringLiteral("ghp_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"),
      tr("GitHub Personal Access Token with 'repo' scope. Generate at GitHub → Settings → Developer settings → Personal access tokens.")}},
    {QStringLiteral("userName"),
     {QStringLiteral("octocat"),
      tr("Your GitHub username.")}},
    {QStringLiteral("repositoryName"),
     {QStringLiteral("my-image-repo"),
      tr("Name of the GitHub repository to store images. Must already exist.")}},
  };
}

void GitHubProvider::setConfig(const QJsonObject &p_config) {
  m_personalAccessToken = p_config[QStringLiteral("personalAccessToken")].toString();
  m_userName = p_config[QStringLiteral("userName")].toString();
  m_repoName = p_config[QStringLiteral("repositoryName")].toString();

  // Do not assume the default branch.
  m_imageUrlPrefix =
      QStringLiteral("https://raw.githubusercontent.com/%1/%2/").arg(m_userName, m_repoName);
}

bool GitHubProvider::testConfig(const QJsonObject &p_config, QString &p_msg) {
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

QPair<QByteArray, QByteArray> GitHubProvider::authorizationHeader(const QString &p_token) {
  auto token = "token " + p_token;
  return qMakePair(QByteArray("Authorization"), token.toUtf8());
}

QPair<QByteArray, QByteArray> GitHubProvider::acceptHeader() {
  return qMakePair(QByteArray("Accept"), QByteArray("application/vnd.github.v3+json"));
}

vte::NetworkAccess::RawHeaderPairs GitHubProvider::prepareCommonHeaders(const QString &p_token) {
  vte::NetworkAccess::RawHeaderPairs rawHeader;
  rawHeader.push_back(authorizationHeader(p_token));
  rawHeader.push_back(acceptHeader());
  return rawHeader;
}

vte::NetworkReply GitHubProvider::getRepoInfo(const QString &p_token, const QString &p_userName,
                                              const QString &p_repoName) const {
  auto rawHeader = prepareCommonHeaders(p_token);
  const auto urlStr = QStringLiteral("%1/repos/%2/%3").arg(c_apiUrl, p_userName, p_repoName);
  auto reply = vte::NetworkAccess::request(QUrl(urlStr), rawHeader);
  return reply;
}

QString GitHubProvider::fetchResourcePath(const QString &p_prefix, const QString &p_url) {
  auto resourcePath = p_url.mid(p_prefix.size());
  // Skip the branch name.
  resourcePath = resourcePath.mid(resourcePath.indexOf(QLatin1Char('/')) + 1);
  resourcePath = WebUtils::purifyUrl(resourcePath);
  return resourcePath;
}

ImageUploadResult GitHubProvider::upload(const QByteArray &p_data, const QString &p_path) {
  ImageUploadResult result;

  if (p_path.isEmpty()) {
    result.errorMessage = tr("Failed to create image with empty path.");
    return result;
  }

  if (!ready()) {
    result.errorMessage = tr("Invalid GitHub image host configuration.");
    return result;
  }

  auto rawHeader = prepareCommonHeaders(m_personalAccessToken);
  const auto urlStr =
      QStringLiteral("%1/repos/%2/%3/contents/%4").arg(c_apiUrl, m_userName, m_repoName, p_path);

  // Check if @p_path already exists.
  auto reply = vte::NetworkAccess::request(QUrl(urlStr), rawHeader);
  if (reply.m_error == QNetworkReply::NoError) {
    result.errorMessage = tr("The resource already exists at the image host (%1).").arg(p_path);
    return result;
  } else if (reply.m_error != QNetworkReply::ContentNotFoundError) {
    result.errorMessage = tr("Failed to query the resource at the image host (%1) (%2) (%3).")
                              .arg(urlStr, reply.errorStr(), reply.m_data);
    return result;
  }

  // Create the content.
  QJsonObject requestDataObj;
  requestDataObj[QStringLiteral("message")] = QStringLiteral("VX_ADD: %1").arg(p_path);
  requestDataObj[QStringLiteral("content")] = QString::fromUtf8(p_data.toBase64());
  auto requestData = Utils::toJsonString(requestDataObj);
  reply = vte::NetworkAccess::put(QUrl(urlStr), rawHeader, requestData);
  if (reply.m_error != QNetworkReply::NoError) {
    result.errorMessage = tr("Failed to create resource at the image host (%1) (%2) (%3).")
                              .arg(urlStr, reply.errorStr(), reply.m_data);
    return result;
  } else {
    auto replyObj = Utils::fromJsonString(reply.m_data);
    Q_ASSERT(!replyObj.isEmpty());
    auto targetUrl = replyObj[QStringLiteral("content")]
                         .toObject()
                         .value(QStringLiteral("download_url"))
                         .toString();
    if (targetUrl.isEmpty()) {
      result.errorMessage = tr("Failed to create resource at the image host (%1) (%2) (%3).")
                                .arg(urlStr, reply.errorStr(), reply.m_data);
    } else {
      qDebug() << "created resource" << targetUrl;
      result.success = true;
      result.imageUrl = targetUrl;
      result.fileName = p_path;
    }
    return result;
  }
}

bool GitHubProvider::ownsUrl(const QString &p_url) const {
  return p_url.startsWith(m_imageUrlPrefix);
}

bool GitHubProvider::remove(const QString &p_url, QString &p_msg) {
  Q_ASSERT(ownsUrl(p_url));

  if (!ready()) {
    p_msg = tr("Invalid GitHub image host configuration.");
    return false;
  }

  const auto resourcePath = fetchResourcePath(m_imageUrlPrefix, p_url);

  auto rawHeader = prepareCommonHeaders(m_personalAccessToken);
  const auto urlStr = QStringLiteral("%1/repos/%2/%3/contents/%4")
                          .arg(c_apiUrl, m_userName, m_repoName, resourcePath);

  // Get the SHA of the resource.
  QString sha = "";
  auto reply = vte::NetworkAccess::request(QUrl(urlStr), rawHeader);
  if (reply.m_error == QNetworkReply::NoError) {
    auto replyObj = Utils::fromJsonString(reply.m_data);
    Q_ASSERT(!replyObj.isEmpty());
    sha = replyObj[QStringLiteral("sha")].toString();
  } else if (reply.m_error == QNetworkReply::ContentAccessDenied) { // File larger than 1M

    // Get all file information under the warehouse directory
    const auto fileInfo = QFileInfo(urlStr);
    const auto urlFilePath = QUrl(fileInfo.path());
    const auto fleName = fileInfo.fileName();
    reply = vte::NetworkAccess::request(urlFilePath, rawHeader);

    if (QNetworkReply::NoError == reply.m_error) {
      const auto jsonArray = QJsonDocument::fromJson(reply.m_data).array();
      for (auto i = jsonArray.begin(); i < jsonArray.end(); i++) {
        if (!i->isObject()) {
          continue;
        }
        const auto jsonObj = i->toObject();
        const auto name = jsonObj[QStringLiteral("name")].toString();
        if (name == fleName) {
          sha = jsonObj[QStringLiteral("sha")].toString();
          break;
        }
      }
    }
  }

  if (sha.isEmpty()) {
    p_msg = tr("Failed to fetch SHA about the resource (%1) (%2).")
                .arg(resourcePath, QString::fromUtf8(reply.m_data));
    return false;
  }

  // Delete.
  QJsonObject requestDataObj;
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
