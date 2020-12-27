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
#include "fullscreentoggleaction.h"
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/fileopenparameters.h>
#include "propertydefs.h"
#include "dialogs/settings/settingsdialog.h"

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

    // Notebook.
    {
        auto act = tb->addAction(generateIcon("notebook_menu.svg"), MainWindow::tr("Notebook"));

        auto toolBtn = dynamic_cast<QToolButton *>(tb->widgetForAction(act));
        Q_ASSERT(toolBtn);
        toolBtn->setPopupMode(QToolButton::InstantPopup);
        toolBtn->setProperty(PropertyDefs::s_toolButtonWithoutMenuIndicator, true);

        auto newMenu = WidgetsFactory::createMenu(tb);
        toolBtn->setMenu(newMenu);

        newMenu->addAction(generateIcon("new_notebook.svg"),
                           MainWindow::tr("New Notebook"),
                           newMenu,
                           []() {
                               emit VNoteX::getInst().newNotebookRequested();
                           });

        // New notebook from folder.
        newMenu->addAction(generateIcon("new_notebook_from_folder.svg"),
                           MainWindow::tr("New Notebook From Folder"),
                           newMenu,
                           []() {
                               emit VNoteX::getInst().newNotebookFromFolderRequested();
                           });

        newMenu->addSeparator();

        // Import notebook.
        newMenu->addAction(generateIcon("import_notebook.svg"),
                           MainWindow::tr("Import Notebook"),
                           newMenu,
                           []() {
                               emit VNoteX::getInst().importNotebookRequested();
                           });

        // Import notebook of VNote 2.0.
        newMenu->addAction(generateIcon("import_notebook_of_vnote2.svg"),
                           MainWindow::tr("Import Legacy Notebook Of VNote 2.0"),
                           newMenu,
                           []() {
                               emit VNoteX::getInst().importLegacyNotebookRequested();
                           });
    }

    // New Note.
    {
        const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

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
        btn->setProperty(PropertyDefs::s_toolButtonWithoutMenuIndicator, true);

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

    return tb;
}

QToolBar *ToolBarHelper::setupQuickAccessToolBar(MainWindow *p_win, QToolBar *p_toolBar)
{
    auto tb = p_toolBar;
    if (!tb) {
        tb = createToolBar(p_win, MainWindow::tr("Quick Access"), "QuickAccessToolBar");
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

        auto fullScreenAct = new FullScreenToggleAction(p_win,
                                                        generateIcon("fullscreen.svg"),
                                                        menu);
        WidgetUtils::addActionShortcut(fullScreenAct,
                                       coreConfig.getShortcut(CoreConfig::Shortcut::FullScreen));
        menu->addAction(fullScreenAct);

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
        btn->setProperty(PropertyDefs::s_toolButtonWithoutMenuIndicator, true);

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

        menu->addAction(MainWindow::tr("Quit"),
                        menu,
                        [p_win]() {
                            p_win->quitApp();
                        });

        menu->addAction(MainWindow::tr("Restart"),
                        menu,
                        [p_win]() {
                            p_win->restart();
                        });
    }

    // Help.
    {
        auto act = tb->addAction(generateIcon("help_menu.svg"), MainWindow::tr("Help"));
        auto btn = dynamic_cast<QToolButton *>(tb->widgetForAction(act));
        Q_ASSERT(btn);
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setProperty(PropertyDefs::s_toolButtonWithoutMenuIndicator, true);

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

    auto quickAccessTb = setupQuickAccessToolBar(p_win, nullptr);
    m_toolBars.insert(quickAccessTb->objectName(), quickAccessTb);

    auto fileTab = setupFileToolBar(p_win, nullptr);
    m_toolBars.insert(fileTab->objectName(), fileTab);

    auto settingsToolBar = setupSettingsToolBar(p_win, nullptr);
    m_toolBars.insert(settingsToolBar->objectName(), settingsToolBar);
}

void ToolBarHelper::setupToolBars(MainWindow *p_win, QToolBar *p_toolBar)
{
    m_toolBars.clear();

    p_toolBar->setObjectName(QStringLiteral("UnifiedToolBar"));
    p_toolBar->setMovable(false);
    p_win->addToolBar(p_toolBar);

    setupQuickAccessToolBar(p_win, p_toolBar);
    setupFileToolBar(p_win, p_toolBar);
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
