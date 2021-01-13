#include "theme.h"

#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <QFileInfo>
#include <QJsonDocument>

#include "exception.h"
#include <utils/fileutils.h>
#include <utils/pathutils.h>
#include <utils/utils.h>
#include <utils/widgetutils.h>

using namespace vnotex;

Theme::Theme(const QString &p_themeFolderPath,
             const Metadata &p_metadata,
             const Palette &p_palette)
    : m_themeFolderPath(p_themeFolderPath),
      m_metadata(p_metadata),
      m_palette(p_palette)
{
}

bool Theme::isValidThemeFolder(const QString &p_folder)
{
    QDir dir(p_folder);
    if (!dir.exists()) {
        qWarning() << "theme folder does not exist" << p_folder;
        return false;
    }

    // The Palette file must exist.
    auto file = getFileName(File::Palette);
    if (!dir.exists(file)) {
        qWarning() << "Not a valid theme folder" << p_folder;
        return false;
    }

    return true;
}

QString Theme::getDisplayName(const QString &p_folder, const QString &p_locale)
{
    auto obj = readPaletteFile(p_folder);
    const auto metaObj = obj[QStringLiteral("metadata")].toObject();
    QString prefix("display_name");

    if (!p_locale.isEmpty()) {
        // Check full locale.
        auto fullLocale = QString("%1_%2").arg(prefix, p_locale);
        if (metaObj.contains(fullLocale)) {
            return metaObj.value(fullLocale).toString();
        }

        auto shortLocale = QString("%1_%2").arg(prefix, p_locale.split('_')[0]);
        if (metaObj.contains(shortLocale)) {
            return metaObj.value(shortLocale).toString();
        }
    }

    if (metaObj.contains(prefix)) {
        return metaObj.value(prefix).toString();
    }
    return PathUtils::dirName(p_folder);
}

Theme *Theme::fromFolder(const QString &p_folder)
{
    Q_ASSERT(!p_folder.isEmpty());
    auto obj = readPaletteFile(p_folder);
    auto metadata = readMetadata(obj);
    auto paletteObj = translatePalette(obj);
    return new Theme(p_folder,
                     metadata,
                     paletteObj);
}

Theme::Metadata Theme::readMetadata(const Palette &p_obj)
{
    Metadata data;

    const auto metaObj = p_obj[QStringLiteral("metadata")].toObject();

    data.m_revision = metaObj[QStringLiteral("revision")].toInt();
    data.m_editorHighlightTheme = metaObj[QStringLiteral("editor-highlight-theme")].toString();
    data.m_markdownEditorHighlightTheme = metaObj[QStringLiteral("markdown-editor-highlight-theme")].toString();

    return data;
}

Theme::Palette Theme::translatePalette(const QJsonObject &p_obj)
{
    const QString paletteSection("palette");
    const QString baseSection("base");
    const QString widgetsSection("widgets");

    // @p_palette may contain referenced definitons: derived=@base#sub#sub2.
    Palette palette;

    palette[paletteSection] = p_obj[paletteSection];
    palette[baseSection] = p_obj[baseSection];
    palette[widgetsSection] = p_obj[widgetsSection];

    // Skip paletteSection since it will not contain any reference.

    translatePaletteObject(palette, palette, baseSection);

    translatePaletteObject(palette, palette, widgetsSection);

    return palette;
}

void Theme::translatePaletteObject(const Palette &p_palette,
                                   QJsonObject &p_obj,
                                   const QString &p_key)
{
    int lastUnresolvedRefs = 0;
    while (true)
    {
        auto ret = translatePaletteObjectOnce(p_palette, p_obj, p_key);
        if (!ret.first) {
            break;
        }

        if (ret.second > 0 && ret.second == lastUnresolvedRefs) {
            qWarning() << "found cyclic references in palette definitions" << p_obj[p_key];
            break;
        }
        lastUnresolvedRefs = ret.second;
    }
}

QPair<bool, int> Theme::translatePaletteObjectOnce(const Palette &p_palette,
                                                   QJsonObject &p_obj,
                                                   const QString &p_key)
{
    bool changed = false;
    int unresolvedRefs = 0;

    // May contain referenced definitions: derived=@base#sub#sub2.
    QRegularExpression refRe("\\A@(\\w+(?:#\\w+)*)\\z");
    const int baseCapturedIdx = 1;

    auto obj = p_obj[p_key].toObject();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        auto val = it.value();
        if (val.isString()) {
            // Check if it references to another key.
            auto match = refRe.match(val.toString());
            if (match.hasMatch()) {
                auto refVal = findValueByKeyPath(p_palette, match.captured(baseCapturedIdx));
                if (refVal.isUndefined()) {
                    ++unresolvedRefs;
                    qWarning() << "failed to find palette key" << match.captured(baseCapturedIdx);
                    break;
                } else if (val.toString() == refVal.toString()) {
                    ++unresolvedRefs;
                    qWarning() << "found cyclic references in palette definitions" << it.key() << val.toString();
                    break;
                }

                Q_ASSERT_X(refVal.isString(), "translatePaletteObjectOnce", val.toString().toStdString().c_str());
                it.value() = refVal.toString();
                if (isRef(refVal.toString())) {
                    // It is another ref again.
                    ++unresolvedRefs;
                }
                changed = true;
            }
        } else if (val.isObject()) {
            auto ret = translatePaletteObjectOnce(p_palette, obj, it.key());
            changed = changed || ret.first;
            unresolvedRefs += ret.second;
        } else {
            Q_ASSERT(false);
        }
    }

    if (changed) {
        p_obj[p_key] = obj;
    }
    return qMakePair(changed, unresolvedRefs);
}

QString Theme::fetchQtStyleSheet() const
{
    const auto qtStyleFile = getFile(File::QtStyleSheet);
    if (qtStyleFile.isEmpty()) {
        return "";
    }
    auto style = FileUtils::readTextFile(qtStyleFile);
    translateStyleByPalette(m_palette, style);
    translateUrlToAbsolute(m_themeFolderPath, style);
    translateFontFamilyList(style);
    translateScaledSize(WidgetUtils::calculateScaleFactor(), style);
    return style;
}

void Theme::translateStyleByPalette(const Palette &p_palette, QString &p_style)
{
    QRegularExpression refRe("(\\s|:)@(\\w+(?:#\\w+)*)");
    const int prefixCapturedIdx = 1;
    const int refCapturedIdx = 2;

    int pos = 0;
    QRegularExpressionMatch match;
    while (pos < p_style.size()) {
        int idx = p_style.indexOf(refRe, pos, &match);
        if (idx == -1) {
            break;
        }

        auto name = match.captured(refCapturedIdx);
        auto val = findValueByKeyPath(p_palette, name).toString();
        if (val.isEmpty() || isRef(val)) {
            qWarning() << "failed to translate style" << name << val;
            pos = idx + match.capturedLength();
        } else {
            pos = idx + match.capturedLength() + val.size() - (name.size() + 1);
            p_style.replace(idx + match.captured(prefixCapturedIdx).size(),
                            name.size() + 1,
                            val);
        }
    }
}

void Theme::translateUrlToAbsolute(const QString &p_basePath, QString &p_style)
{
    QRegularExpression urlRe("(\\s|:)url\\(([^\\(\\)]+)\\)");
    const int prefixCapturedIdx = 1;
    const int urlCapturedIdx = 2;

    QDir dir(p_basePath);
    const int literalSize = QString("url(").size();
    int pos = 0;
    QRegularExpressionMatch match;
    while (pos < p_style.size()) {
        int idx = p_style.indexOf(urlRe, pos, &match);
        if (idx == -1) {
            break;
        }

        auto url = match.captured(urlCapturedIdx);
        if (QFileInfo(url).isRelative()) {
            auto absoluteUrl = dir.filePath(url);
            pos = idx + match.capturedLength() + absoluteUrl.size() - url.size();
            p_style.replace(idx + match.captured(prefixCapturedIdx).size() + literalSize,
                            url.size(),
                            absoluteUrl);
        } else {
            pos = idx + match.capturedLength();
        }
    }
}

void Theme::translateFontFamilyList(QString &p_style)
{
    QRegularExpression fontRe("(\\s|^)font-family:([^;]+);");
    const int prefixCapturedIdx = 1;
    const int fontCapturedIdx = 2;

    int pos = 0;
    QRegularExpressionMatch match;
    while (pos < p_style.size()) {
        int idx = p_style.indexOf(fontRe, pos, &match);
        if (idx == -1) {
            break;
        }

        auto familyList = match.captured(fontCapturedIdx).trimmed();
        familyList.remove('"');
        auto family = Utils::pickAvailableFontFamily(familyList.split(','));
        if (family.isEmpty()) {
            // Could not find available font. Remove it.
            auto newStr = match.captured(prefixCapturedIdx);
            p_style.replace(idx, match.capturedLength(), newStr);
            pos = idx + newStr.size();
        } else if (family != familyList) {
            if (family.contains(' ')) {
                family = "\"" + family + "\"";
            }

            auto newStr = QString("%1font-family: %2;").arg(match.captured(prefixCapturedIdx), family);
            p_style.replace(idx, match.capturedLength(), newStr);
            pos = idx + newStr.size();
        } else {
            pos = idx + match.capturedLength();
        }
    }
}

void Theme::translateScaledSize(qreal p_factor, QString &p_style)
{
    QRegularExpression scaleRe("(\\s|:)\\$([+-]?)(\\d+)(?=\\D)");
    const int prefixCapturedIdx = 1;
    const int signCapturedIdx = 2;
    const int numCapturedIdx = 3;

    int pos = 0;
    QRegularExpressionMatch match;
    while (pos < p_style.size()) {
        int idx = p_style.indexOf(scaleRe, pos, &match);
        if (idx == -1) {
            break;
        }

        auto numStr = match.captured(numCapturedIdx);
        bool ok = false;
        int val = numStr.toInt(&ok);
        if (!ok) {
            pos = idx + match.capturedLength();
            continue;
        }

        val = val * p_factor + 0.5;
        auto newStr = QString("%1%2%3").arg(match.captured(prefixCapturedIdx),
                                            match.captured(signCapturedIdx),
                                            QString::number(val));
        p_style.replace(idx, match.capturedLength(), newStr);
        pos = idx + newStr.size();
    }
}

QString Theme::paletteColor(const QString &p_name) const
{
    auto val = findValueByKeyPath(m_palette, p_name).toString();
    if (!val.isEmpty() && !isRef(val)) {
        return val;
    }
    qWarning() << "undefined or invalid palette color" << p_name;
    return QString("#ff0000");
}

QJsonObject Theme::readJsonFile(const QString &p_filePath)
{
    auto bytes = FileUtils::readFile(p_filePath);
    return QJsonDocument::fromJson(bytes).object();
}

QJsonObject Theme::readPaletteFile(const QString &p_folder)
{
    auto obj = readJsonFile(QDir(p_folder).filePath(getFileName(File::Palette)));
    return obj;
}

QJsonValue Theme::findValueByKeyPath(const Palette &p_palette, const QString &p_keyPath)
{
    auto keys = p_keyPath.split('#');
    Q_ASSERT(!keys.isEmpty());
    if (keys.size() == 1) {
        return p_palette[keys.first()];
    }

    auto obj = p_palette;
    for (int i = 0; i < keys.size() - 1; ++i) {
        obj = obj[keys[i]].toObject();
    }

    return obj[keys.last()];
}

bool Theme::isRef(const QString &p_str)
{
    return p_str.startsWith('@');
}

QString Theme::getFile(File p_fileType) const
{
    return getFile(m_themeFolderPath, p_fileType);
}

QString Theme::getFile(const QString &p_themeFolder, File p_fileType)
{
    QDir dir(p_themeFolder);
    if (dir.exists(getFileName(p_fileType))) {
        return dir.filePath(getFileName(p_fileType));
    } else if (p_fileType == File::MarkdownEditorStyle) {
        // Fallback to text editor style.
        if (dir.exists(getFileName(File::TextEditorStyle))) {
            return dir.filePath(getFileName(File::TextEditorStyle));
        }
    }
    return "";
}

QString Theme::getFileName(File p_fileType)
{
    switch (p_fileType) {
    case File::Palette:
        return QStringLiteral("palette.json");
    case File::QtStyleSheet:
        return QStringLiteral("interface.qss");
    case File::WebStyleSheet:
        return QStringLiteral("web.css");
    case File::HighlightStyleSheet:
        return QStringLiteral("highlight.css");
    case File::TextEditorStyle:
        return QStringLiteral("text-editor.theme");
    case File::MarkdownEditorStyle:
        return QStringLiteral("markdown-text-editor.theme");
    case File::EditorHighlightStyle:
        return QStringLiteral("editor-highlight.theme");
    case File::MarkdownEditorHighlightStyle:
        return QStringLiteral("markdown-editor-highlight.theme");
    case File::Cover:
        return QStringLiteral("cover.png");
    default:
        Q_ASSERT(false);
        return "";
    }
}

QString Theme::getEditorHighlightTheme() const
{
    auto file = getFile(File::EditorHighlightStyle);
    if (file.isEmpty()) {
        return m_metadata.m_editorHighlightTheme;
    } else {
        return file;
    }
}

QString Theme::getMarkdownEditorHighlightTheme() const
{
    auto file = getFile(File::MarkdownEditorHighlightStyle);
    if (!file.isEmpty()) {
        return file;
    }

    if (!m_metadata.m_markdownEditorHighlightTheme.isEmpty()) {
        return m_metadata.m_markdownEditorHighlightTheme;
    }

    return getEditorHighlightTheme();
}

QString Theme::name() const
{
    return PathUtils::dirName(m_themeFolderPath);
}

QPixmap Theme::getCover(const QString &p_folder)
{
    QDir dir(p_folder);
    if (dir.exists(getFileName(File::Cover))) {
        const auto coverFile = dir.filePath(getFileName(File::Cover));
        return QPixmap(coverFile);
    }
    return QPixmap();
}
