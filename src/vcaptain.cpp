#include <QtWidgets>
#include <QString>
#include <QTimer>
#include <QDebug>
#include <QShortcut>
#include "vcaptain.h"
#include "vmainwindow.h"
#include "veditarea.h"
#include "vedittab.h"
#include "vfilelist.h"

// 3s pending time after the leader keys.
const int c_pendingTime = 3 * 1000;

VCaptain::VCaptain(VMainWindow *p_parent)
    : QWidget(p_parent), m_mainWindow(p_parent), m_mode(VCaptain::Normal),
      m_widgetBeforeCaptain(NULL)
{
    m_pendingTimer = new QTimer(this);
    m_pendingTimer->setSingleShot(true);
    m_pendingTimer->setInterval(c_pendingTime);
    connect(m_pendingTimer, &QTimer::timeout,
            this, &VCaptain::pendingTimerTimeout);

    connect(qApp, &QApplication::focusChanged,
            this, &VCaptain::handleFocusChanged);

    QShortcut *shortcut = new QShortcut(QKeySequence("Ctrl+E"), this,
                                        Q_NULLPTR, Q_NULLPTR);
    connect(shortcut, &QShortcut::activated,
            this, &VCaptain::trigger);

    qApp->installEventFilter(this);

    setWindowFlags(Qt::FramelessWindowHint);
    // Make it as small as possible. This widget will stay at the top-left corner
    // of VMainWindow.
    resize(1, 1);
}

// In pending mode, if user click other widgets, we need to exit Captain mode.
void VCaptain::handleFocusChanged(QWidget *p_old, QWidget * /* p_now */)
{
    if (p_old == this) {
        exitCaptainMode();
    }
}

void VCaptain::pendingTimerTimeout()
{
    qDebug() << "Captain mode timeout";
    exitCaptainMode();
    restoreFocus();
}

void VCaptain::trigger()
{
    if (m_mode == VCaptain::Pending) {
        return;
    }
    qDebug() << "trigger Captain mode";
    // Focus to listen pending key press.
    m_widgetBeforeCaptain = QApplication::focusWidget();
    setFocus();
    m_mode = VCaptain::Pending;
    m_pendingTimer->stop();
    m_pendingTimer->start();

    emit captainModeChanged(true);
}

void VCaptain::keyPressEvent(QKeyEvent *p_event)
{
    int key = p_event->key();
    Qt::KeyboardModifiers modifiers = p_event->modifiers();
    qDebug() << "VCaptain key pressed" << key << modifiers;

    if (m_mode == VCaptain::Normal) {
        // Should not in focus while in Normal mode.
        QWidget::keyPressEvent(p_event);
        m_mainWindow->focusNextChild();
        return;
    }

    if (key == Qt::Key_Control || key == Qt::Key_Shift) {
        qDebug() << "VCaptain ignore key event";
        QWidget::keyPressEvent(p_event);
        return;
    }

    if (handleKeyPress(key, modifiers)) {
        p_event->accept();
    } else {
        QWidget::keyPressEvent(p_event);
    }
}

bool VCaptain::handleKeyPress(int p_key, Qt::KeyboardModifiers p_modifiers)
{
    qDebug() << "handleKeyPress" << p_key << p_modifiers;
    bool ret = true;

    if (p_key == Qt::Key_Escape
        || (p_key == Qt::Key_BracketLeft
            && p_modifiers == Qt::ControlModifier)) {
        goto exit;
    }

    // In Captain mode, Ctrl key won't make a difference.
    switch (p_key) {
    case Qt::Key_D:
        // Locate current tab.
        m_mainWindow->locateCurrentFile();
        break;

    case Qt::Key_E:
        // Toggle expand view.
        m_mainWindow->expandViewAct->trigger();
        break;

    case Qt::Key_F:
    {
        // Show current window's opened file list.
        VEditWindow *win = m_mainWindow->editArea->getCurrentWindow();
        if (win) {
            if (win->showOpenedFileList()) {
                // showOpenedFileList() already focus the right widget.
                m_widgetBeforeCaptain = NULL;
            }
        }
        break;
    }

    case Qt::Key_H:
    {
        if (p_modifiers & Qt::ShiftModifier) {
            // Move current tab one split left.
            m_mainWindow->editArea->moveCurrentTabOneSplit(false);
        } else {
            // Focus previous window split.
            int idx = m_mainWindow->editArea->focusNextWindow(-1);
            if (idx > -1) {
                m_widgetBeforeCaptain = NULL;
            }
        }
        break;
    }

    case Qt::Key_J:
    {
        // Focus next tab.
        VEditWindow *win = m_mainWindow->editArea->getCurrentWindow();
        if (win) {
            win->focusNextTab(true);
            // focusNextTab() will focus the right widget.
            m_widgetBeforeCaptain = NULL;
        }
        break;
    }

    case Qt::Key_K:
    {
        // Focus previous tab.
        VEditWindow *win = m_mainWindow->editArea->getCurrentWindow();
        if (win) {
            win->focusNextTab(false);
            // focusNextTab() will focus the right widget.
            m_widgetBeforeCaptain = NULL;
        }
        break;
    }

    case Qt::Key_L:
    {
        if (p_modifiers & Qt::ShiftModifier) {
            // Move current tab one split right.
            m_mainWindow->editArea->moveCurrentTabOneSplit(true);
        } else {
            // Focus next window split.
            int idx = m_mainWindow->editArea->focusNextWindow(1);
            if (idx > -1) {
                m_widgetBeforeCaptain = NULL;
            }
        }
        break;
    }

    case Qt::Key_P:
        // Toggle one/two panel view.
        m_mainWindow->toggleOnePanelView();
        break;

    case Qt::Key_Q:
        // Discard changes and exit edit mode.
        m_mainWindow->discardExitAct->trigger();
        break;

    case Qt::Key_R:
    {
        // Remove current window split.
        m_mainWindow->editArea->removeCurrentWindow();

        QWidget *nextFocus = m_mainWindow->editArea->currentEditTab();
        m_widgetBeforeCaptain = nextFocus ? nextFocus : m_mainWindow->fileList;
        break;
    }

    case Qt::Key_T:
        // Toggle the Tools dock.
        m_mainWindow->toolDock->setVisible(!m_mainWindow->toolDock->isVisible());
        break;

    case Qt::Key_V:
        // Vertical split current window.
        m_mainWindow->editArea->splitCurrentWindow();
        // Do not restore focus.
        m_widgetBeforeCaptain = NULL;

        break;

    case Qt::Key_X:
    {
        // Close current tab.
        m_mainWindow->closeCurrentFile();

        // m_widgetBeforeCaptain may be the closed tab which will cause crash.
        QWidget *nextFocus = m_mainWindow->editArea->currentEditTab();
        m_widgetBeforeCaptain = nextFocus ? nextFocus : m_mainWindow->fileList;
        break;
    }

    default:
        // Not implemented yet. Just exit Captain mode.
        break;
    }

exit:
    exitCaptainMode();
    restoreFocus();
    return ret;
}

bool VCaptain::eventFilter(QObject *p_obj, QEvent *p_event)
{
    if (m_mode == VCaptain::Pending && p_event->type() == QEvent::Shortcut) {
        qDebug() << "filter" << p_event;
        QShortcutEvent *keyEve = dynamic_cast<QShortcutEvent *>(p_event);
        Q_ASSERT(keyEve);
        const QKeySequence &keys = keyEve->key();
        if (keys.count() == 1) {
            int key = keys[0];
            Qt::KeyboardModifiers modifiers = Qt::KeyboardModifiers(key & (~0x01FFFFFFU));
            key &= 0x01FFFFFFUL;
            if (handleKeyPress(key, modifiers)) {
                return true;
            }
        }
        exitCaptainMode();
        restoreFocus();
    }
    return QWidget::eventFilter(p_obj, p_event);
}

void VCaptain::restoreFocus()
{
    if (m_widgetBeforeCaptain) {
        m_widgetBeforeCaptain->setFocus();
    }
}

void VCaptain::exitCaptainMode()
{
    m_mode = VCaptain::Normal;
    m_pendingTimer->stop();

    emit captainModeChanged(false);
}

