#include <QtWidgets>
#include <QString>
#include <QDebug>
#include <QShortcut>
#include "vcaptain.h"
#include "veditarea.h"
#include "vedittab.h"
#include "vfilelist.h"
#include "vnavigationmode.h"
#include "vconfigmanager.h"

extern VConfigManager *g_config;

VCaptain::VCaptain(QWidget *p_parent)
    : QWidget(p_parent),
      m_mode(CaptainMode::Normal),
      m_widgetBeforeCaptain(NULL),
      m_nextMajorKey('a'),
      m_ignoreFocusChange(false)
{
    connect(qApp, &QApplication::focusChanged,
            this, &VCaptain::handleFocusChanged);

    setWindowFlags(Qt::FramelessWindowHint);

    // Make it as small as possible. This widget will stay at the top-left corner
    // of VMainWindow.
    resize(1, 1);

    // Register Captain mode leader key.
    // This can fix the Input Method blocking issue.
    m_captainModeShortcut = new QShortcut(QKeySequence(g_config->getShortcutKeySequence("CaptainMode")),
                                          this);
    m_captainModeShortcut->setContext(Qt::ApplicationShortcut);
    connect(m_captainModeShortcut, &QShortcut::activated,
            this, &VCaptain::trigger);

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

    if (p_old == this
        && !m_ignoreFocusChange
        && !checkMode(CaptainMode::Normal)) {
        exitCaptainMode();
    }
}

void VCaptain::trigger()
{
    if (!checkMode(CaptainMode::Normal)) {
        exitCaptainMode();
        return;
    }

    triggerCaptainMode();
}

void VCaptain::keyPressEvent(QKeyEvent *p_event)
{
    int key = p_event->key();
    Qt::KeyboardModifiers modifiers = p_event->modifiers();

    Q_ASSERT(!checkMode(CaptainMode::Normal));

    if (VUtils::isMetaKey(key)) {
        QWidget::keyPressEvent(p_event);
        return;
    }

    if (g_config->getMultipleKeyboardLayout()) {
        qDebug() << "Captain mode" << key << p_event->nativeScanCode() << p_event->nativeVirtualKey();
        key = p_event->nativeVirtualKey();
    }

    // Use virtual key here for different layout.
    if (handleKeyPress(key, modifiers)) {
        p_event->accept();
    } else {
        QWidget::keyPressEvent(p_event);
    }
}

bool VCaptain::handleKeyPress(int p_key, Qt::KeyboardModifiers p_modifiers)
{
    bool ret = true;

    m_ignoreFocusChange = true;

    if (checkMode(CaptainMode::Navigation)) {
        ret = handleKeyPressNavigationMode(p_key, p_modifiers);
        m_ignoreFocusChange = false;
        return ret;
    }

    ret = handleKeyPressCaptainMode(p_key, p_modifiers);
    m_ignoreFocusChange = false;
    return ret;
}

bool VCaptain::handleKeyPressNavigationMode(int p_key,
                                            Qt::KeyboardModifiers /* p_modifiers */)
{
    Q_ASSERT(m_mode == CaptainMode::Navigation);
    bool hasConsumed = false;
    bool pending = false;
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
    return true;
}

void VCaptain::triggerNavigationMode()
{
    setMode(CaptainMode::Navigation);

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
}

void VCaptain::restoreFocus()
{
    if (m_widgetBeforeCaptain) {
        m_widgetBeforeCaptain->setFocus();
        m_widgetBeforeCaptain = NULL;
    }
}

void VCaptain::exitCaptainMode()
{
    if (checkMode(CaptainMode::Normal)) {
        return;
    } else if (checkMode(CaptainMode::Navigation)) {
        exitNavigationMode();
    }

    setMode(CaptainMode::Normal);
    m_ignoreFocusChange = false;

    restoreFocus();

    emit captainModeChanged(false);
}

bool VCaptain::registerCaptainTarget(const QString &p_name,
                                     const QString &p_key,
                                     void *p_target,
                                     CaptainFunc p_func)
{
    if (p_key.isEmpty()) {
        return false;
    }

    QString normKey = QKeySequence(p_key).toString();

    if (m_captainTargets.contains(normKey)) {
        return false;
    }

    CaptainModeTarget target(p_name,
                             normKey,
                             p_target,
                             p_func);
    m_captainTargets.insert(normKey, target);

    qDebug() << "registered:" << target.toString() << normKey;

    return true;
}

void VCaptain::triggerCaptainTarget(const QString &p_key)
{
    auto it = m_captainTargets.find(p_key);
    if (it == m_captainTargets.end()) {
        return;
    }

    const CaptainModeTarget &target = it.value();

    qDebug() << "triggered:" << target.toString();

    CaptainData data(m_widgetBeforeCaptain);
    if (!target.m_function(target.m_target, (void *)&data)) {
        m_widgetBeforeCaptain = NULL;
    }
}

bool VCaptain::navigationModeByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VCaptain *obj = static_cast<VCaptain *>(p_target);
    obj->triggerNavigationMode();
    return true;
}

void VCaptain::triggerCaptainMode()
{
    m_widgetBeforeCaptain = QApplication::focusWidget();

    m_ignoreFocusChange = false;

    setMode(CaptainMode::Pending);

    emit captainModeChanged(true);

    // Focus to listen pending key press.
    setFocus();
}

bool VCaptain::handleKeyPressCaptainMode(int p_key,
                                         Qt::KeyboardModifiers p_modifiers)
{
    Q_ASSERT(checkMode(CaptainMode::Pending));
    QString normKey = QKeySequence(p_key | p_modifiers).toString();
    triggerCaptainTarget(normKey);

    if (!checkMode(CaptainMode::Navigation)) {
        exitCaptainMode();
    }

    return true;
}

void VCaptain::setCaptainModeEnabled(bool p_enabled)
{
    m_captainModeShortcut->setEnabled(p_enabled);
}
