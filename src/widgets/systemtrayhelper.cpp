#include "systemtrayhelper.h"

#include <QMenu>
#include <QIcon>
#include <QSystemTrayIcon>
#include <QApplication>

#include <utils/widgetutils.h>
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include "mainwindow.h"
#include "widgetsfactory.h"

using namespace vnotex;

QSystemTrayIcon *SystemTrayHelper::setupSystemTray(MainWindow *p_win)
{
#if defined(Q_OS_MACOS)
    QIcon icon(":/vnotex/data/core/logo/vnote_mono.png");
    icon.setIsMask(true);
#else
    QIcon icon(":/vnotex/data/core/logo/256x256/vnote.png");
#endif

    auto trayIcon = new QSystemTrayIcon(icon, p_win);
    trayIcon->setToolTip(qApp->applicationName());

    MainWindow::connect(trayIcon, &QSystemTrayIcon::activated,
                        p_win, [p_win](QSystemTrayIcon::ActivationReason p_reason) {
                            Q_UNUSED(p_reason);
#if !defined(Q_OS_MACOS)
                            if (p_reason == QSystemTrayIcon::Trigger) {
                                p_win->showMainWindow();
                            }
#endif
                        });

    auto menu = WidgetsFactory::createMenu(p_win);
    trayIcon->setContextMenu(menu);

    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    {
        auto act = menu->addAction(MainWindow::tr("Show Main Window"),
                                   menu,
                                   [p_win]() {
                                       p_win->showMainWindow();
                                   });

        WidgetUtils::addActionShortcutText(act, coreConfig.getShortcut(CoreConfig::Global_WakeUp));
    }

    menu->addSeparator();

    menu->addAction(MainWindow::tr("Quit"),
                    menu,
                    [p_win]() {
                        p_win->quitApp();
                    });

    return trayIcon;
}
