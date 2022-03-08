#include "giteeimagehost.h"

#include <QDebug>
#include <QFileInfo>
#include <QByteArray>

#include <utils/utils.h>
#include <utils/pathutils.h>

using namespace vnotex;

const QString GiteeImageHost::c_apiUrl = "https://gitee.com/api/v5";

GiteeImageHost::GiteeImageHost(QObject *p_parent)
    : RepoImageHost(p_parent)
{
}

bool GiteeImageHost::ready() const
{
    return !m_personalAccessToken.isEmpty() && !m_userName.isEmpty() && !m_repoName.isEmpty();
}

ImageHost::Type GiteeImageHost::getType() const
{
    return Type::Gitee;
}

QJsonObject GiteeImageHost::getConfig() const
{
    QJsonObject obj;
    obj[QStringLiteral("personal_access_token")] = m_personalAccessToken;
    obj[QStringLiteral("user_name")] = m_userName;
    obj[QStringLiteral("repository_name")] = m_repoName;
    return obj;
}

void GiteeImageHost::setConfig(const QJsonObject &p_jobj)
{
    parseConfig(p_jobj, m_personalAccessToken, m_userName, m_repoName);

    // Do not assume the default branch.
    m_imageUrlPrefix = QString("https://gitee.com/%1/%2/raw/").arg(m_userName, m_repoName);
}

vte::NetworkAccess::RawHeaderPairs GiteeImageHost::prepareCommonHeaders()
{
    vte::NetworkAccess::RawHeaderPairs rawHeader;
    rawHeader.push_back(qMakePair(QByteArray("Content-Type"), QByteArray("application/json;charset=UTF-8")));
    return rawHeader;
}

QString GiteeImageHost::addAccessToken(const QString &p_token, QString p_url)
{
    if (p_url.contains(QLatin1Char('?'))) {
        p_url += QString("&access_token=%1").arg(p_token);
    } else {
        p_url += QString("?access_token=%1").arg(p_token);
    }
    return p_url;
}

vte::NetworkReply GiteeImageHost::getRepoInfo(const QString &p_token, const QString &p_userName, const QString &p_repoName) const
{
    auto rawHeader = prepareCommonHeaders();
    auto urlStr = QString("%1/repos/%2/%3").arg(c_apiUrl, p_userName, p_repoName);
    auto reply = vte::NetworkAccess::request(QUrl(addAccessToken(p_token, urlStr)), rawHeader);
    return reply;
}

static bool isEmptyResponse(const QByteArray &p_data)
{
    return p_data == QByteArray("[]");
}

QString GiteeImageHost::create(const QByteArray &p_data, const QString &p_path, QString &p_msg)
{
    QString destUrl;

    if (p_path.isEmpty()) {
        p_msg = tr("Failed to create image with empty path.");
        return destUrl;
    }

    if (!ready()) {
        p_msg = tr("Invalid Gitee image host configuration.");
        return QString();
    }

    auto rawHeader = prepareCommonHeaders();
    const auto urlStr = QString("%1/repos/%2/%3/contents/%4").arg(c_apiUrl, m_userName, m_repoName, p_path);

    // Check if @p_path already exists.
    auto reply = vte::NetworkAccess::request(QUrl(addAccessToken(m_personalAccessToken, urlStr)), rawHeader);
    if (reply.m_error == QNetworkReply::NoError) {
        if (!isEmptyResponse(reply.m_data)) {
            p_msg = tr("The resource already exists at the image host (%1).").arg(p_path);
            return QString();
        }
    } else if (reply.m_error != QNetworkReply::ContentNotFoundError) {
        p_msg = tr("Failed to query the resource at the image host (%1) (%2) (%3).").arg(urlStr, reply.errorStr(), reply.m_data);
        return QString();
    }

    // Create the content.
    QJsonObject requestDataObj;
    requestDataObj[QStringLiteral("access_token")] = m_personalAccessToken;
    requestDataObj[QStringLiteral("message")] = QString("VX_ADD: %1").arg(p_path);
    requestDataObj[QStringLiteral("content")] = QString::fromUtf8(p_data.toBase64());
    auto requestData = Utils::toJsonString(requestDataObj);
    reply = vte::NetworkAccess::post(QUrl(urlStr), rawHeader, requestData);
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
            targetUrl = PathUtils::encodeSpacesInPath(targetUrl);
            qDebug() << "created resource" << targetUrl;
        }
        return targetUrl;
    }
}

bool GiteeImageHost::ownsUrl(const QString &p_url) const
{
    return p_url.startsWith(m_imageUrlPrefix);
}

bool GiteeImageHost::remove(const QString &p_url, QString &p_msg)
{
    Q_ASSERT(ownsUrl(p_url));

    if (!ready()) {
        p_msg = tr("Invalid Gitee image host configuration.");
        return false;
    }

    const auto resourcePath = fetchResourcePath(m_imageUrlPrefix, p_url);

    auto rawHeader = prepareCommonHeaders();
    const auto urlStr = QString("%1/repos/%2/%3/contents/%4").arg(c_apiUrl, m_userName, m_repoName, resourcePath);

    // Get the SHA of the resource.
    auto reply = vte::NetworkAccess::request(QUrl(addAccessToken(m_personalAccessToken, urlStr)), rawHeader);
    if (reply.m_error != QNetworkReply::NoError || isEmptyResponse(reply.m_data)) {
        p_msg = tr("Failed to fetch information about the resource (%1).").arg(resourcePath);
        return false;
    }

    auto replyObj = Utils::fromJsonString(reply.m_data);
    Q_ASSERT(!replyObj.isEmpty());
    const auto sha = replyObj[QStringLiteral("sha")].toString();
    if (sha.isEmpty()) {
        p_msg = tr("Failed to fetch SHA about the resource (%1) (%2).").arg(resourcePath, QString::fromUtf8(reply.m_data));
        return false;
    }

    // Delete.
    QJsonObject requestDataObj;
    requestDataObj[QStringLiteral("access_token")] = m_personalAccessToken;
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
