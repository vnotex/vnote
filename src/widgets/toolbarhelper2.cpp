#include "toolbarhelper2.h"

#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QWhatsThis>

#include "dialogs/settings/newquickaccessitemdialog.h"
#include "fullscreentoggleaction.h"
#include "mainwindow2.h"
#include "messageboxhelper.h"
#include "settingswidget.h"
#include "viewarea2.h"
#include "widgetsfactory.h"
#include <controllers/viewareacontroller.h>
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/exception.h>
#include <core/fileopensettings.h>
#include <core/nodeidentifier.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/sessionconfig.h>
#include <gui/services/themeservice.h>
#include <gui/utils/iconutils.h>
#include <unitedentry/unitedentry.h>
#include <unitedentry/unitedentrymgr.h>
#include <utils/pathutils.h>
#include <utils/vxurlutils.h>
#include <utils/widgetutils.h>

using namespace vnotex;

static const QString c_fgPalette = QStringLiteral("widgets#toolbar#icon#fg");
static const QString c_disabledPalette = QStringLiteral("widgets#toolbar#icon#disabled#fg");
static const QString c_dangerousPalette = QStringLiteral("widgets#toolbar#icon#danger#fg");

namespace {

ViewWindowMode resolveOpenMode(QuickAccessOpenMode p_qaMode, const CoreConfig &p_coreConfig) {
  switch (p_qaMode) {
  case QuickAccessOpenMode::Read:
    return ViewWindowMode::Read;
  case QuickAccessOpenMode::Edit:
    return ViewWindowMode::Edit;
  case QuickAccessOpenMode::Default:
  default:
    return p_coreConfig.getDefaultOpenMode();
  }
}

QString quickAccessModeSuffix(QuickAccessOpenMode p_mode) {
  switch (p_mode) {
  case QuickAccessOpenMode::Read:
    return QObject::tr(" (Read)");
  case QuickAccessOpenMode::Edit:
    return QObject::tr(" (Edit)");
  case QuickAccessOpenMode::Default:
  default:
    return QString();
  }
}

} // namespace

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
    auto newQuickNoteAct = newMenu->addAction(MainWindow2::tr("Quick Note"), newMenu, [this]() {
      emit m_mainWindow->newQuickNoteRequested();
    });
    WidgetUtils::addActionShortcut(newQuickNoteAct,
                                   coreConfig.getShortcut(CoreConfig::Shortcut::NewQuickNote));

    // New folder.
    auto newFolderAct = newMenu->addAction(MainWindow2::tr("New Folder"), newMenu,
                                           [this]() { emit m_mainWindow->newFolderRequested(); });
    WidgetUtils::addActionShortcut(newFolderAct,
                                   coreConfig.getShortcut(CoreConfig::Shortcut::NewFolder));

    newMenu->addSeparator();

    // Import file.
    newMenu->addAction(MainWindow2::tr("Import Files"), newMenu,
                       [this]() { emit m_mainWindow->importFileRequested(); });

    // Import folder.
    newMenu->addAction(MainWindow2::tr("Import Folder"), newMenu,
                       [this]() { emit m_mainWindow->importFolderRequested(); });

    auto exportAct = newMenu->addAction(MainWindow2::tr("Export"), newMenu,
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

      auto *bufferSvc = m_services.get<BufferService>();
      if (bufferSvc) {
        FileOpenSettings settings;
        settings.m_mode = m_services.get<ConfigMgr2>()->getCoreConfig().getDefaultOpenMode();
        for (const auto &file : files) {
          NodeIdentifier nodeId;
          nodeId.relativePath = file;
          bufferSvc->openBuffer(nodeId, settings);
        }
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
      auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
      const auto &quickAccessItems = sessionConfig.getQuickAccessItems();
      if (quickAccessItems.isEmpty()) {
        NewQuickAccessItemDialog dialog(m_services, m_mainWindow);
        if (dialog.exec() == QDialog::Accepted) {
          auto items = sessionConfig.getQuickAccessItems();
          const auto newItem = dialog.getItem();
          items.append(newItem);
          sessionConfig.setQuickAccessItems(items);
          activateQuickAccess(newItem);
        }
        return;
      }

      activateQuickAccess(quickAccessItems.first());
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
    MainWindow2::connect(btnMenu, &QMenu::triggered, btnMenu, [this](QAction *p_act) {
      if (!p_act || !p_act->data().isValid()) {
        return;
      }

      const auto index = p_act->data().toInt();
      const auto &items = m_services.get<ConfigMgr2>()->getSessionConfig().getQuickAccessItems();
      if (index < 0 || index >= items.size()) {
        return;
      }

      activateQuickAccess(items[index]);
    });
    tb->addWidget(toolBtn);
  }

  return tb;
}

QToolBar *ToolBarHelper2::setupSettingsToolBar(QToolBar *p_toolBar) {
  auto tb = p_toolBar;
  if (!tb) {
    tb = createToolBar(MainWindow2::tr("Settings"), "SettingsToolBar");
  }

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
  setupUnitedEntry(nullptr);
  setupSettingsToolBar(nullptr);
}

void ToolBarHelper2::setupToolBars(QToolBar *p_toolBar) {
  p_toolBar->setObjectName(QStringLiteral("UnifiedToolBar"));
  p_toolBar->setMovable(false);
  m_mainWindow->addToolBar(p_toolBar);

  setupFileToolBar(p_toolBar);
  setupUnitedEntry(p_toolBar);
  setupSettingsToolBar(p_toolBar);
}

void ToolBarHelper2::setupUnitedEntry(QToolBar *p_toolBar) {
  auto tb = p_toolBar;
  if (!tb) {
    tb = createToolBar(MainWindow2::tr("United Entry"), "UnitedEntryToolBar");
  }

  // Create the UnitedEntryMgr controller.
  m_unitedEntryMgr = new UnitedEntryMgr(m_services, this);
  m_unitedEntryMgr->init();

  // Create the UnitedEntry toolbar widget.
  m_unitedEntry = new UnitedEntry(m_services, m_unitedEntryMgr, tb);

  // Left spacer.
  addSpacer(tb);

  // Add the UnitedEntry widget.
  tb->addWidget(m_unitedEntry);

  // Right spacer.
  addSpacer(tb);

  // Create activation action (Ctrl+G,G shortcut).
  auto *activateAct = m_unitedEntry->getActivateAction();
  m_mainWindow->addAction(activateAct);
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

  const auto &quickAccessItems = sessionConfig.getQuickAccessItems();
  if (quickAccessItems.isEmpty()) {
    auto act = p_menu->addAction(MainWindow2::tr("Quick Access Not Set"));
    act->setEnabled(false);
  } else {
    for (int i = 0; i < quickAccessItems.size(); ++i) {
      const auto &item = quickAccessItems[i];
      QString displayName = PathUtils::fileName(item.m_path);
      QString displayFullName =
          VxUrlUtils::getFilePathFromVxURL(item.m_path) + quickAccessModeSuffix(item.m_openMode);

      auto act = p_menu->addAction(displayName);
      act->setToolTip(displayFullName);
      act->setData(i);
    }
  }

  p_menu->addSeparator();

  auto newAct = p_menu->addAction(MainWindow2::tr("New Quick Access"));
  QAction::connect(newAct, &QAction::triggered, p_menu, [this]() {
    auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
    NewQuickAccessItemDialog dialog(m_services, m_mainWindow);
    if (dialog.exec() == QDialog::Accepted) {
      auto items = sessionConfig.getQuickAccessItems();
      items.append(dialog.getItem());
      sessionConfig.setQuickAccessItems(items);
    }
  });
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
  MainWindow2::connect(m_mainWindow, &MainWindow2::layoutChanged, expandAct, [expandAct, this]() {
    expandAct->setChecked(m_mainWindow->isContentAreaExpanded());
  });
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

  auto stayOnTopAct =
      menu->addAction(generateIcon("stay_on_top.svg"), MainWindow2::tr("Stay on Top"), m_mainWindow,
                      &MainWindow2::setStayOnTop);
  stayOnTopAct->setCheckable(true);
  WidgetUtils::addActionShortcut(stayOnTopAct,
                                 coreConfig.getShortcut(CoreConfig::Shortcut::StayOnTop));

  menu->addSeparator();

  {
    // Windows.
    auto subMenu = menu->addMenu(MainWindow2::tr("Windows"));
    for (auto dock : m_mainWindow->getDocks()) {
      if (!dock) {
        continue;
      }
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
    auto *content = new SettingsWidget(m_services, m_mainWindow);
    auto *controller = m_mainWindow->getViewArea()->getController();
    controller->openWidgetContent(content);
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
      auto *bufferSvc = m_services.get<BufferService>();
      if (bufferSvc) {
        NodeIdentifier nodeId;
        nodeId.relativePath = file;
        FileOpenSettings settings;
        settings.m_readOnly = true;
        bufferSvc->openBuffer(nodeId, settings);
      }
    } else {
      MessageBoxHelper::notify(MessageBoxHelper::Type::Information,
                               MainWindow2::tr("No log file found."), m_mainWindow);
    }
  });

  menu->addSeparator();

  menu->addAction(MainWindow2::tr("About"), menu, [this]() {
    auto info = MainWindow2::tr("<h3>%1</h3><h4>%2</h4>")
                    .arg(qApp->applicationDisplayName(), qApp->applicationVersion());
    const auto text = MainWindow2::tr(
        "A pleasant note-taking platform, focusing on native experience, open source since 2016.");
    QMessageBox::about(m_mainWindow, MainWindow2::tr("About"), info + text);
  });

  // TODO: Updater dialog not yet migrated.
  menu->addAction(MainWindow2::tr("Check for Updates"), menu,
                  [this]() { qDebug() << "Check for updates requested (stub)"; });

  menu->addAction(MainWindow2::tr("Restart"), menu, [this]() { m_mainWindow->restart(); });

  auto quitAct =
      menu->addAction(MainWindow2::tr("Quit"), menu, [this]() { m_mainWindow->close(); });
  quitAct->setMenuRole(QAction::QuitRole);
  WidgetUtils::addActionShortcut(quitAct, coreConfig.getShortcut(CoreConfig::Shortcut::Quit));

  p_toolBar->addWidget(btn);
}

UnitedEntryMgr *ToolBarHelper2::unitedEntryMgr() const { return m_unitedEntryMgr; }

void ToolBarHelper2::activateQuickAccess(const SessionConfig::QuickAccessItem &p_item) {
  // UUID-first: try to open by UUID if available.
  if (!p_item.m_uuid.isEmpty()) {
    const auto mode =
        resolveOpenMode(p_item.m_openMode, m_services.get<ConfigMgr2>()->getCoreConfig());
    FileOpenSettings settings;
    settings.m_mode = mode;
    auto *bufferSvc = m_services.get<BufferService>();
    Buffer2 buf = bufferSvc->openBufferByNodeId(p_item.m_uuid, settings);
    if (buf.isValid()) {
      return;
    }
    // UUID resolution failed — fall through to path-based logic.
    qDebug() << "UUID resolution failed for" << p_item.m_uuid << ", falling back to path";
  }

  const auto mode =
      resolveOpenMode(p_item.m_openMode, m_services.get<ConfigMgr2>()->getCoreConfig());
  if (p_item.m_path.startsWith('#')) {
    activateQuickAccessFromVxUrl(p_item.m_path);
  } else {
    activateQuickAccessFilePath(p_item.m_path, mode);
  }
}

void ToolBarHelper2::activateQuickAccessFilePath(const QString &p_file, ViewWindowMode p_mode) {
  auto *bufferSvc = m_services.get<BufferService>();
  if (!bufferSvc) {
    return;
  }

  NodeIdentifier nodeId;
  nodeId.relativePath = p_file;
  FileOpenSettings settings;
  settings.m_mode = p_mode;
  bufferSvc->openBuffer(nodeId, settings);
}

void ToolBarHelper2::activateQuickAccessFromVxUrl(const QString &p_vxUrl) {
  // TODO: Need NotebookService to get current notebook.
  // For now, just try to open as a regular file path.
  Q_UNUSED(p_vxUrl);
  qDebug() << "activateQuickAccessFromVxUrl not fully implemented yet:" << p_vxUrl;
}
