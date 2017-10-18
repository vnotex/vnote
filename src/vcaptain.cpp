#include <QtWidgets>
#include <QString>
#include <QDebug>
#include <QShortcut>
#include "vcaptain.h"
#include "vmainwindow.h"
#include "veditarea.h"
#include "vedittab.h"
#include "vfilelist.h"
#include "vnavigationmode.h"
#include "vconfigmanager.h"

extern VMainWindow *g_mainWin;

extern VConfigManager *g_config;

// 3s pending time after the leader keys.
const int c_pendingTime = 3 * 1000;

VCaptain::VCaptain(QWidget *p_parent)
    : QWidget(p_parent),
      m_mode(CaptainMode::Normal),
      m_widgetBeforeNavigation(NULL),
      m_nextMajorKey('a'),
      m_ignoreFocusChange(false),
      m_leaderKey(g_config->getShortcutKeySequence("CaptainMode"))
{
    Q_ASSERT(!m_leaderKey.isEmpty());

    connect(qApp, &QApplication::focusChanged,
            this, &VCaptain::handleFocusChanged);

    setWindowFlags(Qt::FramelessWindowHint);

    // Make it as small as possible. This widget will stay at the top-left corner
    // of VMainWindow.
    resize(1, 1);

    // Register Navigation mode as Captain mode target.
    registerCaptainTarget(tr("NavigationMode"),
                          g_config->getCaptainShortcutKeySequence("NavigationMode"),
                          this,
                          navigationModeByCaptain);
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
        m_naviTargets.push_back(NaviModeTarget(p_target, true));
    }
}

void VCaptain::handleFocusChanged(QWidget *p_old, QWidget * p_now)
{
    Q_UNUSED(p_now);

    if (!m_ignoreFocusChange
        && !checkMode(CaptainMode::Normal)
        && p_old == this) {
        exitNavigationMode();
    }
}

void VCaptain::keyPressEvent(QKeyEvent *p_event)
{
    int key = p_event->key();
    Qt::KeyboardModifiers modifiers = p_event->modifiers();

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
    if (!checkMode(CaptainMode::Navigation)) {
        return false;
    }

    if (p_key == Qt::Key_Escape
        || (p_key == Qt::Key_BracketLeft
            && p_modifiers == Qt::ControlModifier)) {
        exitNavigationMode();
        return true;
    }

    return handleKeyPressNavigationMode(p_key, p_modifiers);
}

bool VCaptain::handleKeyPressNavigationMode(int p_key,
                                            Qt::KeyboardModifiers /* p_modifiers */)
{
    Q_ASSERT(m_mode == CaptainMode::Navigation);
    bool hasConsumed = false;
    bool pending = false;
    m_ignoreFocusChange = true;
    for (auto &target : m_naviTargets) {
        if (hasConsumed) {
            target.m_available = false;
            target.m_target->hideNavigation();
            continue;
        }
        if (target.m_available) {
            bool succeed = false;
            // May change focus, so we need to ignore focus change here.
            bool consumed = target.m_target->handleKeyNavigation(p_key, succeed);
            if (consumed) {
                hasConsumed = true;
                if (succeed) {
                    // Exit.
                    m_widgetBeforeNavigation = NULL;
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

    m_ignoreFocusChange = false;
    if (pending) {
        return true;
    }

    exitNavigationMode();
    return true;
}

void VCaptain::triggerNavigationMode()
{
    setMode(CaptainMode::Navigation);
    m_widgetBeforeNavigation = QApplication::focusWidget();
    // Focus to listen pending key press.
    setFocus();
    for (auto &target : m_naviTargets) {
        target.m_available = true;
        target.m_target->showNavigation();
    }
}

void VCaptain::exitNavigationMode()
{
    setMode(CaptainMode::Normal);

    for (auto &target : m_naviTargets) {
        target.m_available = true;
        target.m_target->hideNavigation();
    }

    restoreFocus();
}

void VCaptain::restoreFocus()
{
    if (m_widgetBeforeNavigation) {
        m_widgetBeforeNavigation->setFocus();
    }
}

bool VCaptain::registerCaptainTarget(const QString &p_name,
                                     const QString &p_key,
                                     void *p_target,
                                     CaptainFunc p_func)
{
    if (p_key.isEmpty()) {
        return false;
    }

    QString lowerKey = p_key.toLower();

    if (m_captainTargets.contains(lowerKey)) {
        return false;
    }

    // Register shortcuts.
    QString sequence = QString("%1,%2").arg(m_leaderKey).arg(p_key);
    QShortcut *shortcut = new QShortcut(QKeySequence(sequence),
                                        this);
    shortcut->setContext(Qt::ApplicationShortcut);

    connect(shortcut, &QShortcut::activated,
            this, std::bind(&VCaptain::triggerCaptainTarget, this, p_key));


    CaptainModeTarget target(p_name,
                             p_key,
                             p_target,
                             p_func,
                             shortcut);
    m_captainTargets.insert(lowerKey, target);

    qDebug() << "registered:" << target.toString() << sequence;

    return true;
}

void VCaptain::triggerCaptainTarget(const QString &p_key)
{
    auto it = m_captainTargets.find(p_key.toLower());
    Q_ASSERT(it != m_captainTargets.end());
    const CaptainModeTarget &target = it.value();

    qDebug() << "triggered:" << target.toString();

    target.m_function(target.m_target, nullptr);
}

void VCaptain::navigationModeByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VCaptain *obj = static_cast<VCaptain *>(p_target);
    obj->triggerNavigationMode();
}
