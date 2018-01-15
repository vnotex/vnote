#include "vpalette.h"

#include <QSettings>
#include <QRegExp>
#include <QFileInfo>
#include <QDir>
#include <QDebug>

#include "utils/vutils.h"

VPalette::VPalette(const QString &p_file)
{
    init(p_file);
}

void VPalette::init(const QString &p_file)
{
    m_file = QFileInfo(p_file).absoluteFilePath();

    QSettings settings(p_file, QSettings::IniFormat);
    m_data = getPaletteMetaData(m_file);
    initPaleteFromSettings(&settings, "phony");
    initPaleteFromSettings(&settings, "soft_defined");
    initPaleteFromSettings(&settings, "widgets");

    qDebug() << "theme file" << m_file << m_data.toString();
}

void VPalette::initPaleteFromSettings(QSettings *p_settings, const QString &p_group)
{
    QRegExp reg("@(\\w+)");

    p_settings->beginGroup(p_group);
    // Used to store undefined pairs.
    QHash<QString, QString> undefined;
    QStringList keys = p_settings->childKeys();
    for (auto const & key : keys) {
        if (key.isEmpty()) {
            continue;
        }

        QString val = p_settings->value(key).toString();
        if (reg.exactMatch(val)) {
            auto it = m_palette.find(reg.cap(1));
            if (it != m_palette.end()) {
                val = it.value();
            } else {
                undefined.insert(key, reg.cap(1));
                continue;
            }
        }

        m_palette.insert(key, val);
    }

    // Handle definition: a=@b b=@c c=red.
    int iter = 0;
    while (!undefined.isEmpty()) {
        if (iter >= undefined.size()) {
            qWarning() << "cyclic palette definitions found" << undefined;
            break;
        }

        for (auto it = undefined.begin(); it != undefined.end();) {
            auto pit = m_palette.find(it.value());
            if (pit != m_palette.end()) {
                m_palette.insert(it.key(), pit.value());
                iter = 0;
                it = undefined.erase(it);
            } else {
                ++iter;
                ++it;
            }
        }
    }

    p_settings->endGroup();
}

QString VPalette::color(const QString &p_name) const
{
    auto it = m_palette.find(p_name);
    if (it != m_palette.end()) {
        return it.value();
    }

    return QString();
}

void VPalette::fillStyle(QString &p_text) const
{
    // Cap(2) is the string to be replaced.
    QRegExp reg("(\\s|:)@(\\w+)(?=\\W)");

    int pos = 0;
    while (pos < p_text.size()) {
        int idx = p_text.indexOf(reg, pos);
        if (idx == -1) {
            break;
        }

        QString name = reg.cap(2);
        QString val = color(name);

        if (val.isEmpty()) {
            pos = idx + reg.matchedLength();
        } else {
            pos = idx + reg.matchedLength() + val.size() - name.size() - 1;
            p_text.replace(idx + reg.cap(1).size(), name.size() + 1, val);
        }
    }
}

QString VPalette::fetchQtStyleSheet() const
{
    QString style = VUtils::readFileFromDisk(m_data.m_qssFile);
    fillStyle(style);
    fillAbsoluteUrl(style);
    fillFontFamily(style);

    return style;
}

void VPalette::fillAbsoluteUrl(QString &p_style) const
{
    // Cap(2) is the string to be replaced.
    QRegExp reg("(\\s|:)url\\(([^\\(\\)]+)\\)(?=\\W)");
    int literalSize = QString("url(").size();
    QDir dir(VUtils::basePathFromPath(m_file));

    int pos = 0;
    while (pos < p_style.size()) {
        int idx = p_style.indexOf(reg, pos);
        if (idx == -1) {
            break;
        }

        QString url = reg.cap(2);
        QString abUrl = dir.filePath(url);
        pos = idx + reg.matchedLength() + abUrl.size() - url.size();
        p_style.replace(idx + reg.cap(1).size() + literalSize, url.size(), abUrl);
    }
}

QMap<QString, QString> VPalette::editorStylesFromThemes(const QList<QString> &p_themeFiles)
{
    QMap<QString, QString> styles;
    for (auto const & theme : p_themeFiles) {
        QString value = getPaletteMetaData(theme).m_mdhlFile;
        if (!value.isEmpty()) {
            styles.insert(themeName(theme) + "/" + QFileInfo(value).completeBaseName(), value);
        }
    }

    return styles;
}

QMap<QString, QString> VPalette::cssStylesFromThemes(const QList<QString> &p_themeFiles)
{
    QMap<QString, QString> styles;
    for (auto const & theme : p_themeFiles) {
        QString value = getPaletteMetaData(theme).m_cssFile;
        if (!value.isEmpty()) {
            styles.insert(themeName(theme) + "/" + QFileInfo(value).completeBaseName(), value);
        }
    }

    return styles;
}

QMap<QString, QString> VPalette::codeBlockCssStylesFromThemes(const QList<QString> &p_themeFiles)
{
    QMap<QString, QString> styles;
    for (auto const & theme : p_themeFiles) {
        QString value = getPaletteMetaData(theme).m_codeBlockCssFile;
        if (!value.isEmpty()) {
            styles.insert(themeName(theme) + "/" + QFileInfo(value).completeBaseName(), value);
        }
    }

    return styles;
}

static QString translateColorValue(const QString &p_col)
{
    QString tmp = p_col.trimmed().toLower();
    if (tmp.startsWith('#')) {
        QColor col(tmp);
        int r, g, b;
        col.getRgb(&r, &g, &b);
        return QString("rgb(%1, %2, %3)").arg(r).arg(g).arg(b);
    } else {
        return tmp;
    }
}

VPaletteMetaData VPalette::getPaletteMetaData(const QString &p_paletteFile)
{
    VPaletteMetaData data;

    QSettings settings(p_paletteFile, QSettings::IniFormat);
    QDir dir(VUtils::basePathFromPath(QFileInfo(p_paletteFile).absoluteFilePath()));

    settings.beginGroup("metadata");
    data.m_version = settings.value("version").toInt();

    QString val = settings.value("qss_file").toString();
    if (!val.isEmpty()) {
        data.m_qssFile = dir.filePath(val);
    }

    val = settings.value("mdhl_file").toString();
    if (!val.isEmpty()) {
        data.m_mdhlFile = dir.filePath(val);
    }

    val = settings.value("css_file").toString();
    if (!val.isEmpty()) {
        data.m_cssFile = dir.filePath(val);
    }

    val = settings.value("codeblock_css_file").toString();
    if (!val.isEmpty()) {
        data.m_codeBlockCssFile = dir.filePath(val);
    }

    QStringList mapping = settings.value("css_color_mapping").toStringList();
    if (!mapping.isEmpty()) {
        for (auto const & m : mapping) {
            QStringList vals = m.split(':');
            if (vals.size() != 2) {
                continue;
            }

            // Translate the #aabbcc into rgb(aa, bb, cc) format.
            data.m_colorMapping.insert(translateColorValue(vals[0]),
                                       translateColorValue(vals[1]));
        }
    }

    settings.endGroup();

    return data;
}

QString VPalette::themeName(const QString &p_paletteFile)
{
    return QFileInfo(p_paletteFile).completeBaseName();
}

QString VPalette::themeEditorStyle(const QString &p_paletteFile)
{
    VPaletteMetaData data = getPaletteMetaData(p_paletteFile);
    return themeName(p_paletteFile) + "/" + QFileInfo(data.m_mdhlFile).completeBaseName();
}

QString VPalette::themeCssStyle(const QString &p_paletteFile)
{
    VPaletteMetaData data = getPaletteMetaData(p_paletteFile);
    return themeName(p_paletteFile) + "/" + QFileInfo(data.m_cssFile).completeBaseName();
}

QString VPalette::themeCodeBlockCssStyle(const QString &p_paletteFile)
{
    VPaletteMetaData data = getPaletteMetaData(p_paletteFile);
    return themeName(p_paletteFile) + "/" + QFileInfo(data.m_codeBlockCssFile).completeBaseName();
}

int VPalette::getPaletteVersion(const QString &p_paletteFile)
{
    return getPaletteMetaData(p_paletteFile).m_version;
}

void VPalette::fillFontFamily(QString &p_text) const
{
    QRegExp reg("(\\s|^)font-family:([^;]+);");

    int pos = 0;
    while (pos < p_text.size()) {
        int idx = p_text.indexOf(reg, pos);
        if (idx == -1) {
            break;
        }

        QString familyList = reg.cap(2).trimmed();
        familyList.remove('"');
        QString family = VUtils::getAvailableFontFamily(familyList.split(','));
        if (!family.isEmpty() && family != familyList) {
            if (family.contains(' ')) {
                family = "\"" + family + "\"";
            }

            QString str = QString("%1font-family: %2;").arg(reg.cap(1)).arg(family);
            p_text.replace(idx, reg.matchedLength(), str);

            pos = idx + str.size();
        } else {
            pos = idx + reg.matchedLength();
        }
    }
}
