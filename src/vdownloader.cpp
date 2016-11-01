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
    emit downloadFinished(data);
}

void VDownloader::download(QUrl url)
{
    Q_ASSERT(url.isValid());
    QNetworkRequest request(url);
    webCtrl.get(request);
    qDebug() << "VDownloader get" << url.toString();
}
