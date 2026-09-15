#include "systemtrayhelper.h"

#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
#include <QPointer>
#include <QSystemTrayIcon>

#include "mainwindow2.h"
#include "viewarea2.h"
#include "widgetsfactory.h"
#include <controllers/viewareacontroller.h>
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/sessionconfig.h>
#include <utils/widgetutils.h>

using namespace vnotex;

QSystemTrayIcon *SystemTrayHelper::setupSystemTray(MainWindow2 *p_win,
                                                   const ConfigMgr2 *p_configMgr) {
#if defined(Q_OS_MACOS)
  QIcon icon(":/vnotex/data/core/logo/vnote_mono.png");
  icon.setIsMask(true);
#else
  QIcon icon(":/vnotex/data/core/logo/256x256/vnote.png");
#endif

  auto trayIcon = new QSystemTrayIcon(icon, p_win);
  trayIcon->setToolTip(qApp->applicationDisplayName());

  MainWindow2::connect(trayIcon, &QSystemTrayIcon::activated, p_win,
                       [p_win](QSystemTrayIcon::ActivationReason p_reason) {
                         Q_UNUSED(p_reason);
#if !defined(Q_OS_MACOS)
                         if (p_reason == QSystemTrayIcon::Trigger) {
                           p_win->showMainWindow();
                         }
#endif
                       });

  auto menu = WidgetsFactory::createMenu(p_win);
  trayIcon->setContextMenu(menu);

  const auto &coreConfig = p_configMgr->getCoreConfig();

  {
    auto act = menu->addAction(MainWindow2::tr("Show Main Window"), menu,
                               [p_win]() { p_win->showMainWindow(); });

    WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::Global_WakeUp));
  }

  {
    auto *act = menu->addAction(MainWindow2::tr("Quick Note"));
    WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::NewQuickNote));
    act->setEnabled(false);
    MainWindow2::connect(p_win->getViewArea(), &ViewArea2::corePropagationReady, act,
                         [act]() { act->setEnabled(true); });
    MainWindow2::connect(act, &QAction::triggered, p_win, [p_win, p_configMgr, act]() {
      QPointer<QAction> actionGuard(act);
      act->setEnabled(false);
      if (p_configMgr->getSessionConfig().getQuickNoteSchemes().isEmpty()) {
        p_win->showMainWindow();
      }
      p_win->getViewArea()->getController()->requestQuickNote();
      if (actionGuard) {
        actionGuard->setEnabled(true);
      }
    });
  }

  menu->addSeparator();

  menu->addAction(MainWindow2::tr("Quit"), menu, [p_win]() { p_win->quitApp(); });

  return trayIcon;
}
