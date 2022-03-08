#include "githubimagehost.h"

#include <QDebug>
#include <QFileInfo>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>

#include <utils/utils.h>

using namespace vnotex;

const QString GitHubImageHost::c_apiUrl = "https://api.github.com";

GitHubImageHost::GitHubImageHost(QObject *p_parent)
    : RepoImageHost(p_parent)
{
}

bool GitHubImageHost::ready() const
{
    return !m_personalAccessToken.isEmpty() && !m_userName.isEmpty() && !m_repoName.isEmpty();
}

ImageHost::Type GitHubImageHost::getType() const
{
    return Type::GitHub;
}

QJsonObject GitHubImageHost::getConfig() const
{
    QJsonObject obj;
    obj[QStringLiteral("personal_access_token")] = m_personalAccessToken;
    obj[QStringLiteral("user_name")] = m_userName;
    obj[QStringLiteral("repository_name")] = m_repoName;
    return obj;
}

void GitHubImageHost::setConfig(const QJsonObject &p_jobj)
{
    parseConfig(p_jobj, m_personalAccessToken, m_userName, m_repoName);

    // Do not assume the default branch.
    m_imageUrlPrefix = QString("https://raw.githubusercontent.com/%1/%2/").arg(m_userName, m_repoName);
}

QPair<QByteArray, QByteArray> GitHubImageHost::authorizationHeader(const QString &p_token)
{
    auto token = "token " + p_token;
    return qMakePair(QByteArray("Authorization"), token.toUtf8());
}

QPair<QByteArray, QByteArray> GitHubImageHost::acceptHeader()
{
    return qMakePair(QByteArray("Accept"), QByteArray("application/vnd.github.v3+json"));
}

vte::NetworkAccess::RawHeaderPairs GitHubImageHost::prepareCommonHeaders(const QString &p_token)
{
    vte::NetworkAccess::RawHeaderPairs rawHeader;
    rawHeader.push_back(authorizationHeader(p_token));
    rawHeader.push_back(acceptHeader());
    return rawHeader;
}

vte::NetworkReply GitHubImageHost::getRepoInfo(const QString &p_token, const QString &p_userName, const QString &p_repoName) const
{
    auto rawHeader = prepareCommonHeaders(p_token);
    const auto urlStr = QString("%1/repos/%2/%3").arg(c_apiUrl, p_userName, p_repoName);
    auto reply = vte::NetworkAccess::request(QUrl(urlStr), rawHeader);
    return reply;
}

QString GitHubImageHost::create(const QByteArray &p_data, const QString &p_path, QString &p_msg)
{
    QString destUrl;

    if (p_path.isEmpty()) {
        p_msg = tr("Failed to create image with empty path.");
        return destUrl;
    }

    if (!ready()) {
        p_msg = tr("Invalid GitHub image host configuration.");
        return QString();
    }

    auto rawHeader = prepareCommonHeaders(m_personalAccessToken);
    const auto urlStr = QString("%1/repos/%2/%3/contents/%4").arg(c_apiUrl, m_userName, m_repoName, p_path);

    // Check if @p_path already exists.
    auto reply = vte::NetworkAccess::request(QUrl(urlStr), rawHeader);
    if (reply.m_error == QNetworkReply::NoError) {
        p_msg = tr("The resource already exists at the image host (%1).").arg(p_path);
        return QString();
    } else if (reply.m_error != QNetworkReply::ContentNotFoundError) {
        p_msg = tr("Failed to query the resource at the image host (%1) (%2) (%3).").arg(urlStr, reply.errorStr(), reply.m_data);
        return QString();
    }

    // Create the content.
    QJsonObject requestDataObj;
    requestDataObj[QStringLiteral("message")] = QString("VX_ADD: %1").arg(p_path);
    requestDataObj[QStringLiteral("content")] = QString::fromUtf8(p_data.toBase64());
    auto requestData = Utils::toJsonString(requestDataObj);
    reply = vte::NetworkAccess::put(QUrl(urlStr), rawHeader, requestData);
    if (reply.m_error != QNetworkReply::NoError) {
        p_msg = tr("Failed to create resource at the image host (%1) (%2) (%3).").arg(urlStr, reply.errorStr(), reply.m_data);
        return QString();
    } else {
        auto replyObj = Utils::fromJsonString(reply.m_data);
        Q_ASSERT(!replyObj.isEmpty());
        auto targetUrl = replyObj[QStringLiteral("content")].toObject().value(QStringLiteral("download_url")).toString();
        if (targetUrl.isEmpty()) {
            p_msg = tr("Failed to create resource at the image host (%1) (%2) (%3).").arg(urlStr, reply.errorStr(), reply.m_data);
        } else {
            qDebug() << "created resource" << targetUrl;
        }
        return targetUrl;
    }
}

bool GitHubImageHost::ownsUrl(const QString &p_url) const
{
    return p_url.startsWith(m_imageUrlPrefix);
}

bool GitHubImageHost::remove(const QString &p_url, QString &p_msg)
{
    Q_ASSERT(ownsUrl(p_url));

    if (!ready()) {
        p_msg = tr("Invalid GitHub image host configuration.");
        return false;
    }

    const auto resourcePath = fetchResourcePath(m_imageUrlPrefix, p_url);

    auto rawHeader = prepareCommonHeaders(m_personalAccessToken);
    const auto urlStr = QString("%1/repos/%2/%3/contents/%4").arg(c_apiUrl, m_userName, m_repoName, resourcePath);

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
        p_msg = tr("Failed to fetch SHA about the resource (%1) (%2).").arg(resourcePath, QString::fromUtf8(reply.m_data));
        return false;
    }

    // Delete.
    QJsonObject requestDataObj;
    requestDataObj[QStringLiteral("message")] = QString("VX_DEL: %1").arg(resourcePath);
    requestDataObj[QStringLiteral("sha")] = sha;
    auto requestData = Utils::toJsonString(requestDataObj);
    reply = vte::NetworkAccess::deleteResource(QUrl(urlStr), rawHeader, requestData);
    if (reply.m_error != QNetworkReply::NoError) {
        p_msg = tr("Failed to delete resource (%1) (%2).").arg(resourcePath, QString::fromUtf8(reply.m_data));
        return false;
    }

    qDebug() << "deleted resource" << resourcePath;

    return true;
}
