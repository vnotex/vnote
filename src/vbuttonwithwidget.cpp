#include "vbuttonwithwidget.h"

#include <QEvent>
#include <QMouseEvent>
#include <QRect>

VButtonWithWidget::VButtonWithWidget(QWidget *p_parent)
    : QPushButton(p_parent), m_popupWidget(NULL)
{
    init();
}

VButtonWithWidget::VButtonWithWidget(const QString &p_text, QWidget *p_parent)
    : QPushButton(p_text, p_parent), m_popupWidget(NULL)
{
    init();
}

VButtonWithWidget::VButtonWithWidget(const QIcon &p_icon,
                                     const QString &p_text,
                                     QWidget *p_parent)
    : QPushButton(p_icon, p_text, p_parent), m_popupWidget(NULL)
{
    init();
}

VButtonWithWidget::~VButtonWithWidget()
{
    if (m_popupWidget) {
        delete m_popupWidget;
    }
}

void VButtonWithWidget::init()
{
    connect(this, &QPushButton::clicked,
            this, &VButtonWithWidget::showPopupWidget);
}

void VButtonWithWidget::setPopupWidget(QWidget *p_widget)
{
    if (m_popupWidget) {
        delete m_popupWidget;
    }

    m_popupWidget = p_widget;
    if (m_popupWidget) {
        m_popupWidget->hide();
        m_popupWidget->setParent(NULL);

        Qt::WindowFlags flags = Qt::Popup;
        m_popupWidget->setWindowFlags(flags);
        m_popupWidget->setWindowModality(Qt::NonModal);

        // Let popup widget to hide itself if focus lost.
        m_popupWidget->installEventFilter(this);
    }
}

QWidget *VButtonWithWidget::getPopupWidget() const
{
    return m_popupWidget;
}

void VButtonWithWidget::showPopupWidget()
{
    if (m_popupWidget->isVisible()) {
        m_popupWidget->hide();
    } else {
        emit popupWidgetAboutToShow(m_popupWidget);

        // Calculate the position of the popup widget.
        QPoint btnPos = mapToGlobal(QPoint(0, 0));
        int btnWidth = width();

        int popupWidth = btnWidth * 10;
        int popupHeight = height() * 10;
        int popupX = btnPos.x() + btnWidth - popupWidth;
        int popupY = btnPos.y() - popupHeight - 10;

        m_popupWidget->setGeometry(popupX, popupY, popupWidth, popupHeight);
        m_popupWidget->show();
    }
}

bool VButtonWithWidget::eventFilter(QObject *p_obj, QEvent *p_event)
{
    if (p_event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *eve = dynamic_cast<QMouseEvent *>(p_event);
        QPoint clickPos = eve->pos();
        const QRect &rect = m_popupWidget->rect();
        if (!rect.contains(clickPos)) {
            m_popupWidget->hide();
        }
    }

    return QPushButton::eventFilter(p_obj, p_event);
}
