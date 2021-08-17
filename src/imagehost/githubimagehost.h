#ifndef GITHUBIMAGEHOST_H
#define GITHUBIMAGEHOST_H

#include "imagehost.h"

#include <vtextedit/networkutils.h>

namespace vnotex
{
    class GitHubImageHost : public ImageHost
    {
        Q_OBJECT
    public:
        explicit GitHubImageHost(QObject *p_parent);

        bool ready() const Q_DECL_OVERRIDE;

        Type getType() const Q_DECL_OVERRIDE;

        QJsonObject getConfig() const Q_DECL_OVERRIDE;

        void setConfig(const QJsonObject &p_jobj) Q_DECL_OVERRIDE;

        bool testConfig(const QJsonObject &p_jobj, QString &p_msg) Q_DECL_OVERRIDE;

        QString create(const QByteArray &p_data, const QString &p_path, QString &p_msg) Q_DECL_OVERRIDE;

        bool remove(const QString &p_url, QString &p_msg) Q_DECL_OVERRIDE;

        bool ownsUrl(const QString &p_url) const Q_DECL_OVERRIDE;

        static QString fetchResourcePath(const QString &p_prefix, const QString &p_url);

    protected:
        QString m_personalAccessToken;

        QString m_userName;

        QString m_repoName;

        QString m_imageUrlPrefix;

    private:
        // Used to test.
        vte::NetworkReply getRepoInfo(const QString &p_token, const QString &p_userName, const QString &p_repoName) const;

        static void parseConfig(const QJsonObject &p_jobj,
                                QString &p_token,
                                QString &p_userName,
                                QString &p_repoName);

        static QPair<QByteArray, QByteArray> authorizationHeader(const QString &p_token);

        static QPair<QByteArray, QByteArray> acceptHeader();

        static vte::NetworkAccess::RawHeaderPairs prepareCommonHeaders(const QString &p_token);

        static const QString c_apiUrl;
    };
}

#endif // GITHUBIMAGEHOST_H
