#include "navigationmodeservice.h"

#include <QApplication>
#include <QDebug>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QShortcut>
#include <QWidget>

#include <core/services/configcoreservice.h>
#include <gui/utils/widgetutils.h>
#include <vtextedit/vtextedit.h>

#include <widgets/navigationmode.h>

using namespace vnotex;

NavigationModeService::NavigationModeService(ConfigCoreService &p_configService,
                                             QWidget *p_topLevelWidget, QObject *p_parent)
    : QObject(p_parent) {
  Q_ASSERT(p_topLevelWidget);

  const auto config = p_configService.getConfig();
  const auto keys =
      config.value(QStringLiteral("core")).toObject().value(QStringLiteral("shortcuts")).toObject().value(
          QStringLiteral("navigationMode")).toString();
  auto shortcut =
      WidgetUtils::createShortcut(keys, p_topLevelWidget, Qt::ApplicationShortcut);
  if (!shortcut) {
    qWarning() << "failed to create shortcut for NavigationMode" << keys;
    return;
  }

  connect(shortcut, &QShortcut::activated, this, &NavigationModeService::triggerNavigationMode);
}

void NavigationModeService::registerNavigationTarget(NavigationMode *p_target) {
  auto key = getNextMajorKey();
  if (!key.isNull()) {
    p_target->registerNavigation(key);
    m_targets.push_back(Target(p_target, true));
  } else {
    qWarning() << "failed to register navigation target for no major key available" << p_target;
  }
}

void NavigationModeService::unregisterNavigationTarget(NavigationMode *p_target) {
  if (!p_target) {
    return;
  }

  if (m_activated) {
    exitNavigationMode();
  }

  for (auto it = m_targets.begin(); it != m_targets.end(); ++it) {
    if (it->m_target == p_target) {
      m_targets.erase(it);
      return;
    }
  }
}

QChar NavigationModeService::getNextMajorKey() {
  QChar ret = m_nextMajorKey;
  if (m_nextMajorKey == 'z') {
    m_nextMajorKey = QChar();
  } else if (!m_nextMajorKey.isNull()) {
    m_nextMajorKey = QChar(m_nextMajorKey.toLatin1() + 1);
  }

  return ret;
}

void NavigationModeService::triggerNavigationMode() {
  if (m_targets.isEmpty()) {
    qInfo() << "no navigation target";
    return;
  }

  m_activated = true;

  qApp->installEventFilter(this);

  vte::VTextEdit::forceInputMethodDisabled(true);

  for (auto &target : m_targets) {
    target.m_available = true;
    target.m_target->showNavigation();
  }
}

void NavigationModeService::exitNavigationMode() {
  m_activated = false;

  qApp->removeEventFilter(this);

  vte::VTextEdit::forceInputMethodDisabled(false);

  for (auto &target : m_targets) {
    target.m_available = true;
    target.m_target->hideNavigation();
  }
}

bool NavigationModeService::eventFilter(QObject *p_obj, QEvent *p_event) {
  if (m_activated) {
    switch (p_event->type()) {
    case QEvent::KeyPress: {
      const bool ret = handleKeyPress(static_cast<QKeyEvent *>(p_event));
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

bool NavigationModeService::handleKeyPress(QKeyEvent *p_event) {
  const int key = p_event->key();

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
      const auto sta = target.m_target->handleKeyNavigation(key);
      if (sta.m_isKeyConsumed) {
        hasConsumed = true;
        if (!sta.m_isTargetHit) {
          pending = true;
        }
      } else {
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
