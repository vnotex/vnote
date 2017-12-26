#include "vbuttonmenuitem.h"

#include <QAction>
#include <QPaintEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QRect>

VButtonMenuItem::VButtonMenuItem(QAction *p_action, QWidget *p_parent)
    : QPushButton(p_parent),
      m_action(p_action),
      m_decorationWidth(0),
      m_decorationFM(font())
{
    init();
}

VButtonMenuItem::VButtonMenuItem(QAction *p_action, const QString &p_text, QWidget *p_parent)
    : QPushButton(p_text, p_parent),
      m_action(p_action),
      m_decorationWidth(0),
      m_decorationFM(font())
{
    init();
}

VButtonMenuItem::VButtonMenuItem(QAction *p_action,
                                 const QIcon &p_icon,
                                 const QString &p_text,
                                 const QString &p_decorationText,
                                 const QString &p_decorationTextFg,
                                 QWidget *p_parent)
    : QPushButton(p_icon, p_text, p_parent),
      m_action(p_action),
      m_decorationText(p_decorationText),
      m_decorationTextFg(p_decorationTextFg),
      m_decorationWidth(0),
      m_decorationFM(font())
{
    init();
}

void VButtonMenuItem::init()
{
    QFont ft = font();
    ft.setItalic(true);
    ft.setBold(true);
    m_decorationFM = QFontMetrics(ft);

    connect(this, &QPushButton::clicked,
            m_action, &QAction::triggered);
}

void VButtonMenuItem::paintEvent(QPaintEvent *p_event)
{
    QPushButton::paintEvent(p_event);

    if (m_decorationWidth > 0) {
        Q_ASSERT(!m_decorationText.isEmpty());
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QFont font = painter.font();
        font.setItalic(true);
        font.setBold(true);
        painter.setFont(font);

        QPen pen = painter.pen();
        pen.setColor(m_decorationTextFg);
        painter.setPen(pen);

        QRect re = rect();
        re.adjust(re.width() - m_decorationWidth, 0, 0, 0);
        painter.drawText(re, Qt::AlignCenter, m_decorationText);
    }
}

QSize VButtonMenuItem::sizeHint() const
{
    QSize size = QPushButton::sizeHint();
    if (!m_decorationText.isEmpty()) {
        const_cast<VButtonMenuItem *>(this)->m_decorationWidth = 10 + m_decorationFM.width(m_decorationText);
        size.rwidth() += m_decorationWidth;
    }

    return size;
}
