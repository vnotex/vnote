#include "toolbarhelper.h"
#include <QToolBar>
#include <QToolButton>
#include <QMenu>
#include <QDebug>
#include <QWhatsThis>
#include <QUrl>
#include <QDockWidget>
#include <QApplication>
#include <QDir>
#include <QFileDialog>

#include "mainwindow.h"
#include "vnotex.h"
#include "widgetsfactory.h"
#include <utils/iconutils.h>
#include <utils/widgetutils.h>
#include <utils/docsutils.h>
#include <utils/pathutils.h>
#include "fullscreentoggleaction.h"
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/sessionconfig.h>
#include <core/fileopenparameters.h>
#include "propertydefs.h"
#include "dialogs/settings/settingsdialog.h"
#include "messageboxhelper.h"

using namespace vnotex;

static QToolBar *createToolBar(MainWindow *p_win, const QString &p_title, const QString &p_name)
{
    auto tb = p_win->addToolBar(p_title);
    tb->setObjectName(p_name);
    tb->setMovable(false);
    return tb;
}

QToolBar *ToolBarHelper::setupFileToolBar(MainWindow *p_win, QToolBar *p_toolBar)
{
    auto tb = p_toolBar;
    if (!tb) {
        tb = createToolBar(p_win, MainWindow::tr("File"), "FileToolBar");
    }

    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    // Notebook.
    {
        auto act = tb->addAction(generateIcon("notebook_menu.svg"), MainWindow::tr("Notebook"));

        auto toolBtn = dynamic_cast<QToolButton *>(tb->widgetForAction(act));
        Q_ASSERT(toolBtn);
        toolBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toolBtn->setPopupMode(QToolButton::InstantPopup);
        toolBtn->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);

        auto btnMenu = WidgetsFactory::createMenu(tb);
        toolBtn->setMenu(btnMenu);

        btnMenu->addAction(generateIcon("new_notebook.svg"),
                           MainWindow::tr("New Notebook"),
                           btnMenu,
                           []() {
                               emit VNoteX::getInst().newNotebookRequested();
                           });

        // New notebook from folder.
        btnMenu->addAction(generateIcon("new_notebook_from_folder.svg"),
                           MainWindow::tr("New Notebook From Folder"),
                           btnMenu,
                           []() {
                               emit VNoteX::getInst().newNotebookFromFolderRequested();
                           });

        btnMenu->addSeparator();

        // Import notebook.
        btnMenu->addAction(generateIcon("import_notebook.svg"),
                           MainWindow::tr("Import Notebook"),
                           btnMenu,
                           []() {
                               emit VNoteX::getInst().importNotebookRequested();
                           });

        // Import notebook of VNote 2.0.
        btnMenu->addAction(generateIcon("import_notebook_of_vnote2.svg"),
                           MainWindow::tr("Import Legacy Notebook Of VNote 2.0"),
                           btnMenu,
                           []() {
                               emit VNoteX::getInst().importLegacyNotebookRequested();
                           });
    }

    // New Note.
    {
        auto newBtn = WidgetsFactory::createToolButton(tb);
        newBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

        // Popup menu.
        auto newMenu = WidgetsFactory::createMenu(tb);
        newBtn->setMenu(newMenu);

        // New note.
        auto newNoteAct = newMenu->addAction(generateIcon("new_note.svg"),
                                             MainWindow::tr("New Note"),
                                             newMenu,
                                             []() {
                                                 emit VNoteX::getInst().newNoteRequested();
                                             });
        WidgetUtils::addActionShortcut(newNoteAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::NewNote));
        newBtn->setDefaultAction(newNoteAct);
        // To hide the shortcut text shown in button.
        newBtn->setText(MainWindow::tr("New Note"));

        // New folder.
        newMenu->addAction(generateIcon("new_folder.svg"),
                           MainWindow::tr("New Folder"),
                           newMenu,
                           []() {
                               emit VNoteX::getInst().newFolderRequested();
                           });

        newMenu->addSeparator();

        // Open file.
        newMenu->addAction(MainWindow::tr("Open File"),
                           newMenu,
                           [p_win]() {
                               static QString lastDirPath = QDir::homePath();
                               auto files = QFileDialog::getOpenFileNames(p_win, MainWindow::tr("Open File"), lastDirPath);
                               if (files.isEmpty()) {
                                   return;
                               }

                               lastDirPath = QFileInfo(files[0]).path();

                               for (const auto &file : files) {
                                   emit VNoteX::getInst().openFileRequested(file,
                                                                            QSharedPointer<FileOpenParameters>::create());
                               }
                           });


        tb->addWidget(newBtn);
    }

    // Import and export.
    {
        auto act = tb->addAction(generateIcon("import_export_menu.svg"), MainWindow::tr("Import And Export"));

        auto btn = dynamic_cast<QToolButton *>(tb->widgetForAction(act));
        Q_ASSERT(btn);
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);

        auto newMenu = WidgetsFactory::createMenu(tb);
        btn->setMenu(newMenu);

        // Import file.
        newMenu->addAction(MainWindow::tr("Import File"),
                           newMenu,
                           []() {
                               emit VNoteX::getInst().importFileRequested();
                           });

        // Import folder.
        newMenu->addAction(MainWindow::tr("Import Folder"),
                           newMenu,
                           []() {
                               emit VNoteX::getInst().importFolderRequested();
                           });

        newMenu->addSeparator();

        auto exportAct = newMenu->addAction(MainWindow::tr("Export"),
                                            newMenu,
                                            []() {
                                                emit VNoteX::getInst().exportRequested();
                                            });
        WidgetUtils::addActionShortcut(exportAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::Export));
    }

    return tb;
}

QToolBar *ToolBarHelper::setupQuickAccessToolBar(MainWindow *p_win, QToolBar *p_toolBar)
{
    auto tb = p_toolBar;
    if (!tb) {
        tb = createToolBar(p_win, MainWindow::tr("Quick Access"), "QuickAccessToolBar");
    }

    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    // Flash Page.
    {
        auto flashPageAct = tb->addAction(generateIcon("flash_page_menu.svg"),
                                          MainWindow::tr("Flash Page"),
                                          tb,
                                          [p_win]() {
                                              const auto &flashPage = ConfigMgr::getInst().getSessionConfig().getFlashPage();
                                              if (flashPage.isEmpty()) {
                                                  MessageBoxHelper::notify(
                                                      MessageBoxHelper::Type::Information,
                                                      MainWindow::tr("Please set the Flash Page location in the Settings dialog first."),
                                                      MainWindow::tr("Flash Page is a temporary page for a flash of inspiration."),
                                                      QString(),
                                                      p_win);
                                                  return;
                                              }

                                              auto paras = QSharedPointer<FileOpenParameters>::create();
                                              paras->m_mode = ViewWindowMode::Edit;
                                              emit VNoteX::getInst().openFileRequested(flashPage, paras);
                                          });
        WidgetUtils::addActionShortcut(flashPageAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::FlashPage));
    }

    // Quick Access.
    {
        auto toolBtn = WidgetsFactory::createToolButton(tb);
        toolBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

        auto btnMenu = WidgetsFactory::createMenu(tb);
        toolBtn->setMenu(btnMenu);

        // Quick Acces.
        auto quickAccessAct = new QAction(generateIcon("quick_access_menu.svg"), MainWindow::tr("Quick Access"), toolBtn);
        MainWindow::connect(quickAccessAct, &QAction::triggered,
                            p_win, [p_win]() {
                                const auto &quickAccess = ConfigMgr::getInst().getSessionConfig().getQuickAccessFiles();
                                if (quickAccess.isEmpty()) {
                                    MessageBoxHelper::notify(
                                        MessageBoxHelper::Type::Information,
                                        MainWindow::tr("Please pin files to Quick Access first."),
                                        MainWindow::tr("Files could be pinned to Quick Access via context menu."),
                                        MainWindow::tr("Quick Access could be managed in the Settings dialog."),
                                        p_win);
                                    return;
                                }

                                emit VNoteX::getInst().openFileRequested(quickAccess.first(),
                                                                         QSharedPointer<FileOpenParameters>::create());
                            });
        WidgetUtils::addActionShortcut(quickAccessAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::QuickAccess));

        toolBtn->setDefaultAction(quickAccessAct);
        // To hide the shortcut text shown in button.
        toolBtn->setText(MainWindow::tr("Quick Access"));

        MainWindow::connect(btnMenu, &QMenu::aboutToShow,
                            btnMenu, [btnMenu]() {
                                btnMenu->clear();
                                const auto &quickAccess = ConfigMgr::getInst().getSessionConfig().getQuickAccessFiles();
                                if (quickAccess.isEmpty()) {
                                    auto act = btnMenu->addAction(MainWindow::tr("Quick Access Not Set"));
                                    act->setEnabled(false);
                                    return;
                                }

                                for (const auto &file : quickAccess) {
                                    auto act = btnMenu->addAction(PathUtils::fileName(file));
                                    act->setData(file);
                                    act->setToolTip(file);
                                }
                            });
        MainWindow::connect(btnMenu, &QMenu::triggered,
                            btnMenu, [](QAction *p_act) {
                                emit VNoteX::getInst().openFileRequested(p_act->data().toString(),
                                                                         QSharedPointer<FileOpenParameters>::create());
                            });
        tb->addWidget(toolBtn);
    }

    return tb;
}

QToolBar *ToolBarHelper::setupSettingsToolBar(MainWindow *p_win, QToolBar *p_toolBar)
{
    auto tb = p_toolBar;
    if (!tb) {
        tb = createToolBar(p_win, MainWindow::tr("Settings"), "SettingsToolBar");
    }

    // Spacer.
    addSpacer(tb);

    // Expand.
    {
        const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

        auto btn = WidgetsFactory::createToolButton(tb);

        auto menu = WidgetsFactory::createMenu(tb);
        btn->setMenu(menu);

        auto expandAct = menu->addAction(generateIcon("expand.svg"),
                                         MainWindow::tr("Expand Content Area"));
        WidgetUtils::addActionShortcut(expandAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::ExpandContentArea));
        expandAct->setCheckable(true);
        MainWindow::connect(expandAct, &QAction::triggered,
                            p_win, &MainWindow::setContentAreaExpanded);
        MainWindow::connect(p_win, &MainWindow::layoutChanged,
                            [expandAct, p_win]() {
                                expandAct->setChecked(p_win->isContentAreaExpanded());
                            });
        btn->setDefaultAction(expandAct);

        {
            auto fullScreenAct = new FullScreenToggleAction(p_win,
                                                            generateIcon("fullscreen.svg"),
                                                            menu);
            const auto shortcut = coreConfig.getShortcut(CoreConfig::Shortcut::FullScreen);
            WidgetUtils::addActionShortcut(fullScreenAct, shortcut);
            MainWindow::connect(fullScreenAct, &FullScreenToggleAction::fullScreenToggled,
                                p_win, [shortcut](bool p_fullScreen) {
                                    if (p_fullScreen) {
                                        VNoteX::getInst().showTips(
                                            MainWindow::tr("Press %1 To Exit Full Screen").arg(shortcut));
                                    } else {
                                        VNoteX::getInst().showTips("");
                                    }
                                });
            menu->addAction(fullScreenAct);
        }

        auto stayOnTopAct = menu->addAction(generateIcon("stay_on_top.svg"), MainWindow::tr("Stay On Top"),
                                            p_win, &MainWindow::setStayOnTop);
        stayOnTopAct->setCheckable(true);
        WidgetUtils::addActionShortcut(stayOnTopAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::StayOnTop));

        menu->addSeparator();

        {
            // Windows.
            auto subMenu = menu->addMenu(MainWindow::tr("Windows"));
            for (auto dock : p_win->getDocks()) {
                subMenu->addAction(dock->toggleViewAction());
            }
        }

        tb->addWidget(btn);
    }

    // Settings.
    {
        const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

        auto act = tb->addAction(generateIcon("settings_menu.svg"), MainWindow::tr("Settings"));
        auto btn = dynamic_cast<QToolButton *>(tb->widgetForAction(act));
        Q_ASSERT(btn);
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);

        auto menu = WidgetsFactory::createMenu(tb);
        btn->setMenu(menu);

        auto settingsAct = menu->addAction(generateIcon("settings.svg"),
                                           MainWindow::tr("Settings"),
                                           menu,
                                           [p_win]() {
                                               SettingsDialog dialog(p_win);
                                               dialog.exec();
                                           });
        WidgetUtils::addActionShortcut(settingsAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::Settings));

        menu->addSeparator();

        menu->addAction(MainWindow::tr("Open User Configuration Folder"),
                        menu,
                        []() {
                            auto folderPath = ConfigMgr::getInst().getUserFolder();
                            WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(folderPath));
                        });

        menu->addAction(MainWindow::tr("Open Default Configuration Folder"),
                        menu,
                        []() {
                            auto folderPath = ConfigMgr::getInst().getAppFolder();
                            WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(folderPath));
                        });

        menu->addSeparator();

        menu->addAction(MainWindow::tr("Edit User Configuration"),
                        menu,
                        []() {
                            auto file = ConfigMgr::getInst().getConfigFilePath(ConfigMgr::Source::User);
                            auto paras = QSharedPointer<FileOpenParameters>::create();
                            emit VNoteX::getInst().openFileRequested(file, paras);
                        });

        menu->addAction(MainWindow::tr("Open Default Configuration"),
                        menu,
                        []() {
                            auto file = ConfigMgr::getInst().getConfigFilePath(ConfigMgr::Source::App);
                            auto paras = QSharedPointer<FileOpenParameters>::create();
                            paras->m_readOnly = true;
                            emit VNoteX::getInst().openFileRequested(file, paras);
                        });

        menu->addSeparator();

        menu->addAction(MainWindow::tr("Reset Main Window Layout"),
                        menu,
                        [p_win]() {
                            p_win->resetStateAndGeometry();
                        });

        menu->addSeparator();

        menu->addAction(MainWindow::tr("Restart"),
                        menu,
                        [p_win]() {
                            p_win->restart();
                        });

        auto quitAct = menu->addAction(MainWindow::tr("Quit"),
                                       menu,
                                       [p_win]() {
                                           p_win->quitApp();
                                       });
        quitAct->setMenuRole(QAction::QuitRole);
        WidgetUtils::addActionShortcut(quitAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::Quit));
    }

    // Help.
    {
        auto act = tb->addAction(generateIcon("help_menu.svg"), MainWindow::tr("Help"));
        auto btn = dynamic_cast<QToolButton *>(tb->widgetForAction(act));
        Q_ASSERT(btn);
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);

        auto menu = WidgetsFactory::createMenu(tb);
        btn->setMenu(menu);

        auto whatsThisAct = menu->addAction(generateIcon("whatsthis.svg"),
                                            MainWindow::tr("What's This?"),
                                            menu,
                                            []() {
                                                QWhatsThis::enterWhatsThisMode();
                                            });
        whatsThisAct->setToolTip(MainWindow::tr("Enter WhatsThis mode and click somewhere to show help information"));

        menu->addSeparator();

        menu->addAction(MainWindow::tr("Shortcuts Help"),
                        menu,
                        []() {
                            const auto file = DocsUtils::getDocFile(QStringLiteral("shortcuts.md"));
                            if (!file.isEmpty()) {
                                auto paras = QSharedPointer<FileOpenParameters>::create();
                                paras->m_readOnly = true;
                                emit VNoteX::getInst().openFileRequested(file, paras);
                            }
                        });

        menu->addAction(MainWindow::tr("Markdown Guide"),
                        menu,
                        []() {
                            const auto file = DocsUtils::getDocFile(QStringLiteral("markdown_guide.md"));
                            if (!file.isEmpty()) {
                                auto paras = QSharedPointer<FileOpenParameters>::create();
                                paras->m_readOnly = true;
                                emit VNoteX::getInst().openFileRequested(file, paras);
                            }
                        });

        menu->addSeparator();

        menu->addAction(MainWindow::tr("View Logs"),
                        menu,
                        []() {
                            const auto file = ConfigMgr::getInst().getLogFile();
                            if (QFileInfo::exists(file)) {
                                auto paras = QSharedPointer<FileOpenParameters>::create();
                                paras->m_readOnly = true;
                                emit VNoteX::getInst().openFileRequested(file, paras);
                            }
                        });

        menu->addSeparator();

        menu->addAction(MainWindow::tr("%1 Home Page").arg(qApp->applicationDisplayName()),
                        menu,
                        []() {
                            WidgetUtils::openUrlByDesktop(QUrl("https://vnotex.github.io/vnote"));
                        });

        menu->addAction(MainWindow::tr("Feedback And Discussions"),
                        menu,
                        []() {
                            WidgetUtils::openUrlByDesktop(QUrl("https://github.com/vnotex/vnote/discussions"));
                        });

        menu->addSeparator();

        menu->addAction(MainWindow::tr("About"),
                        menu,
                        [p_win]() {
                            auto info = MainWindow::tr("<h3>%1</h3>\n<span>%2</span>\n").arg(qApp->applicationDisplayName(),
                                                                                             qApp->applicationVersion());
                            const auto text = DocsUtils::getDocText(QStringLiteral("about_vnotex.txt"));
                            QMessageBox::about(p_win, MainWindow::tr("About"), info + text);
                        });

        auto aboutQtAct = menu->addAction(MainWindow::tr("About Qt"));
        aboutQtAct->setMenuRole(QAction::AboutQtRole);
        MainWindow::connect(aboutQtAct, &QAction::triggered,
                            qApp, &QApplication::aboutQt);
    }

    return tb;
}

static const QString c_fgPalette = QStringLiteral("widgets#toolbar#icon#fg");
static const QString c_disabledPalette = QStringLiteral("widgets#toolbar#icon#disabled#fg");
static const QString c_dangerousPalette = QStringLiteral("widgets#toolbar#icon#danger#fg");

QIcon ToolBarHelper::generateIcon(const QString &p_iconName)
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    const auto fg = themeMgr.paletteColor(c_fgPalette);
    const auto disabledFg = themeMgr.paletteColor(c_disabledPalette);

    QVector<IconUtils::OverriddenColor> colors;
    colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
    colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));

    auto iconFile = themeMgr.getIconFile(p_iconName);
    return IconUtils::fetchIcon(iconFile, colors);
}

QIcon ToolBarHelper::generateDangerousIcon(const QString &p_iconName)
{
    const auto &themeMgr = VNoteX::getInst().getThemeMgr();
    const auto fg = themeMgr.paletteColor(c_fgPalette);
    const auto disabledFg = themeMgr.paletteColor(c_disabledPalette);
    const auto dangerousFg = themeMgr.paletteColor(c_dangerousPalette);

    QVector<IconUtils::OverriddenColor> colors;
    colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
    colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));
    colors.push_back(IconUtils::OverriddenColor(dangerousFg, QIcon::Active));

    auto iconFile = themeMgr.getIconFile(p_iconName);
    return IconUtils::fetchIcon(iconFile, colors);
}

void ToolBarHelper::setupToolBars(MainWindow *p_win)
{
    m_toolBars.clear();

    auto fileTab = setupFileToolBar(p_win, nullptr);
    m_toolBars.insert(fileTab->objectName(), fileTab);

    auto quickAccessTb = setupQuickAccessToolBar(p_win, nullptr);
    m_toolBars.insert(quickAccessTb->objectName(), quickAccessTb);

    auto settingsToolBar = setupSettingsToolBar(p_win, nullptr);
    m_toolBars.insert(settingsToolBar->objectName(), settingsToolBar);
}

void ToolBarHelper::setupToolBars(MainWindow *p_win, QToolBar *p_toolBar)
{
    m_toolBars.clear();

    p_toolBar->setObjectName(QStringLiteral("UnifiedToolBar"));
    p_toolBar->setMovable(false);
    p_win->addToolBar(p_toolBar);

    setupFileToolBar(p_win, p_toolBar);
    setupQuickAccessToolBar(p_win, p_toolBar);
    setupSettingsToolBar(p_win, p_toolBar);
    m_toolBars.insert(p_toolBar->objectName(), p_toolBar);
}

void ToolBarHelper::addSpacer(QToolBar *p_toolBar)
{
    auto spacer = new QWidget(p_toolBar);
    spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    auto act = p_toolBar->addWidget(spacer);
    act->setEnabled(false);
}
