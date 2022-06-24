#include "iconutils.h"

#include <QRegExp>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
#include <QDebug>

#include <core/vnotex.h>

#include "fileutils.h"

using namespace vnotex;

QString IconUtils::s_defaultIconForeground;

QString IconUtils::s_defaultIconDisabledForeground;

QIcon IconUtils::fetchIcon(const QString &p_iconFile,
                           const QVector<OverriddenColor> &p_overriddenColors,
                           qreal p_angle)
{
    const auto suffix = QFileInfo(p_iconFile).suffix().toLower().toStdString();
    if ((p_overriddenColors.isEmpty() || suffix != "svg")
            && VNoteX::getInst().getThemeMgr().getIconMonochrome()) {
        return QIcon(p_iconFile);
    }

    auto content = FileUtils::readTextFile(p_iconFile);
    if (content.isEmpty()) {
        return QIcon();
    }

    QIcon icon;
    for (const auto &color : p_overriddenColors) {
        auto overriddenContent = replaceForegroundOfIcon(content, color.m_foreground);
        auto data = overriddenContent.toLocal8Bit();
        QPixmap pixmap;
        pixmap.loadFromData(data, suffix.c_str());
        if (p_angle > 0) {
            pixmap = pixmap.transformed(QTransform().rotate(p_angle));
        }
        icon.addPixmap(pixmap, color.m_mode, color.m_state);
    }

    return icon;
}

QIcon IconUtils::fetchIcon(const QString &p_iconFile, const QString &p_overriddenForeground)
{
    QVector<OverriddenColor> colors;
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();

    if (!p_overriddenForeground.isEmpty() && themeMgr.getIconMonochrome()) {
        colors.push_back(OverriddenColor(p_overriddenForeground, QIcon::Normal, QIcon::Off));
    }

    return fetchIcon(p_iconFile, colors);
}

QString IconUtils::replaceForegroundOfIcon(const QString &p_iconContent, const QString &p_foreground)
{
    if (p_foreground.isEmpty()) {
        return p_iconContent;
    }

    // Negative lookahead to avoid fill="none".
    QRegExp styleRe(R"((\s|"|;)(fill|stroke)(:|(="))(?!none)[^;"]*)");
    if (p_iconContent.indexOf(styleRe) > -1) {
        auto newContent(p_iconContent);
        newContent.replace(styleRe, QString("\\1\\2\\3%1").arg(p_foreground));
        return newContent;
    }

    return p_iconContent;
}

QIcon IconUtils::fetchIcon(const QString &p_iconFile)
{
    return fetchIcon(p_iconFile, s_defaultIconForeground);
}

void IconUtils::setDefaultIconForeground(const QString &p_fg, const QString &p_disabledFg)
{
    s_defaultIconForeground = p_fg;
    s_defaultIconDisabledForeground = p_disabledFg;
}

QIcon IconUtils::fetchIconWithDisabledState(const QString &p_iconFile)
{
    QVector<OverriddenColor> colors;
    colors.push_back(OverriddenColor(s_defaultIconForeground, QIcon::Normal, QIcon::Off));
    colors.push_back(OverriddenColor(s_defaultIconDisabledForeground, QIcon::Disabled, QIcon::Off));
    return fetchIcon(p_iconFile, colors);
}

QIcon IconUtils::drawTextIcon(const QString &p_text,
                              const QString &p_fg,
                              const QString &p_border)
{
    const int wid = 64;
    QPixmap pixmap(wid, wid);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    auto pen = painter.pen();
    pen.setColor(p_border);
    pen.setWidth(3);
    painter.setPen(pen);

    painter.drawRoundedRect(4, 4, wid - 8, wid - 8, 8, 8);

    if (!p_text.isEmpty()) {
        pen.setColor(p_fg);
        painter.setPen(pen);

        auto font = painter.font();
        font.setPointSize(36);
        painter.setFont(font);

        auto requriedRect = painter.boundingRect(4, 4, wid - 8, wid - 8,
                                                 Qt::AlignCenter,
                                                 p_text);
        painter.drawText(requriedRect, p_text);
    }

    QIcon icon;
    icon.addPixmap(pixmap);
    return icon;
}
