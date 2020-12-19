#include "systemtrayhelper.h"

#include <QMenu>
#include <QIcon>
#include <QSystemTrayIcon>
#include <QApplication>

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
#if !defined(Q_OS_MACOS)
                            if (p_reason == QSystemTrayIcon::Trigger) {
                                p_win->showMainWindow();
                            }
#endif
                        });

    auto menu = WidgetsFactory::createMenu(p_win);
    trayIcon->setContextMenu(menu);

    menu->addAction(MainWindow::tr("Show Main Window"),
                    menu,
                    [p_win]() {
                        p_win->showMainWindow();
                    });

    menu->addAction(MainWindow::tr("Quit"),
                    menu,
                    [p_win]() {
                        p_win->quitApp();
                    });

    return trayIcon;
}
