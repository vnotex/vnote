#include "titletoolbar.h"

#include <QDebug>
#include <QMouseEvent>
#include <QCoreApplication>
#include <QToolButton>

#include "propertydefs.h"

using namespace vnotex;

TitleToolBar::TitleToolBar(QWidget *p_parent)
    : QToolBar(p_parent),
      m_window(p_parent)
{
    setupUI();
    m_window->installEventFilter(this);
}

TitleToolBar::TitleToolBar(const QString &p_title, QWidget *p_parent)
    : QToolBar(p_title, p_parent),
      m_window(p_parent)
{
    setupUI();
    m_window->installEventFilter(this);
}

void TitleToolBar::setupUI()
{
}

void TitleToolBar::mousePressEvent(QMouseEvent *p_event)
{
    QToolBar::mousePressEvent(p_event);
    m_lastPos = p_event->pos();
}

void TitleToolBar::mouseDoubleClickEvent(QMouseEvent *p_event)
{
    QToolBar::mouseDoubleClickEvent(p_event);
    m_ignoreNextMove = true;
    maximizeRestoreWindow();
}

void TitleToolBar::maximizeRestoreWindow()
{
    m_window->isMaximized() ? m_window->showNormal() : m_window->showMaximized();
}

void TitleToolBar::mouseMoveEvent(QMouseEvent *p_event)
{
    auto delta = p_event->pos() - m_lastPos;
    if (!m_ignoreNextMove && !m_lastPos.isNull() && (qAbs(delta.x()) > 10 || qAbs(delta.y()) > 10)) {
        if (m_window->isMaximized()) {
            m_window->showNormal();
        } else {
            m_window->move(p_event->globalPos() - m_lastPos);
        }
    }
    QToolBar::mouseMoveEvent(p_event);
}

void TitleToolBar::mouseReleaseEvent(QMouseEvent *p_event)
{
    QToolBar::mouseReleaseEvent(p_event);
    m_ignoreNextMove = false;
    m_lastPos = QPoint();
}

void TitleToolBar::addTitleBarIcons(const QIcon &p_minimizeIcon,
                                    const QIcon &p_maximizeIcon,
                                    const QIcon &p_restoreIcon,
                                    const QIcon &p_closeIcon)
{
    addSeparator();

    addAction(p_minimizeIcon, tr("Minimize"),
              this, [this]() {
                  m_window->showMinimized();
              });

    m_maximizeIcon = p_maximizeIcon;
    m_restoreIcon = p_restoreIcon;
    m_maximizeAct = addAction(p_maximizeIcon, tr("Maximize"),
                              this, [this]() {
                                  maximizeRestoreWindow();
                              });

    {
        auto closeAct = addAction(p_closeIcon, tr("Close"),
                                  this, [this]() {
                                      m_window->close();
                                  });
        auto btn = static_cast<QToolButton *>(widgetForAction(closeAct));
        btn->setProperty(PropertyDefs::s_dangerButton, true);
    }

    updateMaximizeAct();
}

bool TitleToolBar::eventFilter(QObject *p_obj, QEvent *p_event)
{
    if (p_obj == m_window) {
        if (p_event->type() == QEvent::WindowStateChange) {
            updateMaximizeAct();
        }
    }
    return QToolBar::eventFilter(p_obj, p_event);
}

void TitleToolBar::updateMaximizeAct()
{
    if (m_window->isMaximized()) {
        m_maximizeAct->setIcon(m_restoreIcon);
        m_maximizeAct->setText(tr("Restore Down"));
    } else {
        m_maximizeAct->setIcon(m_maximizeIcon);
        m_maximizeAct->setText(tr("Maximize"));
    }
}
