#ifndef VDOWNLOADER_H
#define VDOWNLOADER_H

#include <QObject>
#include <QUrl>
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>

class VDownloader : public QObject
{
    Q_OBJECT
public:
    explicit VDownloader(QObject *parent = 0);
    void download(const QUrl &p_url);

signals:
    void downloadFinished(const QByteArray &data, const QString &url);

private slots:
    void handleDownloadFinished(QNetworkReply *reply);

private:
    QNetworkAccessManager webCtrl;
    QByteArray data;
};

#endif // VDOWNLOADER_H
