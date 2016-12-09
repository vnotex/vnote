#include "vavatar.h"
#include <QLabel>
#include <QPixmap>
#include <QHBoxLayout>
#include <QPainter>
#include <QBrush>
#include <QDebug>

VAvatar::VAvatar(QWidget *p_parent)
    : QWidget(p_parent, Qt::FramelessWindowHint | Qt::WindowSystemMenuHint),
      m_avatarText("VN"), m_diameter(48), m_borderWidth(3)
{
    resize(m_diameter, m_diameter);
}

void VAvatar::paintEvent(QPaintEvent *p_event)
{
    int diameter = width();
    int x = diameter / 2;
    int y = x + 1;

    // Border
    QPainterPath path;
    path.addEllipse(QPoint(x, y), x - m_borderWidth, y - m_borderWidth);

    QPainter painter(this);
    painter.setClipPath(path);
    painter.setClipping(true);
    painter.setRenderHint(QPainter::Antialiasing);

    if (!m_avatarPixmap.isEmpty()) {
        drawPixmap(painter);
    } else {
        drawText(painter, x);
    }
    drawBorder(painter, path);
}

void VAvatar::drawPixmap(QPainter &p_painter)
{
    p_painter.drawPixmap(rect(), m_pixmap);
}

void VAvatar::drawText(QPainter &p_painter, int p_fontPixcel)
{
    p_painter.save();
    QFont font = p_painter.font();
    font.setPixelSize(p_fontPixcel);
    p_painter.setPen(m_fgColor);
    p_painter.setFont(font);
    p_painter.drawText(rect(), Qt::AlignCenter, m_avatarText);
    p_painter.restore();
}

void VAvatar::drawBorder(QPainter &p_painter, const QPainterPath &p_path)
{
    p_painter.save();
    p_painter.setClipping(false);
    QPen borderPen(m_baseColor);
    borderPen.setWidth(m_borderWidth);
    p_painter.setPen(borderPen);
    p_painter.drawPath(p_path);
    p_painter.restore();
}

void VAvatar::setDiameter(int p_diameter)
{
    if (m_diameter == p_diameter) {
        return;
    }
    m_diameter = p_diameter;
    resize(m_diameter, m_diameter);
}

void VAvatar::setAvatarText(const QString &p_avatarText)
{
    if (m_avatarText == p_avatarText) {
        return;
    }
    m_avatarText = p_avatarText.left(2);
    m_avatarPixmap.clear();
    update();
}

void VAvatar::setAvatarPixmap(const QString &p_avatarPixmap)
{
    if (m_avatarPixmap == p_avatarPixmap) {
        return;
    }
    m_avatarPixmap = p_avatarPixmap;
    m_pixmap = QPixmap(m_avatarPixmap);
    m_avatarText.clear();
    update();
}

QSize VAvatar::sizeHint() const
{
    return QSize(m_diameter, m_diameter);
}

void VAvatar::setColor(const QString &p_baseColor, const QString &p_fgColor, const QString &p_bgColor)
{
    m_baseColor.setNamedColor(p_baseColor);
    m_fgColor.setNamedColor(p_fgColor);
    m_bgColor.setNamedColor(p_bgColor);
}

