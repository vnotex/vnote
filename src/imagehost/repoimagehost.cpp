#include "repoimagehost.h"

#include <utils/webutils.h>
#include <utils/utils.h>

using namespace vnotex;

RepoImageHost::RepoImageHost(QObject *p_parent)
    : ImageHost(p_parent)
{
}

bool RepoImageHost::testConfig(const QJsonObject &p_jobj, QString &p_msg)
{
    p_msg.clear();

    QString token, userName, repoName;
    parseConfig(p_jobj, token, userName, repoName);

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

void RepoImageHost::parseConfig(const QJsonObject &p_jobj,
                                QString &p_token,
                                QString &p_userName,
                                QString &p_repoName)
{
    p_token = p_jobj[QStringLiteral("personal_access_token")].toString();
    p_userName = p_jobj[QStringLiteral("user_name")].toString();
    p_repoName = p_jobj[QStringLiteral("repository_name")].toString();
}

QString RepoImageHost::fetchResourcePath(const QString &p_prefix, const QString &p_url)
{
    auto resourcePath = p_url.mid(p_prefix.size());
    // Skip the branch name.
    resourcePath = resourcePath.mid(resourcePath.indexOf(QLatin1Char('/')) + 1);
    resourcePath = WebUtils::purifyUrl(resourcePath);
    return resourcePath;
}
