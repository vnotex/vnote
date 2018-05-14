#include "vwebutils.h"

#include <QFileInfo>
#include <QObject>
#include <QDebug>
#include <QDir>
#include <QUrl>
#include <QImageReader>

#include "vpalette.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "vdownloader.h"

extern VPalette *g_palette;

extern VConfigManager *g_config;

VWebUtils::VWebUtils()
{
}

void VWebUtils::init()
{
    m_stylesToRemoveWhenCopied = g_config->getStylesToRemoveWhenCopied();

    m_styleOfSpanForMark = g_config->getStyleOfSpanForMark();

    m_tagReg = QRegExp("<([^>/\\s]+)([^>]*)>");

    m_styleTagReg = QRegExp("<([^>\\s]+)([^>]*\\s)style=\"([^\">]+)\"([^>]*)>");

    m_imgTagReg = QRegExp("<img src=\"([^\"]+)\"[^>]*>");

    initCopyTargets(g_config->getCopyTargets());
}

void VWebUtils::initCopyTargets(const QStringList &p_str)
{
    Q_ASSERT(m_copyTargets.isEmpty());
    // cap(1): action;
    // cap(3): arguments;
    QRegExp actReg("([0-9a-zA-Z])(\\(([^\\)]*)\\))?");

    for (auto const & str : p_str) {
        auto vals = str.split('$');
        if (vals.size() != 2) {
            continue;
        }

        CopyTarget tar;
        tar.m_name = vals[0];
        if (tar.m_name.isEmpty()) {
            continue;
        }

        auto acts = vals[1].split(':');
        for (auto const & it : acts) {
            if (it.isEmpty()) {
                continue;
            }

            if (!actReg.exactMatch(it)) {
                continue;
            }

            if (actReg.cap(1).size() != 1) {
                continue;
            }

            CopyTargetAction act;
            act.m_act = actReg.cap(1)[0];

            if (!actReg.cap(3).isEmpty()) {
                act.m_args = actReg.cap(3).toLower().split('|');
            }

            tar.m_actions.append(act);
        }

        m_copyTargets.append(tar);
    }

    qDebug() << "init" << m_copyTargets.size() << "copy targets";
}

bool VWebUtils::fixImageSrc(const QUrl &p_baseUrl, QString &p_html)
{
    bool changed = false;

#if defined(Q_OS_WIN)
    QUrl::ComponentFormattingOption strOpt = QUrl::EncodeSpaces;
#else
    QUrl::ComponentFormattingOption strOpt = QUrl::FullyEncoded;
#endif

    QRegExp reg("<img src=\"([^\"]+)\"");

    int pos = 0;
    while (pos < p_html.size()) {
        int idx = p_html.indexOf(reg, pos);
        if (idx == -1) {
            break;
        }

        QString urlStr = reg.cap(1);
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
            pos = pos + fixedStr.size() + 1 - urlStr.size();
            p_html.replace(idx,
                           reg.matchedLength(),
                           QString("<img src=\"%1\"").arg(fixedStr));
            changed = true;
        }
    }

    return changed;
}

QStringList VWebUtils::getCopyTargetsName() const
{
    QStringList names;
    for (auto const & it : m_copyTargets) {
        names << it.m_name;
    }

    return names;
}

bool VWebUtils::alterHtmlAsTarget(const QUrl &p_baseUrl, QString &p_html, const QString &p_target) const
{
    int idx = targetIndex(p_target);
    if (idx == -1) {
        return false;
    }

    bool altered = false;
    for (auto const & act : m_copyTargets[idx].m_actions) {
        if (const_cast<VWebUtils *>(this)->alterHtmlByTargetAction(p_baseUrl, p_html, act)) {
            altered = true;
        }
    }

    return altered;
}

int VWebUtils::targetIndex(const QString &p_target) const
{
    if (p_target.isEmpty()) {
        return -1;
    }

    for (int i = 0; i < m_copyTargets.size(); ++i) {
        if (m_copyTargets[i].m_name == p_target) {
            return i;
        }
    }

    return -1;
}

bool VWebUtils::alterHtmlByTargetAction(const QUrl &p_baseUrl, QString &p_html, const CopyTargetAction &p_action)
{
    bool altered = false;
    switch (p_action.m_act.toLatin1()) {
    case 's':
        if (!p_html.startsWith("<html>")) {
            p_html = "<html><body>" + p_html + "</body></html>";
            altered = true;
        }

        break;

    case 'e':
        if (!p_html.startsWith("<html>")) {
            p_html = "<html><body><!--StartFragment-->" + p_html + "<!--EndFragment--></body></html>";
            altered = true;
        }

        break;

    case 'b':
        altered = removeBackgroundColor(p_html, p_action.m_args);
        break;

    case 'c':
        altered = translateColors(p_html, p_action.m_args);
        break;

    case 'i':
        altered = fixImageSrc(p_baseUrl, p_html);
        break;

    case 'm':
        altered = removeMarginPadding(p_html, p_action.m_args);
        break;

    case 'x':
        altered = removeStylesToRemoveWhenCopied(p_html, p_action.m_args);
        break;

    case 'r':
        altered = removeAllStyles(p_html, p_action.m_args);
        break;

    case 'a':
        altered = transformMarkToSpan(p_html);
        break;

    case 'p':
        altered = replacePreBackgroundColorWithCode(p_html);
        break;

    case 'n':
        altered = replaceNewLineWithBR(p_html);
        break;

    case 'g':
        altered = replaceLocalImgWithWarningLabel(p_html);
        break;

    case 'd':
        altered = addSpanInsideCode(p_html);
        break;

    case 'f':
        altered = replaceQuoteInFontFamily(p_html);
        break;

    case 'h':
        altered = replaceHeadingWithSpan(p_html);
        break;

    case 'j':
        altered = fixXHtmlTags(p_html);
        break;

    default:
        break;
    }

    return altered;
}

static int skipToTagEnd(const QString &p_html,
                        int p_pos,
                        const QString &p_tag,
                        int *p_endTagIdx = NULL)
{
    QRegExp beginReg(QString("<%1 ").arg(p_tag));
    QRegExp endReg(QString("</%1>").arg(p_tag));

    int pos = p_pos;
    int nBegin = p_html.indexOf(beginReg, pos);
    int nEnd = p_html.indexOf(endReg, pos);
    if (nBegin > -1 && nBegin < nEnd) {
        // Nested tag.
        pos = skipToTagEnd(p_html, nBegin + beginReg.matchedLength(), p_tag);
        nEnd = p_html.indexOf(endReg, pos);
    }

    if (nEnd > -1) {
        if (p_endTagIdx) {
            *p_endTagIdx = nEnd;
        }

        pos = nEnd + endReg.matchedLength();
    } else if (p_endTagIdx) {
        *p_endTagIdx = -1;
    }

    return pos;
}

// @p_html is the style string.
static bool removeStylesInStyleString(QString &p_html, const QStringList &p_styles)
{
    if (p_styles.isEmpty()) {
        return false;
    }

    int size = p_html.size();
    QRegExp reg(QString("(\\s|^)(%1):[^:]+;").arg(p_styles.join('|')));
    p_html.remove(reg);

    return size != p_html.size();
}

bool VWebUtils::removeBackgroundColor(QString &p_html, const QStringList &p_skipTags)
{
    QStringList styles({"background", "background-color"});

    return removeStyles(p_html, p_skipTags, styles);
}

bool VWebUtils::translateColors(QString &p_html, const QStringList &p_skipTags)
{
    bool changed = false;

    const QHash<QString, QString> &mapping = g_palette->getColorMapping();
    if (mapping.isEmpty()) {
        return changed;
    }

    // Won't mixed up with background-color.
    QRegExp colorReg("(\\s|^)color:([^;]+);");

    int pos = 0;
    while (pos < p_html.size()) {
        int tagIdx = p_html.indexOf(m_tagReg, pos);
        if (tagIdx == -1) {
            break;
        }

        QString tagName = m_tagReg.cap(1);
        if (p_skipTags.contains(tagName.toLower())) {
            // Skip this tag.
            pos = skipToTagEnd(p_html, tagIdx + m_tagReg.matchedLength(), tagName);
            continue;
        }

        pos = tagIdx;
        int idx = p_html.indexOf(m_styleTagReg, pos);
        if (idx == -1) {
            break;
        } else if (idx != tagIdx) {
            pos = tagIdx + m_tagReg.matchedLength();
            continue;
        }

        QString styleStr = m_styleTagReg.cap(3);
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
            // Should not add extra space before :.
            QString newStr = QString("%1color: %2;").arg(colorReg.cap(1)).arg(newCol);
            alteredStyleStr.replace(idxb, colorReg.matchedLength(), newStr);
            posb = idxb + newStr.size();
            changed = true;
        }

        if (changed) {
            QString newTag = QString("<%1%2style=\"%3\"%4>").arg(m_styleTagReg.cap(1))
                                                            .arg(m_styleTagReg.cap(2))
                                                            .arg(alteredStyleStr)
                                                            .arg(m_styleTagReg.cap(4));

            p_html.replace(idx, m_styleTagReg.matchedLength(), newTag);

            pos = idx + newTag.size();
        } else {
            pos = idx + m_styleTagReg.matchedLength();
        }
    }

    return changed;
}

bool VWebUtils::removeMarginPadding(QString &p_html, const QStringList &p_skipTags)
{
    QStringList styles({"margin", "margin-left", "margin-right",
                        "padding", "padding-left", "padding-right"});

    return removeStyles(p_html, p_skipTags, styles);
}

bool VWebUtils::removeStyles(QString &p_html, const QStringList &p_skipTags, const QStringList &p_styles)
{
    if (p_styles.isEmpty()) {
        return false;
    }

    bool altered = false;
    int pos = 0;

    while (pos < p_html.size()) {
        int tagIdx = p_html.indexOf(m_tagReg, pos);
        if (tagIdx == -1) {
            break;
        }

        QString tagName = m_tagReg.cap(1);
        if (p_skipTags.contains(tagName.toLower())) {
            // Skip this tag.
            pos = skipToTagEnd(p_html, tagIdx + m_tagReg.matchedLength(), tagName);
            continue;
        }

        pos = tagIdx;
        int idx = p_html.indexOf(m_styleTagReg, pos);
        if (idx == -1) {
            break;
        } else if (idx != tagIdx) {
            pos = tagIdx + m_tagReg.matchedLength();
            continue;
        }

        QString styleStr = m_styleTagReg.cap(3);
        if (removeStylesInStyleString(styleStr, p_styles)) {
            QString newTag = QString("<%1%2style=\"%3\"%4>").arg(m_styleTagReg.cap(1))
                                                            .arg(m_styleTagReg.cap(2))
                                                            .arg(styleStr)
                                                            .arg(m_styleTagReg.cap(4));
            p_html.replace(idx, m_styleTagReg.matchedLength(), newTag);

            pos = idx + newTag.size();

            altered = true;
        } else {
            pos = idx + m_styleTagReg.matchedLength();
        }
    }

    return altered;
}

bool VWebUtils::removeStylesToRemoveWhenCopied(QString &p_html, const QStringList &p_skipTags)
{
    return removeStyles(p_html, p_skipTags, m_stylesToRemoveWhenCopied);
}

bool VWebUtils::removeAllStyles(QString &p_html, const QStringList &p_skipTags)
{
    bool altered = false;
    int pos = 0;

    while (pos < p_html.size()) {
        int tagIdx = p_html.indexOf(m_tagReg, pos);
        if (tagIdx == -1) {
            break;
        }

        QString tagName = m_tagReg.cap(1);
        if (p_skipTags.contains(tagName.toLower())) {
            // Skip this tag.
            pos = skipToTagEnd(p_html, tagIdx + m_tagReg.matchedLength(), tagName);
            continue;
        }

        pos = tagIdx;
        int idx = p_html.indexOf(m_styleTagReg, pos);
        if (idx == -1) {
            break;
        } else if (idx != tagIdx) {
            pos = tagIdx + m_tagReg.matchedLength();
            continue;
        }

        QString newTag = QString("<%1%2%3>").arg(m_styleTagReg.cap(1))
                                            .arg(m_styleTagReg.cap(2))
                                            .arg(m_styleTagReg.cap(4));
        p_html.replace(idx, m_styleTagReg.matchedLength(), newTag);

        pos = idx + newTag.size();

        altered = true;
    }

    return altered;
}

bool VWebUtils::transformMarkToSpan(QString &p_html)
{
    bool altered = false;
    int pos = 0;

    while (pos < p_html.size()) {
        int tagIdx = p_html.indexOf(m_tagReg, pos);
        if (tagIdx == -1) {
            break;
        }

        QString tagName = m_tagReg.cap(1);
        if (tagName.toLower() != "mark") {
            pos = tagIdx + m_tagReg.matchedLength();
            continue;
        }

        pos = tagIdx;
        int idx = p_html.indexOf(m_styleTagReg, pos);
        if (idx == -1 || idx != tagIdx) {
            // <mark> without "style".
            QString newTag = QString("<span style=\"%1\" %2>").arg(m_styleOfSpanForMark)
                                                              .arg(m_tagReg.cap(2));
            p_html.replace(tagIdx, m_tagReg.matchedLength(), newTag);

            pos = tagIdx + newTag.size();

            altered = true;
            continue;
        }

        QString newTag = QString("<span%1style=\"%2\"%3>").arg(m_styleTagReg.cap(2))
                                                          .arg(m_styleTagReg.cap(3) + m_styleOfSpanForMark)
                                                          .arg(m_styleTagReg.cap(4));
        p_html.replace(idx, m_styleTagReg.matchedLength(), newTag);

        pos = idx + newTag.size();

        altered = true;
    }

    if (altered) {
        // Replace all </mark> with </span>.
        p_html.replace("</mark>", "</span>");
    }

    return altered;
}

bool VWebUtils::replacePreBackgroundColorWithCode(QString &p_html)
{
    if (p_html.isEmpty()) {
        return false;
    }

    bool altered = false;
    int pos = 0;

    QRegExp bgReg("(\\s|^)(background(-color)?:[^;]+;)");

    while (pos < p_html.size()) {
        int tagIdx = p_html.indexOf(m_tagReg, pos);
        if (tagIdx == -1) {
            break;
        }

        QString tagName = m_tagReg.cap(1);
        pos = tagIdx + m_tagReg.matchedLength();
        if (tagName.toLower() != "pre") {
            continue;
        }

        int preEnd = skipToTagEnd(p_html, pos, tagName);

        HtmlTag nextTag = readNextTag(p_html, pos);
        if (nextTag.m_name != "code"
            || nextTag.m_start >= preEnd
            || nextTag.m_style.isEmpty()) {
            continue;
        }

        // Get the background style of <code>.
        int idx = nextTag.m_style.indexOf(bgReg);
        if (idx == -1) {
            continue;
        }

        QString bgStyle = bgReg.cap(2);

        pos = tagIdx;
        idx = p_html.indexOf(m_styleTagReg, pos);
        if (idx == -1 || idx != tagIdx) {
            // <pre> without "style".
            QString newTag = QString("<%1 style=\"%2\" %3>").arg(m_tagReg.cap(1))
                                                            .arg(bgStyle)
                                                            .arg(m_tagReg.cap(2));
            p_html.replace(tagIdx, m_tagReg.matchedLength(), newTag);

            pos = tagIdx + newTag.size();

            altered = true;
            continue;
        }

        QString newTag;
        if (m_styleTagReg.cap(3).indexOf(bgReg) == -1) {
            // No background style specified.
            newTag = QString("<%1%2style=\"%3\"%4>").arg(m_styleTagReg.cap(1))
                                                    .arg(m_styleTagReg.cap(2))
                                                    .arg(m_styleTagReg.cap(3) + bgStyle)
                                                    .arg(m_styleTagReg.cap(4));
        } else {
            // Replace background style.
            newTag = QString("<%1%2style=\"%3\"%4>").arg(m_styleTagReg.cap(1))
                                                    .arg(m_styleTagReg.cap(2))
                                                    .arg(m_styleTagReg.cap(3).replace(bgReg, " " + bgStyle))
                                                    .arg(m_styleTagReg.cap(4));
        }

        p_html.replace(idx, m_styleTagReg.matchedLength(), newTag);

        pos = idx + newTag.size();

        altered = true;
    }

    return altered;
}

VWebUtils::HtmlTag VWebUtils::readNextTag(const QString &p_html, int p_pos)
{
    HtmlTag tag;

    int tagIdx = p_html.indexOf(m_tagReg, p_pos);
    if (tagIdx == -1) {
        return tag;
    }

    tag.m_name = m_tagReg.cap(1);
    tag.m_start = tagIdx;
    tag.m_end = skipToTagEnd(p_html, tagIdx + m_tagReg.matchedLength(), tag.m_name);

    int idx = p_html.indexOf(m_styleTagReg, tagIdx);
    if (idx == -1 || idx != tagIdx) {
        return tag;
    }

    tag.m_style = m_styleTagReg.cap(3);
    return tag;
}

bool VWebUtils::replaceNewLineWithBR(QString &p_html)
{
    if (p_html.isEmpty()) {
        return false;
    }

    bool altered = false;
    int pos = 0;
    const QString brTag("<br/>");

    while (pos < p_html.size()) {
        int tagIdx = p_html.indexOf(m_tagReg, pos);
        if (tagIdx == -1) {
            break;
        }

        QString tagName = m_tagReg.cap(1);
        pos = tagIdx + m_tagReg.matchedLength();
        if (tagName.toLower() != "pre") {
            continue;
        }

        int preEnd = skipToTagEnd(p_html, pos, tagName);

        // Replace '\n' in [pos, preEnd).
        while (pos < preEnd) {
            int idx = p_html.indexOf('\n', pos);
            if (idx == -1 || idx >= preEnd) {
                break;
            }

            p_html.replace(idx, 1, brTag);
            pos = idx + brTag.size() - 1;
            preEnd = preEnd + brTag.size() - 1;

            altered = true;
        }

        pos = preEnd;
    }

    return altered;
}

bool VWebUtils::replaceLocalImgWithWarningLabel(QString &p_html)
{
    bool altered = false;

    QString label = QString("<span style=\"font-weight: bold; color: #FFFFFF; background-color: #EE0000;\">%1</span>")
                           .arg(QObject::tr("Insert_Image_HERE"));

    int pos = 0;
    while (pos < p_html.size()) {
        int idx = p_html.indexOf(m_imgTagReg, pos);
        if (idx == -1) {
            break;
        }

        QString urlStr = m_imgTagReg.cap(1);
        QUrl imgUrl(urlStr);

        if (imgUrl.scheme() == "https" || imgUrl.scheme() == "http") {
            pos = idx + m_imgTagReg.matchedLength();
            continue;
        }

        p_html.replace(idx, m_imgTagReg.matchedLength(), label);
        pos = idx + label.size();

        altered = true;
    }

    return altered;
}

bool VWebUtils::addSpanInsideCode(QString &p_html)
{
    bool altered = false;
    int pos = 0;

    while (pos < p_html.size()) {
        int tagIdx = p_html.indexOf(m_tagReg, pos);
        if (tagIdx == -1) {
            break;
        }

        QString tagName = m_tagReg.cap(1);
        QString lowerName = tagName.toLower();
        if (lowerName == "pre") {
            // Skip <pre>.
            pos = skipToTagEnd(p_html, tagIdx + m_tagReg.matchedLength(), tagName);
            continue;
        }

        if (lowerName != "code") {
            pos = tagIdx + m_tagReg.matchedLength();
            continue;
        }

        int idx = tagIdx + m_tagReg.matchedLength() - 1;
        Q_ASSERT(p_html[idx] == '>');
        QString span = QString("><span%1>").arg(m_tagReg.cap(2));
        p_html.replace(idx, 1, span);

        int codeEnd = skipToTagEnd(p_html, idx + span.size(), tagName, &idx);
        Q_ASSERT(idx > -1);
        Q_ASSERT(codeEnd - idx == 7);
        Q_ASSERT(p_html[idx] == '<');
        p_html.replace(idx, 1, "</span><");

        pos = codeEnd;

        altered = true;
    }

    return altered;
}

// @p_html is the style string.
static bool replaceQuoteInFontFamilyInStyleString(QString &p_html)
{
    QRegExp reg("font-family:((&quot;)|[^;])+;");
    int idx = p_html.indexOf(reg);
    if (idx == -1) {
        return false;
    }

    QString quote("&quot;");
    QString family = reg.cap(0);
    if (family.indexOf(quote) == -1) {
        return false;
    }

    QString newFamily = family.replace(quote, "'");
    p_html.replace(idx, reg.matchedLength(), newFamily);
    return true;
}

bool VWebUtils::replaceQuoteInFontFamily(QString &p_html)
{
    bool altered = false;
    int pos = 0;

    while (pos < p_html.size()) {
        int idx = p_html.indexOf(m_styleTagReg, pos);
        if (idx == -1) {
            break;
        }

        QString styleStr = m_styleTagReg.cap(3);
        if (replaceQuoteInFontFamilyInStyleString(styleStr)) {
            QString newTag = QString("<%1%2style=\"%3\"%4>").arg(m_styleTagReg.cap(1))
                                                            .arg(m_styleTagReg.cap(2))
                                                            .arg(styleStr)
                                                            .arg(m_styleTagReg.cap(4));
            p_html.replace(idx, m_styleTagReg.matchedLength(), newTag);

            pos = idx + newTag.size();

            altered = true;
        } else {
            pos = idx + m_styleTagReg.matchedLength();
        }
    }

    return altered;
}

static bool isHeadingTag(const QString &p_tagName)
{
    QString tag = p_tagName.toLower();
    if (!tag.startsWith('h') || tag.size() != 2) {
        return false;
    }

    return tag == "h1"
           || tag == "h2"
           || tag == "h3"
           || tag == "h4"
           || tag == "h5"
           || tag == "h6";
}

bool VWebUtils::replaceHeadingWithSpan(QString &p_html)
{
    bool altered = false;
    int pos = 0;
    QString spanTag("span");

    while (pos < p_html.size()) {
        int tagIdx = p_html.indexOf(m_tagReg, pos);
        if (tagIdx == -1) {
            break;
        }

        QString tagName = m_tagReg.cap(1);
        if (!isHeadingTag(tagName)) {
            pos = tagIdx + m_tagReg.matchedLength();
            continue;
        }

        p_html.replace(tagIdx + 1, 2, spanTag);

        pos = tagIdx + m_tagReg.matchedLength() + spanTag.size() - 2;

        pos = skipToTagEnd(p_html, pos, tagName);

        Q_ASSERT(pos != -1);

        Q_ASSERT(p_html.mid(pos - 3, 2) == tagName);

        p_html.replace(pos - 3, 2, spanTag);

        pos = pos + spanTag.size() - 2;

        altered = true;
    }

    return altered;
}

bool VWebUtils::fixXHtmlTags(QString &p_html)
{
    bool altered = false;

    // <img>.
    int pos = 0;
    const QString legalTag("/>");
    while (pos < p_html.size()) {
        int idx = p_html.indexOf(m_imgTagReg, pos);
        if (idx == -1) {
            break;
        }

        pos = idx + m_imgTagReg.matchedLength();

        Q_ASSERT(p_html[pos - 1] == '>');

        if (p_html.mid(pos - 2, 2) == legalTag) {
            continue;
        }

        p_html.replace(pos - 1, 1, legalTag);
        pos = pos + legalTag.size() - 1;

        altered = true;
    }

    // <br>.
    int size = p_html.size();
    p_html.replace("<br>", "<br/>");
    if (!altered && size != p_html.size()) {
        altered = true;
    }

    return altered;
}

QString VWebUtils::copyResource(const QUrl &p_url, const QString &p_folder) const
{
    Q_ASSERT(!p_url.isRelative());

    QDir dir(p_folder);
    if (!dir.exists()) {
        VUtils::makePath(p_folder);
    }

    QString file = p_url.isLocalFile() ? p_url.toLocalFile() : p_url.toString();
    QString fileName = VUtils::fileNameFromPath(file);
    fileName = VUtils::getFileNameWithSequence(p_folder, fileName, true);
    QString targetFile = dir.absoluteFilePath(fileName);

    bool succ = false;
    if (p_url.scheme() == "https" || p_url.scheme() == "http") {
        // Download it.
        QByteArray data = VDownloader::downloadSync(p_url);
        if (!data.isEmpty()) {
            succ = VUtils::writeFileToDisk(targetFile, data);
        }
    } else if (QFileInfo::exists(file)) {
        // Do a copy.
        succ = VUtils::copyFile(file, targetFile, false);
    }

    return succ ? targetFile : QString();
}

QString VWebUtils::dataURI(const QUrl &p_url, bool p_keepTitle) const
{
    QString uri;
    Q_ASSERT(!p_url.isRelative());
    QString file = p_url.isLocalFile() ? p_url.toLocalFile() : p_url.toString();
    QString suffix(QFileInfo(file).suffix().toLower());

    if (!QImageReader::supportedImageFormats().contains(suffix.toLatin1())) {
        return uri;
    }

    QByteArray data;
    if (p_url.scheme() == "https" || p_url.scheme() == "http") {
        // Download it.
        data = VDownloader::downloadSync(p_url);
    } else if (QFileInfo::exists(file)) {
        QFile fi(file);
        if (fi.open(QIODevice::ReadOnly)) {
            data = fi.readAll();
            fi.close();
        }
    }

    if (data.isEmpty()) {
        return uri;
    }

    if (suffix == "svg") {
        uri = QString("data:image/svg+xml;utf8,%1").arg(QString::fromUtf8(data));
        uri.replace('\r', "").replace('\n', "");

        if (!p_keepTitle) {
            // Remove <title>...</title>.
            QRegExp reg("<title>.*</title>", Qt::CaseInsensitive);
            uri.remove(reg);
        }
    } else {
        uri = QString("data:image/%1;base64,%2").arg(suffix).arg(QString::fromUtf8(data.toBase64()));
    }

    return uri;
}
