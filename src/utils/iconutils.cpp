#include "iconutils.h"

#include <QRegExp>
#include <QFileInfo>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
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
    if (p_overriddenColors.isEmpty() || suffix != "svg") {
        return QIcon(p_iconFile);
    }

    auto content = FileUtils::readTextFile(p_iconFile);
    if (content.isEmpty()) {
        return QIcon();
    }

    if (!isMonochrome(content)) {
        return QIcon(p_iconFile);
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
    if (!p_overriddenForeground.isEmpty()) {
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

bool IconUtils::isMonochrome(const QString &p_iconContent)
{
    // Match color-hex codes.
    QRegExp monoRe("#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})");

    QString lastColor = "";
    int pos = 0;
    while (pos < p_iconContent.size()) {
        int idx = p_iconContent.indexOf(monoRe, pos);
        if (idx == -1) {
            break;
        }

        auto curColor = monoRe.cap(1).toLower();
        if (curColor.size() == 3) {
            for (int i = curColor.size() - 1; i >= 0; --i) {
                curColor.insert(i, curColor[i]);
            }
        }

        if (lastColor != curColor) {
            if (lastColor.isEmpty()) {
                lastColor = curColor;
            } else {
                return false;
            }
        }

        pos += monoRe.matchedLength();
    }

    return true;
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
    return drawTextRectIcon(p_text, p_fg, "", p_border, 56, 56, 8);
}

QIcon IconUtils::drawTextRectIcon(const QString &p_text,
                                  const QString &p_fg,
                                  const QString &p_bg,
                                  const QString &p_border,
                                  int p_rectWidth,
                                  int p_rectHeight,
                                  int p_rectRadius)
{
    const int wid = 64;
    QPixmap pixmap(wid, wid);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QPainterPath bgPath;
    bgPath.addRoundedRect(QRect((wid - p_rectWidth) / 2, (wid - p_rectHeight) / 2, p_rectWidth, p_rectHeight),
                          p_rectRadius,
                          p_rectRadius);

    if (!p_bg.isEmpty()) {
        painter.fillPath(bgPath, QColor(p_bg));
    }

    const int strokeWidth = 3;

    if (!p_border.isEmpty()) {
        QPen pen(QColor(p_border), strokeWidth);
        painter.setPen(pen);
        painter.drawPath(bgPath);
    }

    if (!p_text.isEmpty()) {
        QPen pen(QColor(p_fg), strokeWidth);
        painter.setPen(pen);

        auto font = painter.font();
        font.setPointSize(36);
        font.setBold(true);
        painter.setFont(font);

        auto requriedRect = painter.boundingRect(bgPath.boundingRect(),
                                                 Qt::AlignCenter,
                                                 p_text);
        painter.drawText(requriedRect, p_text);
    }

    QIcon icon;
    icon.addPixmap(pixmap);
    return icon;
}
