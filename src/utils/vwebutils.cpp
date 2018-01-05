#include "vwebutils.h"

#include <QRegExp>
#include <QFileInfo>
#include <QDebug>

#include "vpalette.h"

extern VPalette *g_palette;

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

bool VWebUtils::removeBackgroundColor(QString &p_html)
{
    QRegExp reg("(<[^>]+\\sstyle=[^>]*(\\s|\"))background(-color)?:[^;]+;([^>]*>)");
    int size = p_html.size();
    p_html.replace(reg, "\\1\\4");
    return p_html.size() != size;
}

bool VWebUtils::translateColors(QString &p_html)
{
    bool changed = false;

    const QHash<QString, QString> &mapping = g_palette->getColorMapping();
    if (mapping.isEmpty()) {
        return changed;
    }

    QRegExp tagReg("(<[^>]+\\sstyle=[^>]*>)");
    // Won't mixed up with background-color.
    QRegExp colorReg("(\\s|\")color:([^;]+);");

    int pos = 0;
    while (pos < p_html.size()) {
        int idx = p_html.indexOf(tagReg, pos);
        if (idx == -1) {
            break;
        }

        QString styleStr = tagReg.cap(1);
        QString alteredStyleStr = styleStr;

        int posb = 0;
        while (posb < alteredStyleStr.size()) {
            int idxb = alteredStyleStr.indexOf(colorReg, posb);
            if (idxb == -1) {
                break;
            }

            QString col = colorReg.cap(2).trimmed().toLower();
            auto it = mapping.find(col);
            if (it == mapping.end()) {
                posb = idxb + colorReg.matchedLength();
                continue;
            }

            // Replace the color.
            QString newCol = it.value();
            // Add one extra space between color and :.
            QString newStr = QString("%1color : %2;").arg(colorReg.cap(1)).arg(newCol);
            alteredStyleStr.replace(idxb, colorReg.matchedLength(), newStr);
            posb = idxb + newStr.size();
            changed = true;
        }

        pos = idx + tagReg.matchedLength();
        if (changed) {
            pos = pos + alteredStyleStr.size() - styleStr.size();
            p_html.replace(idx, tagReg.matchedLength(), alteredStyleStr);
        }
    }

    return changed;
}
