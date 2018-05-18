#include "viconutils.h"

#include <QRegExp>
#include <QByteArray>
#include <QPixmap>
#include <QFileInfo>

#include "vutils.h"

QHash<QString, QIcon> VIconUtils::m_cache;

VIconUtils::VIconUtils()
{
}

QIcon VIconUtils::icon(const QString &p_file, const QString &p_fg, bool p_addDisabled)
{
    const QString key = cacheKey(p_file, p_fg, p_addDisabled);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        return it.value();
    }

    QFileInfo fi(p_file);
    bool isSvg = fi.suffix().toLower() == "svg";
    if (p_fg.isEmpty() || !isSvg) {
        return QIcon(p_file);
    }

    QString content = VUtils::readFileFromDisk(p_file);
    Q_ASSERT(!content.isEmpty());
    // Must have a # to avoid fill="none".
    QRegExp reg("(\\s|\")(fill|stroke)(:|(=\"))#[^#\"\\s]+");
    if (content.indexOf(reg) == -1) {
        return QIcon(p_file);
    }

    content.replace(reg, QString("\\1\\2\\3%1").arg(p_fg));
    QByteArray data = content.toLocal8Bit();
    QPixmap pixmap;
    pixmap.loadFromData(data, "svg");

    // Disabled.
    QPixmap disabledPixmap;
    if (p_addDisabled) {
        content.replace(reg, QString("\\1\\2\\3%1").arg(g_palette->color("icon_disabled_fg")));
        data = content.toLocal8Bit();
        disabledPixmap.loadFromData(data, "svg");
    }

    QIcon icon;
    icon.addPixmap(pixmap, QIcon::Normal);
    if (p_addDisabled) {
        icon.addPixmap(disabledPixmap, QIcon::Disabled);
    }

    // Add to cache.
    m_cache.insert(key, icon);

    return icon;
}
