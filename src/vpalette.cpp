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
    initMetaData(&settings, "metadata");
    initPaleteFromSettings(&settings, "phony");
    initPaleteFromSettings(&settings, "widgets");
}

void VPalette::initMetaData(QSettings *p_settings, const QString &p_group)
{
    p_settings->beginGroup(p_group);
    // Qss file.
    QString val = p_settings->value("qss_file").toString();
    if (!val.isEmpty()) {
        m_qssFile = QDir(VUtils::basePathFromPath(m_file)).filePath(val);
        qDebug() << "theme file" << m_file << "qss file" << m_qssFile;
    }

    p_settings->endGroup();
}

void VPalette::initPaleteFromSettings(QSettings *p_settings, const QString &p_group)
{
    QRegExp reg("@(\\w+)");

    p_settings->beginGroup(p_group);
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
                qWarning() << "non-defined reference attribute" << key << "in palette" << p_settings->fileName();
                val.clear();
            }
        }

        m_palette.insert(key, val);
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

void VPalette::fillStyle(QString &p_style) const
{
    // Cap(2) is the string to be replaced.
    QRegExp reg("(\\s|:)@(\\w+)(?=\\W)");

    int pos = 0;
    while (pos < p_style.size()) {
        int idx = p_style.indexOf(reg, pos);
        if (idx == -1) {
            break;
        }

        QString name = reg.cap(2);
        QString val = color(name);

        if (val.isEmpty()) {
            pos = idx + reg.matchedLength();
        } else {
            pos = idx + reg.matchedLength() + val.size() - name.size() - 1;
            p_style.replace(idx + reg.cap(1).size(), name.size() + 1, val);
        }
    }
}

QString VPalette::fetchQtStyleSheet() const
{
    QString style = VUtils::readFileFromDisk(m_qssFile);
    fillStyle(style);
    fillAbsoluteUrl(style);

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
