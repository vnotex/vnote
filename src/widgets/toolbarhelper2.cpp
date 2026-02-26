#include "toolbarhelper2.h"

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

#include "fullscreentoggleaction.h"
#include "labelwithbuttonswidget.h"
#include "mainwindow2.h"
#include "messageboxhelper.h"
#include "propertydefs.h"
#include "widgetsfactory.h"
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/exception.h>
#include <core/fileopenparameters.h>
#include <core/servicelocator.h>
#include <core/sessionconfig.h>
#include <gui/services/themeservice.h>
#include <utils/docsutils.h>
#include <utils/iconutils.h>
#include <utils/pathutils.h>
#include <utils/vxurlutils.h>
#include <utils/widgetutils.h>

using namespace vnotex;

static const QString c_fgPalette = QStringLiteral("widgets#toolbar#icon#fg");
static const QString c_disabledPalette = QStringLiteral("widgets#toolbar#icon#disabled#fg");
static const QString c_dangerousPalette = QStringLiteral("widgets#toolbar#icon#danger#fg");

ToolBarHelper2::ToolBarHelper2(ServiceLocator &p_services, MainWindow2 *p_mainWindow)
    : QObject(p_mainWindow), m_services(p_services), m_mainWindow(p_mainWindow) {}

QToolBar *ToolBarHelper2::createToolBar(const QString &p_title, const QString &p_name) {
  auto tb = m_mainWindow->addToolBar(p_title);
  tb->setObjectName(p_name);
  tb->setMovable(false);
  return tb;
}

QToolBar *ToolBarHelper2::setupFileToolBar(QToolBar *p_toolBar) {
  auto tb = p_toolBar;
  if (!tb) {
    tb = createToolBar(MainWindow2::tr("File"), "FileToolBar");
  }

  const auto &coreConfig = m_services.get<ConfigMgr2>()->getCoreConfig();

  // New Note.
  {
    auto newBtn = WidgetsFactory::createToolButton(tb);
    newBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    // New note.
    const auto text = MainWindow2::tr("New Note");
    auto newNoteAct = new QAction(generateIcon("new_note.svg"), text, newBtn);
    QAction::connect(newNoteAct, &QAction::triggered, m_mainWindow,
                     [this]() { emit m_mainWindow->newNoteRequested(); });
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
    auto newQuickNoteAct =
        newMenu->addAction(MainWindow2::tr("Quick Note"), newMenu,
                           [this]() { emit m_mainWindow->newQuickNoteRequested(); });
    WidgetUtils::addActionShortcut(newQuickNoteAct,
                                   coreConfig.getShortcut(CoreConfig::Shortcut::NewQuickNote));

    // New folder.
    auto newFolderAct = newMenu->addAction(MainWindow2::tr("New Folder"), newMenu,
                                           [this]() { emit m_mainWindow->newFolderRequested(); });
    WidgetUtils::addActionShortcut(newFolderAct,
                                   coreConfig.getShortcut(CoreConfig::Shortcut::NewFolder));

    newMenu->addSeparator();

    // Import file.
    newMenu->addAction(MainWindow2::tr("Import File"), newMenu,
                       [this]() { emit m_mainWindow->importFileRequested(); });

    // Import folder.
    newMenu->addAction(MainWindow2::tr("Import Folder"), newMenu,
                       [this]() { emit m_mainWindow->importFolderRequested(); });

    auto exportAct = newMenu->addAction(MainWindow2::tr("Export (Convert Format)"), newMenu,
                                        [this]() { emit m_mainWindow->exportRequested(); });
    WidgetUtils::addActionShortcut(exportAct, coreConfig.getShortcut(CoreConfig::Shortcut::Export));

    newMenu->addSeparator();

    // Open file.
    newMenu->addAction(MainWindow2::tr("Open File"), newMenu, [this]() {
      static QString lastDirPath = QDir::homePath();
      auto files =
          QFileDialog::getOpenFileNames(m_mainWindow, MainWindow2::tr("Open File"), lastDirPath);
      if (files.isEmpty()) {
        return;
      }

      lastDirPath = QFileInfo(files[0]).path();

      for (const auto &file : files) {
        emit m_mainWindow->openFileRequested(file, QSharedPointer<FileOpenParameters>::create());
      }
    });

    tb->addWidget(newBtn);
  }

  // Quick Access.
  {
    auto toolBtn = WidgetsFactory::createToolButton(tb);
    toolBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    const auto text = MainWindow2::tr("Quick Access");
    auto quickAccessAct = new QAction(generateIcon("quick_access_menu.svg"), text, toolBtn);
    MainWindow2::connect(quickAccessAct, &QAction::triggered, m_mainWindow, [this]() {
      const auto &quickAccess =
          m_services.get<ConfigMgr2>()->getSessionConfig().getQuickAccessFiles();
      if (quickAccess.isEmpty()) {
        MessageBoxHelper::notify(
            MessageBoxHelper::Type::Information,
            MainWindow2::tr("Please pin files to Quick Access first."),
            MainWindow2::tr("Files could be pinned to Quick Access via context menu."),
            MainWindow2::tr("Quick Access could be managed in the Settings dialog."), m_mainWindow);
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

    MainWindow2::connect(btnMenu, &QMenu::aboutToShow, btnMenu,
                         [this, btnMenu]() { updateQuickAccessMenu(btnMenu); });
    MainWindow2::connect(btnMenu, &QMenu::triggered, btnMenu,
                         [this](QAction *p_act) { activateQuickAccess(p_act->data().toString()); });
    tb->addWidget(toolBtn);
  }

  return tb;
}

QToolBar *ToolBarHelper2::setupSettingsToolBar(QToolBar *p_toolBar) {
  auto tb = p_toolBar;
  if (!tb) {
    tb = createToolBar(MainWindow2::tr("Settings"), "SettingsToolBar");
  }

  addSpacer(tb);

  setupExpandButton(tb);

  setupSettingsButton(tb);

  return tb;
}

QIcon ToolBarHelper2::generateIcon(const QString &p_iconName) {
  static QVector<IconUtils::OverriddenColor> colors;

  auto *themeService = m_services.get<ThemeService>();

  if (colors.isEmpty()) {
    const auto fg = themeService->paletteColor(c_fgPalette);
    const auto disabledFg = themeService->paletteColor(c_disabledPalette);

    colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
    colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));
  }

  auto iconFile = themeService->getIconFile(p_iconName);
  return IconUtils::fetchIcon(iconFile, colors);
}

QIcon ToolBarHelper2::generateDangerousIcon(const QString &p_iconName) {
  auto *themeService = m_services.get<ThemeService>();
  const auto fg = themeService->paletteColor(c_fgPalette);
  const auto disabledFg = themeService->paletteColor(c_disabledPalette);
  const auto dangerousFg = themeService->paletteColor(c_dangerousPalette);

  QVector<IconUtils::OverriddenColor> colors;
  colors.push_back(IconUtils::OverriddenColor(fg, QIcon::Normal));
  colors.push_back(IconUtils::OverriddenColor(disabledFg, QIcon::Disabled));
  colors.push_back(IconUtils::OverriddenColor(dangerousFg, QIcon::Active));

  auto iconFile = themeService->getIconFile(p_iconName);
  return IconUtils::fetchIcon(iconFile, colors);
}

void ToolBarHelper2::setupToolBars() {
  setupFileToolBar(nullptr);
  setupSettingsToolBar(nullptr);
}

void ToolBarHelper2::setupToolBars(QToolBar *p_toolBar) {
  p_toolBar->setObjectName(QStringLiteral("UnifiedToolBar"));
  p_toolBar->setMovable(false);
  m_mainWindow->addToolBar(p_toolBar);

  setupFileToolBar(p_toolBar);
  setupSettingsToolBar(p_toolBar);
}

void ToolBarHelper2::addSpacer(QToolBar *p_toolBar) {
  auto spacer = new QWidget(p_toolBar);
  spacer->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
  auto act = p_toolBar->addWidget(spacer);
  act->setEnabled(false);
}

void ToolBarHelper2::updateQuickAccessMenu(QMenu *p_menu) {
  p_menu->clear();
  auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();

  const auto &quickAccess = sessionConfig.getQuickAccessFiles();
  if (quickAccess.isEmpty()) {
    auto act = p_menu->addAction(MainWindow2::tr("Quick Access Not Set"));
    act->setEnabled(false);
    return;
  }

  for (const auto &file : quickAccess) {
    auto act = new QWidgetAction(p_menu);
    QString displayName = PathUtils::fileName(file);
    QString displayFullName = VxUrlUtils::getFilePathFromVxURL(file);

    auto widget = new LabelWithButtonsWidget(displayName, LabelWithButtonsWidget::Delete);
    p_menu->connect(widget, &LabelWithButtonsWidget::triggered, p_menu, [p_menu, act, this]() {
      const auto qaFile = act->data().toString();
      auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
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

void ToolBarHelper2::setupExpandButton(QToolBar *p_toolBar) {
  const auto &coreConfig = m_services.get<ConfigMgr2>()->getCoreConfig();

  auto btn = WidgetsFactory::createToolButton(p_toolBar);

  auto defaultText = MainWindow2::tr("Expand Content Area");
  auto expandAct = new QAction(generateIcon("expand.svg"), defaultText, btn);
  WidgetUtils::addActionShortcut(expandAct,
                                 coreConfig.getShortcut(CoreConfig::Shortcut::ExpandContentArea));
  expandAct->setCheckable(true);
  MainWindow2::connect(expandAct, &QAction::triggered, m_mainWindow,
                       &MainWindow2::setContentAreaExpanded);
  MainWindow2::connect(m_mainWindow, &MainWindow2::layoutChanged, expandAct,
                       [expandAct, this]() { expandAct->setChecked(m_mainWindow->isContentAreaExpanded()); });
  btn->addAction(expandAct);
  btn->setDefaultAction(expandAct);
  btn->setText(defaultText);

  auto menu = WidgetsFactory::createMenu(p_toolBar);
  btn->setMenu(menu);

  {
    auto fullScreenAct =
        new FullScreenToggleAction(m_mainWindow, generateIcon("fullscreen.svg"), menu);
    const auto shortcut = coreConfig.getShortcut(CoreConfig::Shortcut::FullScreen);
    WidgetUtils::addActionShortcut(fullScreenAct, shortcut);
    MainWindow2::connect(fullScreenAct, &FullScreenToggleAction::fullScreenToggled, m_mainWindow,
                         [shortcut, this](bool p_fullScreen) {
                           // TODO: showTips not yet available in MainWindow2
                           Q_UNUSED(p_fullScreen);
                           Q_UNUSED(shortcut);
                         });
    menu->addAction(fullScreenAct);
  }

  auto stayOnTopAct = menu->addAction(generateIcon("stay_on_top.svg"),
                                      MainWindow2::tr("Stay on Top"), m_mainWindow,
                                      &MainWindow2::setStayOnTop);
  stayOnTopAct->setCheckable(true);
  WidgetUtils::addActionShortcut(stayOnTopAct,
                                 coreConfig.getShortcut(CoreConfig::Shortcut::StayOnTop));

  menu->addSeparator();

  {
    // Windows.
    auto subMenu = menu->addMenu(MainWindow2::tr("Windows"));
    for (auto dock : m_mainWindow->getDocks()) {
      // @act is owned by the QDockWidget.
      auto act = dock->toggleViewAction();
      auto actWrapper = subMenu->addAction(act->text());
      actWrapper->setCheckable(act->isCheckable());
      actWrapper->setChecked(act->isChecked());
      MainWindow2::connect(act, &QAction::toggled, actWrapper, [actWrapper](bool checked) {
        if (actWrapper->isChecked() != checked) {
          actWrapper->setChecked(checked);
        }
      });
      MainWindow2::connect(actWrapper, &QAction::triggered, act, [act]() { act->trigger(); });
    }
  }

  p_toolBar->addWidget(btn);
}

void ToolBarHelper2::setupSettingsButton(QToolBar *p_toolBar) {
  const auto &coreConfig = m_services.get<ConfigMgr2>()->getCoreConfig();

  auto btn = WidgetsFactory::createToolButton(p_toolBar);

  auto defaultText = MainWindow2::tr("Settings");
  auto settingsAct = new QAction(generateIcon("settings.svg"), defaultText, btn);
  QAction::connect(settingsAct, &QAction::triggered, [this]() {
    // TODO: SettingsDialog not yet migrated to DI architecture.
    // Stub for now - will be implemented when SettingsDialog is migrated.
    qDebug() << "Settings dialog requested (stub)";
  });
  WidgetUtils::addActionShortcut(settingsAct,
                                 coreConfig.getShortcut(CoreConfig::Shortcut::Settings));
  btn->addAction(settingsAct);
  btn->setDefaultAction(settingsAct);
  btn->setText(defaultText);

  auto menu = WidgetsFactory::createMenu(p_toolBar);
  btn->setMenu(menu);

  menu->addAction(MainWindow2::tr("Open Configuration Folder"), menu, [this]() {
    auto folderPath =
        m_services.get<ConfigMgr2>()->getConfigDataFolder(ConfigMgr2::ConfigDataType::Main);
    WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(folderPath));
  });

  menu->addAction(MainWindow2::tr("Reset Main Window Layout"), menu,
                  [this]() { m_mainWindow->resetStateAndGeometry(); });

  menu->addAction(MainWindow2::tr("View Logs"), menu, [this]() {
    const auto file = m_services.get<ConfigMgr2>()->getLogFile();
    if (QFileInfo::exists(file)) {
      auto paras = QSharedPointer<FileOpenParameters>::create();
      paras->m_readOnly = true;
      paras->m_sessionEnabled = false;
      emit m_mainWindow->openFileRequested(file, paras);
    }
  });

  menu->addSeparator();

  menu->addAction(MainWindow2::tr("About"), menu, [this]() {
    auto info = MainWindow2::tr("<h3>%1</h3>\n<span>%2</span>\n")
                    .arg(qApp->applicationDisplayName(), qApp->applicationVersion());
    const auto text = DocsUtils::getDocText(QStringLiteral("about_vnotex.txt"));
    QMessageBox::about(m_mainWindow, MainWindow2::tr("About"), info + text);
  });

  // TODO: Updater dialog not yet migrated.
  menu->addAction(MainWindow2::tr("Check for Updates"), menu, [this]() {
    qDebug() << "Check for updates requested (stub)";
  });

  menu->addAction(MainWindow2::tr("Restart"), menu, [this]() {
    m_mainWindow->restart();
  });

  auto quitAct =
      menu->addAction(MainWindow2::tr("Quit"), menu, [this]() { m_mainWindow->close(); });
  quitAct->setMenuRole(QAction::QuitRole);
  WidgetUtils::addActionShortcut(quitAct, coreConfig.getShortcut(CoreConfig::Shortcut::Quit));

  p_toolBar->addWidget(btn);
}

void ToolBarHelper2::activateQuickAccess(const QString &p_file) {
  if (p_file.startsWith('#')) {
    activateQuickAccessFromVxUrl(p_file);
  } else {
    activateQuickAccessFilePath(p_file);
  }
}

void ToolBarHelper2::activateQuickAccessFilePath(const QString &p_file) {
  const auto &coreConfig = m_services.get<ConfigMgr2>()->getCoreConfig();
  auto paras = QSharedPointer<FileOpenParameters>::create();
  paras->m_mode = coreConfig.getDefaultOpenMode();

  emit m_mainWindow->openFileRequested(p_file, paras);
}

void ToolBarHelper2::activateQuickAccessFromVxUrl(const QString &p_vxUrl) {
  // TODO: Need NotebookService to get current notebook.
  // For now, just try to open as a regular file path.
  Q_UNUSED(p_vxUrl);
  qDebug() << "activateQuickAccessFromVxUrl not fully implemented yet:" << p_vxUrl;
}
