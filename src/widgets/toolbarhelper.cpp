#include "toolbarhelper.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QWhatsThis>
#include <QWidgetAction>

#include "dialogs/updater.h"
#include "fullscreentoggleaction.h"
#include "labelwithbuttonswidget.h"
#include "mainwindow.h"
#include "messageboxhelper.h"
#include "propertydefs.h"
#include "widgetsfactory.h"
#include <core/configmgr.h>
#include <core/coreconfig.h>
#include <core/editorconfig.h>
#include <core/exception.h>
#include <core/fileopenparameters.h>
#include <core/htmltemplatehelper.h>
#include <core/markdowneditorconfig.h>
#include <core/notebookmgr.h>
#include <core/sessionconfig.h>
#include <core/vnotex.h>
#include <task/taskmgr.h>
// LEGACY: UnitedEntry migrated to new architecture in toolbarhelper2.cpp.
// #include <unitedentry/unitedentry.h>
#include <gui/utils/iconutils.h>
#include <utils/docsutils.h>
#include <utils/pathutils.h>
#include <utils/vxurlutils.h>
#include <utils/widgetutils.h>

using namespace vnotex;

static QToolBar *createToolBar(MainWindow *p_win, const QString &p_title, const QString &p_name) {
  auto tb = p_win->addToolBar(p_title);
  tb->setObjectName(p_name);
  tb->setMovable(false);
  return tb;
}

QToolBar *ToolBarHelper::setupFileToolBar(MainWindow *p_win, QToolBar *p_toolBar) {
  auto tb = p_toolBar;
  if (!tb) {
    tb = createToolBar(p_win, MainWindow::tr("File"), "FileToolBar");
  }

  const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

  // New Note.
  {
    auto newBtn = WidgetsFactory::createToolButton(tb);
    newBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // New note.
    const auto text = MainWindow::tr("New Note");
    auto newNoteAct = new QAction(generateIcon("new_note.svg"), text, newBtn);
    QAction::connect(newNoteAct, &QAction::triggered,
                     []() { emit VNoteX::getInst().newNoteRequested(); });
    WidgetUtils::addActionShortcut(newNoteAct,
                                   coreConfig.getShortcut(CoreConfig::Shortcut::NewNote));
    newBtn->addAction(newNoteAct);
    newBtn->setDefaultAction(newNoteAct);
    // To hide the shortcut text shown in button.
    newBtn->setText(text);

    // Popup menu.
    auto newMenu = WidgetsFactory::createMenu(tb);
    newBtn->setMenu(newMenu);

    // Quick note.
    auto newQuickNoteAct = newMenu->addAction(MainWindow::tr("Quick Note"), newMenu, []() {
      emit VNoteX::getInst().newQuickNoteRequested();
    });
    WidgetUtils::addActionShortcut(newQuickNoteAct,
                                   coreConfig.getShortcut(CoreConfig::Shortcut::NewQuickNote));

    // New folder.
    auto newFolderAct = newMenu->addAction(MainWindow::tr("New Folder"), newMenu,
                                           []() { emit VNoteX::getInst().newFolderRequested(); });
    WidgetUtils::addActionShortcut(newFolderAct,
                                   coreConfig.getShortcut(CoreConfig::Shortcut::NewFolder));

    newMenu->addSeparator();

    // Import file.
    newMenu->addAction(MainWindow::tr("Import File"), newMenu,
                       []() { emit VNoteX::getInst().importFileRequested(); });

    // Import folder.
    newMenu->addAction(MainWindow::tr("Import Folder"), newMenu,
                       []() { emit VNoteX::getInst().importFolderRequested(); });

    auto exportAct = newMenu->addAction(MainWindow::tr("Export (Convert Format)"), newMenu,
                                        []() { emit VNoteX::getInst().exportRequested(); });
    WidgetUtils::addActionShortcut(exportAct, coreConfig.getShortcut(CoreConfig::Shortcut::Export));

    newMenu->addSeparator();

    // Open file.
    newMenu->addAction(MainWindow::tr("Open File"), newMenu, [p_win]() {
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

  // Quick Access.
  {
    auto toolBtn = WidgetsFactory::createToolButton(tb);
    toolBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    const auto text = MainWindow::tr("Quick Access");
    auto quickAccessAct = new QAction(generateIcon("quick_access_menu.svg"), text, toolBtn);
    MainWindow::connect(quickAccessAct, &QAction::triggered, p_win, [p_win]() {
      const auto &quickAccess = ConfigMgr::getInst().getSessionConfig().getQuickAccessFiles();
      if (quickAccess.isEmpty()) {
        MessageBoxHelper::notify(
            MessageBoxHelper::Type::Information,
            MainWindow::tr("Please pin files to Quick Access first."),
            MainWindow::tr("Files could be pinned to Quick Access via context menu."),
            MainWindow::tr("Quick Access could be managed in the Settings dialog."), p_win);
        return;
      }

      activateQuickAccess(quickAccess.first());
    });
    WidgetUtils::addActionShortcut(quickAccessAct,
                                   coreConfig.getShortcut(CoreConfig::Shortcut::QuickAccess));
    toolBtn->addAction(quickAccessAct);
    toolBtn->setDefaultAction(quickAccessAct);
    // To hide the shortcut text shown in button.
    toolBtn->setText(text);

    auto btnMenu = WidgetsFactory::createMenu(tb);
    toolBtn->setMenu(btnMenu);

    MainWindow::connect(btnMenu, &QMenu::aboutToShow, btnMenu,
                        [btnMenu]() { ToolBarHelper::updateQuickAccessMenu(btnMenu); });
    MainWindow::connect(btnMenu, &QMenu::triggered, btnMenu,
                        [](QAction *p_act) { activateQuickAccess(p_act->data().toString()); });
    tb->addWidget(toolBtn);
  }

  return tb;
}

QToolBar *ToolBarHelper::setupSettingsToolBar(MainWindow *p_win, QToolBar *p_toolBar) {
  auto tb = p_toolBar;
  if (!tb) {
    tb = createToolBar(p_win, MainWindow::tr("Settings"), "SettingsToolBar");
  }

  addSpacer(tb);

  setupExpandButton(p_win, tb);

  setupSettingsButton(p_win, tb);

  return tb;
}

static const QString c_fgPalette = QStringLiteral("widgets#toolbar#icon#fg");
static const QString c_disabledPalette = QStringLiteral("widgets#toolbar#icon#disabled#fg");
static const QString c_dangerousPalette = QStringLiteral("widgets#toolbar#icon#danger#fg");

QIcon ToolBarHelper::generateIcon(const QString &p_iconName) {
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

QIcon ToolBarHelper::generateDangerousIcon(const QString &p_iconName) {
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

void ToolBarHelper::setupToolBars(MainWindow *p_mainWindow) {
  setupFileToolBar(p_mainWindow, nullptr);
  setupSettingsToolBar(p_mainWindow, nullptr);
}

void ToolBarHelper::setupToolBars(MainWindow *p_mainWindow, QToolBar *p_toolBar) {
  p_toolBar->setObjectName(QStringLiteral("UnifiedToolBar"));
  p_toolBar->setMovable(false);
  p_mainWindow->addToolBar(p_toolBar);

  setupFileToolBar(p_mainWindow, p_toolBar);
  setupSettingsToolBar(p_mainWindow, p_toolBar);
}

void ToolBarHelper::addSpacer(QToolBar *p_toolBar) {
  auto spacer = new QWidget(p_toolBar);
  spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
  auto act = p_toolBar->addWidget(spacer);
  act->setEnabled(false);
}

void ToolBarHelper::updateQuickAccessMenu(QMenu *p_menu) {
  p_menu->clear();
  auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();

  const auto &quickAccess = sessionConfig.getQuickAccessFiles();
  if (quickAccess.isEmpty()) {
    auto act = p_menu->addAction(MainWindow::tr("Quick Access Not Set"));
    act->setEnabled(false);
    return;
  }

  for (const auto &file : quickAccess) {
    auto act = new QWidgetAction(p_menu);
    QString displayName = PathUtils::fileName(file);
    QString displayFullName = VxUrlUtils::getFilePathFromVxURL(file);

    auto widget = new LabelWithButtonsWidget(displayName, LabelWithButtonsWidget::Delete);
    p_menu->connect(widget, &LabelWithButtonsWidget::triggered, p_menu, [p_menu, act]() {
      const auto qaFile = act->data().toString();
      auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
      sessionConfig.removeQuickAccessFile(qaFile);
      p_menu->removeAction(act);
      if (p_menu->isEmpty()) {
        p_menu->hide();
      }
    });
    // @act will own @widget.
    act->setDefaultWidget(widget);
    act->setData(file);
    act->setToolTip(displayFullName);

    // Must call after setDefaultWidget().
    p_menu->addAction(act);
  }
}

void ToolBarHelper::setupExpandButton(MainWindow *p_win, QToolBar *p_toolBar) {
  const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

  auto btn = WidgetsFactory::createToolButton(p_toolBar);

  auto defaultText = MainWindow::tr("Expand Content Area");
  auto expandAct = new QAction(generateIcon("expand.svg"), defaultText, btn);
  WidgetUtils::addActionShortcut(expandAct,
                                 coreConfig.getShortcut(CoreConfig::Shortcut::ExpandContentArea));
  expandAct->setCheckable(true);
  MainWindow::connect(expandAct, &QAction::triggered, p_win, &MainWindow::setContentAreaExpanded);
  MainWindow::connect(p_win, &MainWindow::layoutChanged, [expandAct, p_win]() {
    expandAct->setChecked(p_win->isContentAreaExpanded());
  });
  btn->addAction(expandAct);
  btn->setDefaultAction(expandAct);
  btn->setText(defaultText);

  auto menu = WidgetsFactory::createMenu(p_toolBar);
  btn->setMenu(menu);

  {
    auto fullScreenAct = new FullScreenToggleAction(p_win, generateIcon("fullscreen.svg"), menu);
    const auto shortcut = coreConfig.getShortcut(CoreConfig::Shortcut::FullScreen);
    WidgetUtils::addActionShortcut(fullScreenAct, shortcut);
    MainWindow::connect(fullScreenAct, &FullScreenToggleAction::fullScreenToggled, p_win,
                        [shortcut](bool p_fullScreen) {
                          if (p_fullScreen) {
                            VNoteX::getInst().showTips(
                                MainWindow::tr("Press %1 To Exit Full Screen").arg(shortcut));
                          } else {
                            VNoteX::getInst().showTips("");
                          }
                        });
    menu->addAction(fullScreenAct);
  }

  auto stayOnTopAct =
      menu->addAction(generateIcon("stay_on_top.svg"), MainWindow::tr("Stay on Top"), p_win,
                      &MainWindow::setStayOnTop);
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
      MainWindow::connect(act, &QAction::toggled, actWrapper, [actWrapper](bool checked) {
        if (actWrapper->isChecked() != checked) {
          actWrapper->setChecked(checked);
        }
      });
      MainWindow::connect(actWrapper, &QAction::triggered, act, [p_win, act]() {
        act->trigger();
        p_win->updateDockWidgetTabBar();
      });
    }
  }

  p_toolBar->addWidget(btn);
}

void ToolBarHelper::setupSettingsButton(MainWindow *p_win, QToolBar *p_toolBar) {
  const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();

  auto btn = WidgetsFactory::createToolButton(p_toolBar);

  auto defaultText = MainWindow::tr("Settings");
  auto settingsAct = new QAction(generateIcon("settings.svg"), defaultText, btn);
  QAction::connect(settingsAct, &QAction::triggered, [p_win]() {
    // LEGACY: SettingsDialog now requires ServiceLocator.
    // Use MainWindow2 code path for settings dialog.
    Q_UNUSED(p_win);
    qDebug() << "Settings dialog not available in legacy code path";
  });
  WidgetUtils::addActionShortcut(settingsAct,
                                 coreConfig.getShortcut(CoreConfig::Shortcut::Settings));
  btn->addAction(settingsAct);
  btn->setDefaultAction(settingsAct);
  btn->setText(defaultText);

  auto menu = WidgetsFactory::createMenu(p_toolBar);
  btn->setMenu(menu);

  menu->addAction(MainWindow::tr("Open Configuration Folder"), menu, []() {
    auto folderPath = ConfigMgr::getInst().getConfigDataFolder(ConfigMgr::ConfigDataType::Main);
    WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(folderPath));
  });

  menu->addAction(MainWindow::tr("Reset Main Window Layout"), menu,
                  [p_win]() { p_win->resetStateAndGeometry(); });

  menu->addAction(MainWindow::tr("View Logs"), menu, []() {
    const auto file = ConfigMgr::getInst().getLogFile();
    if (QFileInfo::exists(file)) {
      auto paras = QSharedPointer<FileOpenParameters>::create();
      paras->m_readOnly = true;
      paras->m_sessionEnabled = false;
      emit VNoteX::getInst().openFileRequested(file, paras);
    }
  });

  menu->addSeparator();

  menu->addAction(MainWindow::tr("About"), menu, [p_win]() {
    auto info = MainWindow::tr("<h3>%1</h3>\n<span>%2</span>\n")
                    .arg(qApp->applicationDisplayName(), qApp->applicationVersion());
    const auto text = DocsUtils::getDocText(QStringLiteral("about_vnotex.txt"));
    QMessageBox::about(p_win, MainWindow::tr("About"), info + text);
  });

  menu->addAction(MainWindow::tr("Check for Updates"), menu, [p_win]() {
    Updater updater(p_win);
    updater.exec();
  });

  menu->addAction(MainWindow::tr("Restart"), menu, [p_win]() { p_win->restart(); });

  auto quitAct = menu->addAction(MainWindow::tr("Quit"), menu, [p_win]() { p_win->quitApp(); });
  quitAct->setMenuRole(QAction::QuitRole);
  WidgetUtils::addActionShortcut(quitAct, coreConfig.getShortcut(CoreConfig::Shortcut::Quit));

  p_toolBar->addWidget(btn);
}

void ToolBarHelper::activateQuickAccess(const QString &p_file) {
  if (p_file.startsWith('#')) {
    activateQuickAccessFromVxUrl(p_file);
  } else {
    activateQuickAccessFilePath(p_file);
  }
}

void ToolBarHelper::activateQuickAccessFilePath(const QString &p_file) {
  const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
  auto paras = QSharedPointer<FileOpenParameters>::create();
  paras->m_mode = coreConfig.getDefaultOpenMode();

  emit VNoteX::getInst().openFileRequested(p_file, paras);
}

void ToolBarHelper::activateQuickAccessFromVxUrl(const QString &p_vx_url) {
  auto notebook = VNoteX::getInst().getNotebookMgr().getCurrentNotebook();
  if (!notebook) {
    return;
  }

  // get 'signature' from format '#signature:filename'
  QString signature = VxUrlUtils::getSignatureFromVxURL(p_vx_url);

  // get FilePath from Signature from currentNotebook
  const QString rootPath = notebook->getRootFolderAbsolutePath();
  const QString filePath = VxUrlUtils::getFilePathFromSignature(rootPath, signature);

  const auto &coreConfig = ConfigMgr::getInst().getCoreConfig();
  auto paras = QSharedPointer<FileOpenParameters>::create();
  paras->m_mode = coreConfig.getDefaultOpenMode();

  if (filePath.isEmpty()) {
    return;
  }

  emit VNoteX::getInst().openFileRequested(filePath, paras);
}
