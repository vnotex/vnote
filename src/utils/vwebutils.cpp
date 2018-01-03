#include "vwebutils.h"

#include <QRegExp>
#include <QFileInfo>
#include <QDebug>

VWebUtils::VWebUtils()
{
}

bool VWebUtils::fixImageSrcInHtml(const QUrl &p_baseUrl, QString &p_html)
{
    bool changed = false;

#if defined(Q_OS_WIN)
    QUrl::ComponentFormattingOption strOpt = QUrl::EncodeSpaces;
#else
    QUrl::ComponentFormattingOption strOpt = QUrl::FullyEncoded;
#endif

    QRegExp reg("(<img src=\")([^\"]+)\"");

    int pos = 0;
    while (pos < p_html.size()) {
        int idx = p_html.indexOf(reg, pos);
        if (idx == -1) {
            break;
        }

        QString urlStr = reg.cap(2);
        QUrl imgUrl(urlStr);

        QString fixedStr;
        if (imgUrl.isRelative()) {
            fixedStr = p_baseUrl.resolved(imgUrl).toString(strOpt);
        } else if (imgUrl.isLocalFile()) {
            fixedStr = imgUrl.toString(strOpt);
        } else if (imgUrl.scheme() != "https" && imgUrl.scheme() != "http") {
            QString tmp = imgUrl.toString();
            if (QFileInfo::exists(tmp)) {
                fixedStr = QUrl::fromLocalFile(tmp).toString(strOpt);
            }
        }

        pos = idx + reg.matchedLength();
        if (!fixedStr.isEmpty() && urlStr != fixedStr) {
            qDebug() << "fix img url" << urlStr << fixedStr;
            // Insert one more space to avoid fix the url twice.
            pos = pos + fixedStr.size() + 1 - urlStr.size();
            p_html.replace(idx,
                           reg.matchedLength(),
                           QString("<img  src=\"%1\"").arg(fixedStr));
            changed = true;
        }
    }

    return changed;
}
