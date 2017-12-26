#ifndef VBUTTONMENUITEM_H
#define VBUTTONMENUITEM_H

#include <QPushButton>
#include <QColor>
#include <QFontMetrics>

class QAction;
class QPaintEvent;


class VButtonMenuItem : public QPushButton
{
    Q_OBJECT
public:
    explicit VButtonMenuItem(QAction *p_action, QWidget *p_parent = nullptr);

    VButtonMenuItem(QAction *p_action, const QString &p_text, QWidget *p_parent = nullptr);

    VButtonMenuItem(QAction *p_action,
                    const QIcon &p_icon,
                    const QString &p_text,
                    const QString &p_decorationText = QString(),
                    const QString &p_decorationTextFg = QString(),
                    QWidget *p_parent = nullptr);

    QSize sizeHint() const Q_DECL_OVERRIDE;

protected:
    void paintEvent(QPaintEvent *p_event) Q_DECL_OVERRIDE;

private:
    void init();

    QAction *m_action;

    // Decoration text drawn at the right end.
    QString m_decorationText;

    // Decoration text foreground.
    QColor m_decorationTextFg;

    // Width in pixels of the decoration text.
    int m_decorationWidth;

    QFontMetrics m_decorationFM;
};

#endif // VBUTTONMENUITEM_H
