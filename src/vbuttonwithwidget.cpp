#include "vbuttonwithwidget.h"

#include <QMenu>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QRect>
#include <QPainter>
#include <QPainterPath>
#include <QBrush>

VButtonWithWidget::VButtonWithWidget(QWidget *p_widget,
                                     QWidget *p_parent)
    : QPushButton(p_parent), m_popupWidget(p_widget)
{
    init();
}

VButtonWithWidget::VButtonWithWidget(const QString &p_text,
                                     QWidget *p_widget,
                                     QWidget *p_parent)
    : QPushButton(p_text, p_parent), m_popupWidget(p_widget)
{
    init();
}

VButtonWithWidget::VButtonWithWidget(const QIcon &p_icon,
                                     const QString &p_text,
                                     QWidget *p_widget,
                                     QWidget *p_parent)
    : QPushButton(p_icon, p_text, p_parent), m_popupWidget(p_widget)
{
    init();
}

void VButtonWithWidget::init()
{
    m_popupWidget->setParent(this);

    m_bubbleFg = QColor(Qt::white);
    m_bubbleBg = QColor("#15AE67");

    QMenu *menu = new QMenu(this);
    VButtonWidgetAction *act = new VButtonWidgetAction(m_popupWidget, menu);
    menu->addAction(act);
    connect(menu, &QMenu::aboutToShow,
            this, [this]() {
                emit popupWidgetAboutToShow(m_popupWidget);
            });

    setMenu(menu);

    VButtonPopupWidget *popup = getButtonPopupWidget();
    if (popup) {
        popup->setButton(this);
        setAcceptDrops(popup->isAcceptDrops());
        connect(this, &VButtonWithWidget::popupWidgetAboutToShow,
                this, [this]() {
                    getButtonPopupWidget()->handleAboutToShow();
                });
    }
}

QWidget *VButtonWithWidget::getPopupWidget() const
{
    return m_popupWidget;
}

void VButtonWithWidget::showPopupWidget()
{
    showMenu();
}

void VButtonWithWidget::dragEnterEvent(QDragEnterEvent *p_event)
{
    VButtonPopupWidget *popup = getButtonPopupWidget();
    Q_ASSERT(popup);

    if (popup->handleDragEnterEvent(p_event)) {
        return;
    }

    QPushButton::dragEnterEvent(p_event);
}

void VButtonWithWidget::dropEvent(QDropEvent *p_event)
{
    VButtonPopupWidget *popup = getButtonPopupWidget();
    Q_ASSERT(popup);

    if (popup->handleDropEvent(p_event)) {
        return;
    }

    QPushButton::dropEvent(p_event);
}

void VButtonWithWidget::paintEvent(QPaintEvent *p_event)
{
    QPushButton::paintEvent(p_event);

    if (!isEnabled() || m_bubbleStr.isEmpty()) {
        return;
    }

    QRect re = rect();
    int bubbleWidth = re.width() * 3.0 / 7;
    int x = re.width() - bubbleWidth;
    int y = 0;
    QRect bubbleRect(x, y, bubbleWidth, bubbleWidth);

    QPainter painter(this);
    QPainterPath bgPath;
    bgPath.addEllipse(bubbleRect);
    painter.fillPath(bgPath, m_bubbleBg);

    QFont font = painter.font();
    font.setPixelSize(bubbleWidth / 1.3);
    painter.setFont(font);
    painter.setPen(m_bubbleFg);
    painter.drawText(bubbleRect, Qt::AlignCenter, m_bubbleStr);
}

void VButtonWithWidget::setBubbleNumber(int p_num)
{
    if (p_num < 0) {
        m_bubbleStr.clear();
    } else {
        m_bubbleStr = QString::number(p_num);
    }

    update();
}
