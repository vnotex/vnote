#ifndef REPOIMAGEHOST_H
#define REPOIMAGEHOST_H

#include "imagehost.h"

#include <vtextedit/networkutils.h>

namespace vnotex
{
    class RepoImageHost : public ImageHost
    {
        Q_OBJECT
    public:
        explicit RepoImageHost(QObject *p_parent);

        bool testConfig(const QJsonObject &p_jobj, QString &p_msg) Q_DECL_OVERRIDE;

    protected:
        virtual void parseConfig(const QJsonObject &p_jobj,
                                 QString &p_token,
                                 QString &p_userName,
                                 QString &p_repoName);

        // Used to test.
        virtual vte::NetworkReply getRepoInfo(const QString &p_token, const QString &p_userName, const QString &p_repoName) const = 0;

        static QString fetchResourcePath(const QString &p_prefix, const QString &p_url);
    };
}

#endif // REPOIMAGEHOST_H
