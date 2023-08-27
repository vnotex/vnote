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
#include <QWidgetAction>

#include "mainwindow.h"
#include <core/vnotex.h>
#include "widgetsfactory.h"
#include <utils/iconutils.h>
#include <utils/widgetutils.h>
#include <utils/docsutils.h>
#include <utils/pathutils.h>
#include "fullscreentoggleaction.h"
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/sessionconfig.h>
#include <core/editorconfig.h>
#include <core/markdowneditorconfig.h>
#include <core/fileopenparameters.h>
#include <core/htmltemplatehelper.h>
#include <core/exception.h>
#include <task/taskmgr.h>
#include <unitedentry/unitedentry.h>
#include "propertydefs.h"
#include "dialogs/settings/settingsdialog.h"
#include "dialogs/updater.h"
#include "messageboxhelper.h"
#include "labelwithbuttonswidget.h"

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

        btnMenu->addAction(MainWindow::tr("New Notebook"),
                           btnMenu,
                           []() {
                               emit VNoteX::getInst().newNotebookRequested();
                           });

        // New notebook from folder.
        btnMenu->addAction(MainWindow::tr("New Notebook From Folder"),
                           btnMenu,
                           []() {
                               emit VNoteX::getInst().newNotebookFromFolderRequested();
                           });

        btnMenu->addSeparator();

        // Import notebook.
        btnMenu->addAction(MainWindow::tr("Open Other Notebooks"),
                           btnMenu,
                           []() {
                               emit VNoteX::getInst().importNotebookRequested();
                           });

        // Import notebook of VNote 2.
        btnMenu->addAction(MainWindow::tr("Open Legacy Notebooks Of VNote 2"),
                           btnMenu,
                           []() {
                               emit VNoteX::getInst().importLegacyNotebookRequested();
                           });

        btnMenu->addSeparator();

        // Manage notebook.
        btnMenu->addAction(MainWindow::tr("Manage Notebooks"),
                           btnMenu,
                           []() {
                               emit VNoteX::getInst().manageNotebooksRequested();
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

        // New quick note.
        auto newQuickNoteAct = newMenu->addAction(generateIcon("new_note.svg"),
                                             MainWindow::tr("New Quick Note"),
                                             newMenu,
                                             []() {
                                                 emit VNoteX::getInst().newQuickNoteRequested();
                                             });
        WidgetUtils::addActionShortcut(newQuickNoteAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::NewQuickNote));

        // New folder.
        auto newFolderAct = newMenu->addAction(generateIcon("new_folder.svg"),
                                               MainWindow::tr("New Folder"),
                                               newMenu,
                                               []() {
                                                   emit VNoteX::getInst().newFolderRequested();
                                               });
        WidgetUtils::addActionShortcut(newFolderAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::NewFolder));

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

    // Import.
    {
        auto act = tb->addAction(generateIcon("import_menu.svg"), MainWindow::tr("Import"));

        auto btn = dynamic_cast<QToolButton *>(tb->widgetForAction(act));
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
    }

    // Export.
    {
        auto exportAct = tb->addAction(generateIcon("export_menu.svg"),
                                       MainWindow::tr("Export (Convert Format)"),
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

        auto btnMenu = WidgetsFactory::createMenu(tb);
        toolBtn->setMenu(btnMenu);

        // Quick Acces.
        auto quickAccessAct = btnMenu->addAction(generateIcon("quick_access_menu.svg"), MainWindow::tr("Quick Access"));
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

                                activateQuickAccess(quickAccess.first());
                            });
        WidgetUtils::addActionShortcut(quickAccessAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::QuickAccess));

        toolBtn->setDefaultAction(quickAccessAct);

        MainWindow::connect(btnMenu, &QMenu::aboutToShow,
                            btnMenu, [btnMenu]() {
                                ToolBarHelper::updateQuickAccessMenu(btnMenu);
                            });
        MainWindow::connect(btnMenu, &QMenu::triggered,
                            btnMenu, [](QAction *p_act) {
                                activateQuickAccess(p_act->data().toString());
                            });
        tb->addWidget(toolBtn);
    }

    // Task.
    {
        auto act = tb->addAction(generateIcon("task_menu.svg"), MainWindow::tr("Task"));
        auto btn = dynamic_cast<QToolButton *>(tb->widgetForAction(act));
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);

        auto taskMenu = WidgetsFactory::createMenu(tb);
        setupTaskActionMenu(taskMenu);
        btn->setMenu(taskMenu);
        MainWindow::connect(taskMenu, &QMenu::triggered,
                            taskMenu, [](QAction *act) {
            auto task = reinterpret_cast<Task *>(act->data().toULongLong());
            if (task) {
                task->run();
            }
        });
        MainWindow::connect(&VNoteX::getInst().getTaskMgr(), &TaskMgr::tasksUpdated,
                            taskMenu, [taskMenu]() {
            setupTaskMenu(taskMenu);
        });
    }

    // United Entry.
    {
        // Managed by QObject.
        auto ue = new UnitedEntry(p_win);
        tb->addAction(ue->getTriggerAction());
    }

    return tb;
}

void ToolBarHelper::setupTaskMenu(QMenu *p_menu)
{
    p_menu->clear();

    setupTaskActionMenu(p_menu);

    p_menu->addSeparator();

    const auto &taskMgr = VNoteX::getInst().getTaskMgr();
    for (const auto &task : taskMgr.getAppTasks()) {
        addTaskMenu(p_menu, task.data());
    }

    p_menu->addSeparator();

    for (const auto &task : taskMgr.getUserTasks()) {
        addTaskMenu(p_menu, task.data());
    }

    p_menu->addSeparator();

    for (const auto &task : taskMgr.getNotebookTasks()) {
        addTaskMenu(p_menu, task.data());
    }
}

void ToolBarHelper::setupTaskActionMenu(QMenu *p_menu)
{
    p_menu->addAction(MainWindow::tr("Add Task"),
                      p_menu,
                      []() {
                          WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(ConfigMgr::getInst().getUserTaskFolder()));
                      });

    p_menu->addAction(MainWindow::tr("Reload"),
                      p_menu,
                      []() {
                          VNoteX::getInst().getTaskMgr().reload();
                      });
}

void ToolBarHelper::addTaskMenu(QMenu *p_menu, Task *p_task)
{
    QAction *action = nullptr;

    const auto &children = p_task->getChildren();

    auto label = p_task->getLabel();
    // '&' will be considered shortuct symbol in QAction.
    label.replace("&", "&&");

    if (children.isEmpty()) {
        action = p_menu->addAction(label);
    } else {
        auto subMenu = p_menu->addMenu(label);
        for (auto task : children) {
            addTaskMenu(subMenu, task);
        }
        action = subMenu->menuAction();
    }

    QIcon icon;
    try {
        auto taskIcon = p_task->getIcon();
        if (!taskIcon.isEmpty()) {
            icon = generateIcon(p_task->getIcon());
        }
    }  catch (Exception &e) {
        if (e.m_type != Exception::Type::FailToReadFile) {
            throw e;
        }
    }
    action->setIcon(icon);

    action->setData(reinterpret_cast<qulonglong>(p_task));

    WidgetUtils::addActionShortcut(action, p_task->getShortcut());
}

QToolBar *ToolBarHelper::setupSettingsToolBar(MainWindow *p_win, QToolBar *p_toolBar)
{
    auto tb = p_toolBar;
    if (!tb) {
        tb = createToolBar(p_win, MainWindow::tr("Settings"), "SettingsToolBar");
    }

    addSpacer(tb);

    setupExpandButton(p_win, tb);

    setupMenuButton(p_win, tb);

    return tb;
}

static const QString c_fgPalette = QStringLiteral("widgets#toolbar#icon#fg");
static const QString c_disabledPalette = QStringLiteral("widgets#toolbar#icon#disabled#fg");
static const QString c_dangerousPalette = QStringLiteral("widgets#toolbar#icon#danger#fg");

QIcon ToolBarHelper::generateIcon(const QString &p_iconName)
{
    static QVector<IconUtils::OverriddenColor> colors;

    const auto &themeMgr = VNoteX::getInst().getThemeMgr();

    if (colors.isEmpty()) {
        const auto fg = themeMgr.paletteColor(c_fgPalette);
        const auto disabledFg = themeMgr.paletteColor(c_disabledPalette);

        colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
        colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));
    }

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

void ToolBarHelper::setupToolBars(MainWindow *p_mainWindow)
{
    setupFileToolBar(p_mainWindow, nullptr);

    setupQuickAccessToolBar(p_mainWindow, nullptr);

    setupSettingsToolBar(p_mainWindow, nullptr);
}

void ToolBarHelper::setupToolBars(MainWindow *p_mainWindow, QToolBar *p_toolBar)
{
    p_toolBar->setObjectName(QStringLiteral("UnifiedToolBar"));
    p_toolBar->setMovable(false);
    p_mainWindow->addToolBar(p_toolBar);

    setupFileToolBar(p_mainWindow, p_toolBar);
    setupQuickAccessToolBar(p_mainWindow, p_toolBar);
    setupSettingsToolBar(p_mainWindow, p_toolBar);
}

void ToolBarHelper::addSpacer(QToolBar *p_toolBar)
{
    auto spacer = new QWidget(p_toolBar);
    spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    auto act = p_toolBar->addWidget(spacer);
    act->setEnabled(false);
}

void ToolBarHelper::updateQuickAccessMenu(QMenu *p_menu)
{
    p_menu->clear();
    const auto &quickAccess = ConfigMgr::getInst().getSessionConfig().getQuickAccessFiles();
    if (quickAccess.isEmpty()) {
        auto act = p_menu->addAction(MainWindow::tr("Quick Access Not Set"));
        act->setEnabled(false);
        return;
    }

    for (const auto &file : quickAccess) {
        auto act = new QWidgetAction(p_menu);
        auto widget = new LabelWithButtonsWidget(PathUtils::fileName(file), LabelWithButtonsWidget::Delete);
        p_menu->connect(widget, &LabelWithButtonsWidget::triggered,
                        p_menu, [p_menu, act]() {
                            const auto qaFile = act->data().toString();
                            ConfigMgr::getInst().getSessionConfig().removeQuickAccessFile(qaFile);
                            p_menu->removeAction(act);
                            if (p_menu->isEmpty()) {
                                p_menu->hide();
                            }
                        });
        // @act will own @widget.
        act->setDefaultWidget(widget);
        act->setData(file);
        act->setToolTip(file);

        // Must call after setDefaultWidget().
        p_menu->addAction(act);
    }
}

void ToolBarHelper::setupExpandButton(MainWindow *p_win, QToolBar *p_toolBar)
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    auto btn = WidgetsFactory::createToolButton(p_toolBar);

    auto menu = WidgetsFactory::createMenu(p_toolBar);
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

    auto stayOnTopAct = menu->addAction(generateIcon("stay_on_top.svg"), MainWindow::tr("Stay on Top"),
                                        p_win, &MainWindow::setStayOnTop);
    stayOnTopAct->setCheckable(true);
    WidgetUtils::addActionShortcut(stayOnTopAct,
                                   coreConfig.getShortcut(CoreConfig::Shortcut::StayOnTop));

    menu->addSeparator();

    {
        // Windows.
        // MainWindow will clear the title of the dock widget for the tab bar, so we need to use
        // another action to wrap the no-text action.
        auto subMenu = menu->addMenu(MainWindow::tr("Windows"));
        for (auto dock : p_win->getDocks()) {
            // @act is owned by the QDockWidget.
            auto act = dock->toggleViewAction();
            auto actWrapper = subMenu->addAction(act->text());
            actWrapper->setCheckable(act->isCheckable());
            actWrapper->setChecked(act->isChecked());
            MainWindow::connect(act, &QAction::toggled,
                                actWrapper, [actWrapper](bool checked) {
                                    if (actWrapper->isChecked() != checked) {
                                        actWrapper->setChecked(checked);
                                    }
                                });
            MainWindow::connect(actWrapper, &QAction::triggered,
                                act, [p_win, act]() {
                                    act->trigger();
                                    p_win->updateDockWidgetTabBar();
                                });
        }
    }

    p_toolBar->addWidget(btn);
}

void ToolBarHelper::setupMenuButton(MainWindow *p_win, QToolBar *p_toolBar)
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

    auto act = p_toolBar->addAction(generateIcon("menu.svg"), MainWindow::tr("Menu"));
    auto btn = dynamic_cast<QToolButton *>(p_toolBar->widgetForAction(act));
    Q_ASSERT(btn);
    btn->setPopupMode(QToolButton::InstantPopup);
    btn->setProperty(PropertyDefs::c_toolButtonWithoutMenuIndicator, true);

    auto menu = WidgetsFactory::createMenu(p_toolBar);
    btn->setMenu(menu);

    {
        auto settingsAct = menu->addAction(MainWindow::tr("Settings"),
                                           menu,
                                           [p_win]() {
                                               SettingsDialog dialog(p_win);
                                               dialog.exec();
                                           });
        WidgetUtils::addActionShortcut(settingsAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::Settings));

        menu->addSeparator();

        menu->addAction(MainWindow::tr("Edit User Configuration File"),
                        menu,
                        []() {
                            auto file = ConfigMgr::getInst().getConfigFilePath(ConfigMgr::Source::User);
                            auto paras = QSharedPointer<FileOpenParameters>::create();
                            paras->m_sessionEnabled = false;
                            emit VNoteX::getInst().openFileRequested(file, paras);
                        });

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

        {
            auto act = menu->addAction(MainWindow::tr("Edit Markdown User Styles"),
                                       menu,
                                       []() {
                                           const auto file = ConfigMgr::getInst().getUserMarkdownUserStyleFile();
                                           auto paras = QSharedPointer<FileOpenParameters>::create();
                                           paras->m_sessionEnabled = false;
                                           paras->m_hooks[FileOpenParameters::PostSave] = []() {
                                               qDebug() << "post save";
                                               const auto &markdownConfig = ConfigMgr::getInst().getEditorConfig().getMarkdownEditorConfig();
                                               HtmlTemplateHelper::updateMarkdownViewerTemplate(markdownConfig, true);
                                           };
                                           emit VNoteX::getInst().openFileRequested(file, paras);
                                       });
            act->setStatusTip(MainWindow::tr("Edit the user styles of Markdown editor read mode"));
        }

        menu->addSeparator();

        menu->addAction(MainWindow::tr("Reset Main Window Layout"),
                        menu,
                        [p_win]() {
                            p_win->resetStateAndGeometry();
                        });
    }

    menu->addSeparator();

    menu->addAction(MainWindow::tr("View Logs"),
                    menu,
                    []() {
                        const auto file = ConfigMgr::getInst().getLogFile();
                        if (QFileInfo::exists(file)) {
                            auto paras = QSharedPointer<FileOpenParameters>::create();
                            paras->m_readOnly = true;
                            paras->m_sessionEnabled = false;
                            emit VNoteX::getInst().openFileRequested(file, paras);
                        }
                    });

    {
        menu->addSeparator();

        menu->addAction(MainWindow::tr("Shortcuts Help"),
                        menu,
                        []() {
                            const auto file = DocsUtils::getDocFile(QStringLiteral("shortcuts.md"));
                            if (!file.isEmpty()) {
                                auto paras = QSharedPointer<FileOpenParameters>::create();
                                paras->m_readOnly = true;
                                paras->m_sessionEnabled = false;
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
                                paras->m_sessionEnabled = false;
                                emit VNoteX::getInst().openFileRequested(file, paras);
                            }
                        });

        auto helpMenu = menu->addMenu(MainWindow::tr("Help"));

        helpMenu->addAction(MainWindow::tr("Home Page"),
                            helpMenu,
                            []() {
                                WidgetUtils::openUrlByDesktop(QUrl("https://vnotex.github.io/vnote"));
                            });

        helpMenu->addAction(MainWindow::tr("Documentation"),
                            helpMenu,
                            []() {
                                WidgetUtils::openUrlByDesktop(QUrl("https://vnotex.github.io/vnote/en_us/#!docs/vx.json"));
                            });

        helpMenu->addAction(MainWindow::tr("Feedback and Discussions"),
                            helpMenu,
                            []() {
                                WidgetUtils::openUrlByDesktop(QUrl("https://github.com/vnotex/vnote/discussions"));
                            });

        helpMenu->addSeparator();

        helpMenu->addAction(MainWindow::tr("Contributors"),
                            helpMenu,
                            []() {
                                WidgetUtils::openUrlByDesktop(QUrl("https://github.com/vnotex/vnote/graphs/contributors"));
                            });

        helpMenu->addAction(MainWindow::tr("About"),
                            helpMenu,
                            [p_win]() {
                                auto info = MainWindow::tr("<h3>%1</h3>\n<span>%2</span>\n").arg(qApp->applicationDisplayName(),
                                                                                                 qApp->applicationVersion());
                                const auto text = DocsUtils::getDocText(QStringLiteral("about_vnotex.txt"));
                                QMessageBox::about(p_win, MainWindow::tr("About"), info + text);
                            });

        auto aboutQtAct = helpMenu->addAction(MainWindow::tr("About Qt"));
        aboutQtAct->setMenuRole(QAction::AboutQtRole);
        MainWindow::connect(aboutQtAct, &QAction::triggered,
                            qApp, &QApplication::aboutQt);
    }

    menu->addSeparator();

    menu->addAction(MainWindow::tr("Check for Updates"),
                    menu,
                    [p_win]() {
                        Updater updater(p_win);
                        updater.exec();
                    });


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

void ToolBarHelper::activateQuickAccess(const QString &p_file)
{
    const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
    auto paras = QSharedPointer<FileOpenParameters>::create();
    paras->m_mode = coreConfig.getDefaultOpenMode();

    emit VNoteX::getInst().openFileRequested(p_file, paras);
}
