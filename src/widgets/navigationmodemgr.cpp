#include "navigationmodemgr.h"

#include <QShortcut>
#include <QDebug>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>

#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <utils/widgetutils.h>
#include "navigationmode.h"

using namespace vnotex;

QWidget *NavigationModeMgr::s_widget = nullptr;

NavigationModeMgr::NavigationModeMgr()
    : QObject(nullptr)
{
    Q_ASSERT(s_widget);
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
    auto keys = coreConfig.getShortcut(CoreConfig::Shortcut::NavigationMode);
    auto shortcut = WidgetUtils::createShortcut(keys, s_widget, Qt::ApplicationShortcut);
    if (!shortcut) {
        qWarning() << "failed to create shortcut for NavigationMode" << keys;
        return;
    }
    connect(shortcut, &QShortcut::activated,
            this, &NavigationModeMgr::triggerNavigationMode);
}

NavigationModeMgr::~NavigationModeMgr()
{
}

NavigationModeMgr &NavigationModeMgr::getInst()
{
    static NavigationModeMgr mgr;
    return mgr;
}

void NavigationModeMgr::registerNavigationTarget(NavigationMode *p_target)
{
    auto key = getNextMajorKey();
    if (!key.isNull()) {
        p_target->registerNavigation(key);
        m_targets.push_back(Target(p_target, true));
    } else {
        qWarning() << "failed to register navigation target for no major key available" << p_target;
    }
}

QChar NavigationModeMgr::getNextMajorKey()
{
    QChar ret = m_nextMajorKey;
    if (m_nextMajorKey == 'z') {
        m_nextMajorKey = QChar();
    } else if (!m_nextMajorKey.isNull()) {
        m_nextMajorKey = QChar(m_nextMajorKey.toLatin1() + 1);
    }
    return ret;
}

void NavigationModeMgr::triggerNavigationMode()
{
    if (m_targets.isEmpty()) {
        qInfo() << "no navigation target";
        return;
    }

    m_activated = true;

    qApp->installEventFilter(this);

    for (auto &target : m_targets) {
        target.m_available = true;
        target.m_target->showNavigation();
    }
}

void NavigationModeMgr::init(QWidget *p_widget)
{
    s_widget = p_widget;
}

void NavigationModeMgr::exitNavigationMode()
{
    m_activated = false;
    qApp->removeEventFilter(this);

    for (auto &target : m_targets) {
        target.m_available = true;
        target.m_target->hideNavigation();
    }
}

bool NavigationModeMgr::eventFilter(QObject *p_obj, QEvent *p_event)
{
    if (m_activated) {
        switch (p_event->type()) {
        case QEvent::KeyPress:
        {
            bool ret = handleKeyPress(static_cast<QKeyEvent *>(p_event));
            if (ret) {
                return true;
            }
            break;
        }

        case QEvent::MouseButtonPress:
            exitNavigationMode();
            break;

        default:
            break;
        }
    }

    return QObject::eventFilter(p_obj, p_event);
}

bool NavigationModeMgr::handleKeyPress(QKeyEvent *p_event)
{
    int key = p_event->key();

    if (WidgetUtils::isMetaKey(key)) {
        return false;
    }

    bool hasConsumed = false;
    bool pending = false;
    for (auto &target : m_targets) {
        if (hasConsumed) {
            target.m_available = false;
            target.m_target->hideNavigation();
            continue;
        }
        if (target.m_available) {
            auto sta = target.m_target->handleKeyNavigation(key);
            if (sta.m_isKeyConsumed) {
                hasConsumed = true;
                if (!sta.m_isTargetHit) {
                    // Consumed but not hit. Need more keys.
                    pending = true;
                }
            } else {
                // Do not ask this target any more.
                target.m_available = false;
                target.m_target->hideNavigation();
            }
        }
    }

    if (!pending) {
        exitNavigationMode();
    }

    return true;
}
