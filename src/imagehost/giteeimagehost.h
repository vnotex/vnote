#ifndef GITEEIMAGEHOST_H
#define GITEEIMAGEHOST_H

#include "imagehost.h"

#include <vtextedit/networkutils.h>

namespace vnotex
{
    class GiteeImageHost : public ImageHost
    {
        Q_OBJECT
    public:
        explicit GiteeImageHost(QObject *p_parent);

        bool ready() const Q_DECL_OVERRIDE;

        Type getType() const Q_DECL_OVERRIDE;

        QJsonObject getConfig() const Q_DECL_OVERRIDE;

        void setConfig(const QJsonObject &p_jobj) Q_DECL_OVERRIDE;

        bool testConfig(const QJsonObject &p_jobj, QString &p_msg) Q_DECL_OVERRIDE;

        QString create(const QByteArray &p_data, const QString &p_path, QString &p_msg) Q_DECL_OVERRIDE;

        bool remove(const QString &p_url, QString &p_msg) Q_DECL_OVERRIDE;

        bool ownsUrl(const QString &p_url) const Q_DECL_OVERRIDE;

    private:
        // Used to test.
        vte::NetworkReply getRepoInfo(const QString &p_token, const QString &p_userName, const QString &p_repoName) const;

        static void parseConfig(const QJsonObject &p_jobj,
                                QString &p_token,
                                QString &p_userName,
                                QString &p_repoName);

        static vte::NetworkAccess::RawHeaderPairs prepareCommonHeaders();

        static QString addAccessToken(const QString &p_token, QString p_url);

        QString m_personalAccessToken;

        QString m_userName;

        QString m_repoName;

        QString m_imageUrlPrefix;

        static const QString c_apiUrl;
    };
}

#endif // GITEEIMAGEHOST_H
