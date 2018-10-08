#include "vdownloader.h"

#include "utils/vutils.h"

VDownloader::VDownloader(QObject *parent)
    : QObject(parent)
{
    connect(&webCtrl, &QNetworkAccessManager::finished,
            this, &VDownloader::handleDownloadFinished);
}

void VDownloader::handleDownloadFinished(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "download reply error" << reply->error();
    }

    data = reply->readAll();
    reply->deleteLater();
    // The url() of the reply may be redirected and different from that of the request.
    emit downloadFinished(data, reply->request().url().toString());
}

static QNetworkRequest networkRequest(const QUrl &p_url)
{
    QNetworkRequest request(p_url);
    /*
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setProtocol(QSsl::SslV3);
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    request.setSslConfiguration(config);
    */

    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);

    return request;
}

void VDownloader::download(const QUrl &p_url)
{
    if (!p_url.isValid()) {
        return;
    }

    webCtrl.get(networkRequest(p_url));
}

QByteArray VDownloader::downloadSync(const QUrl &p_url)
{
    QByteArray data;
    if (!p_url.isValid()) {
        return data;
    }

    bool finished = false;
    QNetworkAccessManager nam;
    connect(&nam, &QNetworkAccessManager::finished,
            [&data, &finished](QNetworkReply *p_reply) {
                if (p_reply->error() != QNetworkReply::NoError) {
                    qWarning() << "download reply error" << p_reply->error();
                }

                data = p_reply->readAll();
                p_reply->deleteLater();
                finished = true;
            });

    nam.get(networkRequest(p_url));

    while (!finished) {
        VUtils::sleepWait(100);
    }

    return data;
}
