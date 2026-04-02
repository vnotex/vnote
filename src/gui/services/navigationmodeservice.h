#ifndef NAVIGATIONMODESERVICE_H
#define NAVIGATIONMODESERVICE_H

#include <QChar>
#include <QObject>
#include <QVector>

#include "core/noncopyable.h"

class QWidget;
class QKeyEvent;
class QEvent;

namespace vnotex {

class CoreConfig;
class NavigationMode;

class NavigationModeService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  explicit NavigationModeService(const CoreConfig &p_coreConfig, QWidget *p_topLevelWidget,
                                 QObject *p_parent = nullptr);

  void registerNavigationTarget(NavigationMode *p_target);

  void unregisterNavigationTarget(NavigationMode *p_target);

protected:
  bool eventFilter(QObject *p_obj, QEvent *p_event) Q_DECL_OVERRIDE;

private slots:
  void triggerNavigationMode();

private:
  struct Target {
    Target() = default;

    Target(NavigationMode *p_target, bool p_available)
        : m_target(p_target), m_available(p_available) {}

    NavigationMode *m_target = nullptr;

    bool m_available = false;
  };

  QChar getNextMajorKey();

  void exitNavigationMode();

  bool handleKeyPress(QKeyEvent *p_event);

  QChar m_nextMajorKey = 'a';

  QVector<Target> m_targets;

  bool m_activated = false;
};

} // namespace vnotex

#endif // NAVIGATIONMODESERVICE_H
