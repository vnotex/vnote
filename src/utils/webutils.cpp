#include "webutils.h"

#include <QUrl>
#include <QFileInfo>
#include <QImageReader>

#include "fileutils.h"
#include "pathutils.h"
#include <vtextedit/networkutils.h>
#include <core/exception.h>

using namespace vnotex;

QString WebUtils::purifyUrl(const QString &p_url)
{
    int idx = p_url.indexOf('?');
    if (idx > -1) {
        return p_url.left(idx);
    }

    return p_url;
}

QString WebUtils::toDataUri(const QUrl &p_url, bool p_keepTitle)
{
    QString uri;
    Q_ASSERT(!p_url.isRelative());
    QString file = p_url.isLocalFile() ? p_url.toLocalFile() : p_url.toString();
    const auto filePath = purifyUrl(file);
    const QFileInfo finfo(filePath);
    const QString suffix(finfo.suffix().toLower());
    if (!QImageReader::supportedImageFormats().contains(suffix.toLatin1())) {
        return uri;
    }

    QByteArray data;
    if (p_url.scheme() == "https" || p_url.scheme() == "http") {
        // Download it.
        data = vte::Downloader::download(p_url);
    } else if (finfo.exists()) {
        data = FileUtils::readFile(filePath);
    }

    if (data.isEmpty()) {
        return uri;
    }

    if (suffix == "svg") {
        uri = QString("data:image/svg+xml;utf8,%1").arg(QString::fromUtf8(data));
        uri.replace('\r', "").replace('\n', "");

        // Using unescaped '#' characters in a data URI body is deprecated and
        // will be removed in M68, around July 2018. Please use '%23' instead.
        uri.replace("#", "%23");

        // Escape "'" to avoid conflict with src='...' attribute.
        uri.replace("'", "%27");

        if (!p_keepTitle) {
            // Remove <title>...</title>.
            QRegExp reg("<title>.*</title>", Qt::CaseInsensitive);
            uri.remove(reg);
        }
    } else {
        uri = QString("data:image/%1;base64,%2").arg(suffix, QString::fromUtf8(data.toBase64()));
    }

    return uri;
}

QString WebUtils::copyResource(const QUrl &p_url, const QString &p_folder)
{
    Q_ASSERT(!p_url.isRelative());

    QDir dir(p_folder);
    if (!dir.exists()) {
        dir.mkpath(p_folder);
    }

    QString file = p_url.isLocalFile() ? p_url.toLocalFile() : p_url.toString();
    QFileInfo finfo(file);
    auto fileName = FileUtils::generateFileNameWithSequence(p_folder, finfo.completeBaseName(), finfo.suffix());
    QString targetFile = dir.absoluteFilePath(fileName);

    bool succ = true;
    try {
        if (p_url.scheme() == "https" || p_url.scheme() == "http") {
            // Download it.
            auto data = vte::Downloader::download(p_url);
            if (!data.isEmpty()) {
                FileUtils::writeFile(targetFile, data);
            }
        } else if (finfo.exists()) {
            // Do a copy.
            FileUtils::copyFile(file, targetFile, false);
        }
    } catch (Exception &p_e) {
        Q_UNUSED(p_e);
        succ = false;
    }

    return succ ? targetFile : QString();
}
