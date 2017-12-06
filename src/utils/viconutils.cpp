#include "viconutils.h"

#include <QDebug>
#include <QRegExp>
#include <QByteArray>
#include <QPixMap>
#include <QFileInfo>

#include "vutils.h"

VIconUtils::VIconUtils()
{

}

QIcon VIconUtils::icon(const QString &p_file, const QString &p_fg)
{
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

    return QIcon(pixmap);
}
