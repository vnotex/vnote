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
    data = reply->readAll();
    reply->deleteLater();
    emit downloadFinished(data, reply->url().toString());
}

void VDownloader::download(const QUrl &p_url)
{
    if (!p_url.isValid()) {
        return;
    }

    QNetworkRequest request(p_url);
    webCtrl.get(request);
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
                data = p_reply->readAll();
                p_reply->deleteLater();
                finished = true;
            });

    nam.get(QNetworkRequest(p_url));

    while (!finished) {
        VUtils::sleepWait(100);
    }

    return data;
}
