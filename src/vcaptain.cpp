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
#include "vnavigationmode.h"

// 3s pending time after the leader keys.
const int c_pendingTime = 3 * 1000;

#if defined(QT_NO_DEBUG)
extern QFile g_logFile;
#endif

VCaptain::VCaptain(VMainWindow *p_parent)
    : QWidget(p_parent), m_mainWindow(p_parent), m_mode(VCaptain::Normal),
      m_widgetBeforeCaptain(NULL), m_nextMajorKey('a'), m_ignoreFocusChange(false)
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

QChar VCaptain::getNextMajorKey()
{
    QChar ret = m_nextMajorKey;
    if (m_nextMajorKey == 'z') {
        m_nextMajorKey = QChar();
    } else if (!m_nextMajorKey.isNull()) {
        m_nextMajorKey = QChar(m_nextMajorKey.toLatin1() + 1);
    }
    return ret;
}

void VCaptain::registerNavigationTarget(VNavigationMode *p_target)
{
    QChar key = getNextMajorKey();
    if (!key.isNull()) {
        p_target->registerNavigation(key);
        m_targets.append(NaviModeTarget(p_target, true));
    }
}

// In pending mode, if user click other widgets, we need to exit Captain mode.
void VCaptain::handleFocusChanged(QWidget *p_old, QWidget * /* p_now */)
{
    if (!m_ignoreFocusChange && p_old == this) {
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
    if (m_mode != VCaptain::Normal) {
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

    if (m_mode == VCaptain::Normal) {
        // Should not in focus while in Normal mode.
        QWidget::keyPressEvent(p_event);
        m_mainWindow->focusNextChild();
        return;
    }

    if (key == Qt::Key_Control || key == Qt::Key_Shift) {
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
    bool ret = true;

    if (p_key == Qt::Key_Escape
        || (p_key == Qt::Key_BracketLeft
            && p_modifiers == Qt::ControlModifier)) {
        goto exit;
    }

    m_ignoreFocusChange = true;

    if (m_mode == VCaptainMode::Navigation) {
        ret = handleKeyPressNavigationMode(p_key, p_modifiers);
        m_ignoreFocusChange = false;
        return ret;
    }

    // In Captain mode, Ctrl key won't make a difference.
    switch (p_key) {
    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
    case Qt::Key_5:
    case Qt::Key_6:
    case Qt::Key_7:
    case Qt::Key_8:
    case Qt::Key_9:
    {
        // Switch to tab <i>.
        VEditWindow *win = m_mainWindow->editArea->getCurrentWindow();
        if (win) {
            int sequence = p_key - Qt::Key_0;
            if (win->activateTab(sequence)) {
                m_widgetBeforeCaptain = NULL;
            }
        }
        break;
    }

    case Qt::Key_0:
    {
        // Alternate the tab.
        VEditWindow *win = m_mainWindow->editArea->getCurrentWindow();
        if (win) {
            if (win->alternateTab()) {
                m_widgetBeforeCaptain = NULL;
            }
        }
        break;
    }

    case Qt::Key_A:
    {
        // Show attachment list of current note.
        m_mainWindow->showAttachmentList();
        break;
    }

    case Qt::Key_D:
        // Locate current tab.
        if (m_mainWindow->locateCurrentFile()) {
            m_widgetBeforeCaptain = NULL;
        }

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
        if (m_mainWindow->m_curFile) {
            m_mainWindow->discardExitAct->trigger();
        }
        break;

    case Qt::Key_R:
    {
        // Remove current window split.
        m_mainWindow->editArea->removeCurrentWindow();

        QWidget *nextFocus = m_mainWindow->editArea->currentEditTab();
        m_widgetBeforeCaptain = nextFocus ? nextFocus : m_mainWindow->getFileList();
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

    case Qt::Key_W:
        // Enter navigation mode.
        triggerNavigationMode();
        m_ignoreFocusChange = false;
        return ret;

    case Qt::Key_X:
    {
        // Close current tab.
        m_mainWindow->closeCurrentFile();

        // m_widgetBeforeCaptain may be the closed tab which will cause crash.
        QWidget *nextFocus = m_mainWindow->editArea->currentEditTab();
        m_widgetBeforeCaptain = nextFocus ? nextFocus : m_mainWindow->getFileList();
        break;
    }

    case Qt::Key_Question:
    {
        // Display shortcuts doc.
        m_mainWindow->shortcutHelp();
        m_widgetBeforeCaptain = NULL;
        break;
    }

#if defined(QT_NO_DEBUG)
    case Qt::Key_Comma:
    {
        // Flush g_logFile.
        g_logFile.flush();
        break;
    }
#endif

    default:
        // Not implemented yet. Just exit Captain mode.
        break;
    }

exit:
    exitCaptainMode();
    restoreFocus();
    return ret;
}

bool VCaptain::handleKeyPressNavigationMode(int p_key,
                                            Qt::KeyboardModifiers /* p_modifiers */)
{
    Q_ASSERT(m_mode == VCaptainMode::Navigation);
    bool hasConsumed = false;
    bool pending = false;
    for (auto &target : m_targets) {
        if (hasConsumed) {
            target.m_available = false;
            target.m_target->hideNavigation();
            continue;
        }
        if (target.m_available) {
            bool succeed = false;
            bool consumed = target.m_target->handleKeyNavigation(p_key, succeed);
            if (consumed) {
                hasConsumed = true;
                if (succeed) {
                    // Exit.
                    m_widgetBeforeCaptain = NULL;
                } else {
                    // Consumed but not succeed. Need more keys.
                    pending = true;
                }
            } else {
                // Do not ask this target any more.
                target.m_available = false;
                target.m_target->hideNavigation();
            }
        }
    }
    if (pending) {
        return true;
    }
    exitCaptainMode();
    restoreFocus();
    return true;
}

void VCaptain::triggerNavigationMode()
{
    m_pendingTimer->stop();
    m_mode = VCaptainMode::Navigation;

    for (auto &target : m_targets) {
        target.m_available = true;
        target.m_target->showNavigation();
    }
}

void VCaptain::exitNavigationMode()
{
    m_mode = VCaptainMode::Normal;

    for (auto &target : m_targets) {
        target.m_available = true;
        target.m_target->hideNavigation();
    }
}

bool VCaptain::eventFilter(QObject *p_obj, QEvent *p_event)
{
    if (m_mode != VCaptain::Normal && p_event->type() == QEvent::Shortcut) {
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
    if (m_mode == VCaptainMode::Navigation) {
        exitNavigationMode();
    }
    m_mode = VCaptain::Normal;
    m_pendingTimer->stop();
    m_ignoreFocusChange = false;

    emit captainModeChanged(false);
}

