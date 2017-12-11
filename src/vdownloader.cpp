#include "vdownloader.h"

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
    qDebug() << "VDownloader receive" << reply->url().toString();
    emit downloadFinished(data, reply->url().toString());
}

void VDownloader::download(const QUrl &p_url)
{
    if (!p_url.isValid()) {
        return;
    }

    QNetworkRequest request(p_url);
    webCtrl.get(request);
    qDebug() << "VDownloader get" << p_url.toString();
}
