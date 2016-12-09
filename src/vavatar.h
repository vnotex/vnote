#ifndef VAVATAR_H
#define VAVATAR_H

#include <QWidget>
#include <QString>
#include <QColor>
#include <QPixmap>

class QLabel;

class VAvatar : public QWidget
{
    Q_OBJECT
public:
    explicit VAvatar(QWidget *p_parent = 0);
    void setDiameter(int p_diameter);
    void setAvatarPixmap(const QString &p_avatarPixmap);
    void setAvatarText(const QString &p_avatarText);
    void setColor(const QString &p_baseColor, const QString &p_fgColor, const QString &p_bgColor);
    QSize sizeHint() const Q_DECL_OVERRIDE;

protected:
    void paintEvent(QPaintEvent *event) Q_DECL_OVERRIDE;

private:
    void drawPixmap(QPainter &p_painter);
    void drawText(QPainter &p_painter, int p_fontPixcel);
    void drawBorder(QPainter &p_painter, const QPainterPath &p_path);

    // Draw a pixmap or characters.
    QString m_avatarPixmap;
    QString m_avatarText;
    int m_diameter;
    QColor m_baseColor;
    QColor m_fgColor;
    QColor m_bgColor;
    int m_borderWidth;
    QPixmap m_pixmap;
};

#endif // VAVATAR_H
