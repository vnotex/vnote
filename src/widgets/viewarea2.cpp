#include "viewarea2.h"

#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QShortcut>
#include <QSplitter>
#include <QStackedLayout>
#include <QTimer>
#include <QVBoxLayout>

#include <controllers/viewareacontroller.h>
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/iviewwindowcontent.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/hookmanager.h>
#include <core/services/workspacecoreservice.h>
#include <gui/services/themeservice.h>
#include <gui/services/viewwindowfactory.h>
#include <gui/utils/widgetutils.h>

#include "viewareahomewidget.h"
#include "viewsplit2.h"
#include "viewwindow2.h"
#include "widgetviewwindow2.h"

using namespace vnotex;

ViewArea2::ViewArea2(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent),
      NavigationMode(NavigationMode::Type::DoubleKeys, this, p_services.get<ThemeService>()),
      m_services(p_services) {
  setContentsMargins(0, 0, 0, 0);

  m_stackedLayout = new QStackedLayout(this);
  m_stackedLayout->setContentsMargins(0, 0, 0, 0);

  // Page 0: Home screen.
  m_homeWidget = new ViewAreaHomeWidget(this);
  m_stackedLayout->addWidget(m_homeWidget);

  // Page 1: Contents screen (container for the splitter tree).
  m_contentsWidget = new QWidget(this);
  m_contentsWidget->setContentsMargins(0, 0, 0, 0);
  m_contentsLayout = new QVBoxLayout(m_contentsWidget);
  m_contentsLayout->setContentsMargins(0, 0, 0, 0);
  m_stackedLayout->addWidget(m_contentsWidget);

  // Start on the Home screen.
  m_stackedLayout->setCurrentIndex(HomeScreen);

  setupController();
  setupShortcuts();
}

ViewArea2::~ViewArea2() {}

void ViewArea2::setupController() {
  m_controller = new ViewAreaController(m_services, this);
  m_controller->setView(this);

  m_controller->subscribeToHooks();

  // Subscribe to application lifecycle hooks.
  auto *hookMgr = m_services.get<HookManager>();
  if (hookMgr) {
    hookMgr->addAction(
        HookNames::MainWindowAfterStart,
        [this](HookContext &p_ctx, const QVariantMap &p_args) {
          Q_UNUSED(p_ctx)
          Q_UNUSED(p_args)
          restoreSession();
          QTimer::singleShot(0, this, [this]() {
            // Re-enable propagation now that all deferred Qt signals (e.g.,
            // currentChanged posted by addTab during restore) have settled.
            m_controller->setShouldPropagateToCore(true);
            qDebug() << "ViewArea2: propagation to core re-enabled";
            // Re-sync current window from ground-truth UI state.
            // During restore, onViewWindowOpened() did NOT update m_currentWindowId
            // (propagation was disabled), so m_currentWindowId is still
            // InvalidViewWindowId — setCurrentViewWindow's early-return won't fire,
            // and the correct provider is propagated to the outline viewer.
            ViewWindow2 *currentWin = nullptr;
            auto *currentSplit = getCurrentViewSplit();
            if (!currentSplit && !m_splits.isEmpty()) {
              currentSplit = m_splits.first();
            }
            if (currentSplit) {
              currentWin = currentSplit->getCurrentViewWindow();
            }
            qDebug() << "ViewArea2: singleShot sync: currentSplit=" << currentSplit
                     << "currentWin=" << currentWin;
            ID winId = currentWin ? currentWin->getViewWindowId()
                                  : ViewAreaController::InvalidViewWindowId;
            QString bufferId = currentWin ? currentWin->getBuffer().id() : QString();
            m_controller->setCurrentViewWindow(winId, bufferId);
          });
        },
        10);
    hookMgr->addAction(
        HookNames::MainWindowBeforeClose,
        [this](HookContext &p_ctx, const QVariantMap &p_args) {
          Q_UNUSED(p_args)
          saveSession();
          // Close all buffers with save prompts.
          if (!m_controller->closeAllBuffersForQuit()) {
            // User cancelled — cancel the hook so MainWindow2 aborts close.
            p_ctx.cancel();
          }
        },
        10);
    hookMgr->addAction(
        HookNames::MainWindowShutdownCancelled,
        [this](HookContext &p_ctx, const QVariantMap &p_args) {
          Q_UNUSED(p_ctx)
          Q_UNUSED(p_args)
          qDebug() << "ViewArea2: shutdown cancelled, re-enabling propagation";
          m_controller->setShouldPropagateToCore(true);
        },
        10);
  }
}

void ViewArea2::setupShortcuts() {
  auto *configMgr = m_services.get<ConfigMgr2>();
  if (!configMgr) {
    return;
  }
  const auto &coreConfig = configMgr->getCoreConfig();

  // CloseTab.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::CloseTab), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *win = getCurrentViewWindow();
        if (win) {
          m_controller->closeViewWindow(win->getViewWindowId(), false);
        }
      });
    }
  }

  // CloseAllTabs.
  {
    auto shortcut = WidgetUtils::createShortcut(
        coreConfig.getShortcut(CoreConfig::Shortcut::CloseAllTabs), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *split = getCurrentViewSplit();
        if (split) {
          m_controller->closeTabs(split->getWorkspaceId(), -1, CloseTabMode::All);
        }
      });
    }
  }

  // CloseOtherTabs.
  {
    auto shortcut = WidgetUtils::createShortcut(
        coreConfig.getShortcut(CoreConfig::Shortcut::CloseOtherTabs), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *split = getCurrentViewSplit();
        if (split) {
          m_controller->closeTabs(split->getWorkspaceId(), split->currentIndex(),
                                  CloseTabMode::Others);
        }
      });
    }
  }

  // CloseTabsToTheLeft.
  {
    auto shortcut = WidgetUtils::createShortcut(
        coreConfig.getShortcut(CoreConfig::Shortcut::CloseTabsToTheLeft), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *split = getCurrentViewSplit();
        if (split) {
          m_controller->closeTabs(split->getWorkspaceId(), split->currentIndex(),
                                  CloseTabMode::ToTheLeft);
        }
      });
    }
  }

  // CloseTabsToTheRight.
  {
    auto shortcut = WidgetUtils::createShortcut(
        coreConfig.getShortcut(CoreConfig::Shortcut::CloseTabsToTheRight), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *split = getCurrentViewSplit();
        if (split) {
          m_controller->closeTabs(split->getWorkspaceId(), split->currentIndex(),
                                  CloseTabMode::ToTheRight);
        }
      });
    }
  }

  // LocateNode.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::LocateNode), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *win = getCurrentViewWindow();
        if (win) {
          const auto &nodeId = win->getNodeId();
          if (nodeId.isValid()) {
            emit m_controller->locateNodeRequested(nodeId);
          }
        }
      });
    }
  }

  // FocusContentArea.
  {
    auto shortcut = WidgetUtils::createShortcut(
        coreConfig.getShortcut(CoreConfig::Shortcut::FocusContentArea), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *win = getCurrentViewWindow();
        if (win) {
          win->setFocus();
        } else {
          setFocus();
        }
      });
    }
  }

  // OneSplitLeft.
  {
    auto shortcut = WidgetUtils::createShortcut(
        coreConfig.getShortcut(CoreConfig::Shortcut::OneSplitLeft), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *split = getCurrentViewSplit();
        if (split) {
          auto *target = findSplitByDirection(split, Direction::Left);
          if (target) {
            m_controller->setCurrentViewSplit(target->getWorkspaceId(), true);
          }
        }
      });
    }
  }

  // OneSplitDown.
  {
    auto shortcut = WidgetUtils::createShortcut(
        coreConfig.getShortcut(CoreConfig::Shortcut::OneSplitDown), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *split = getCurrentViewSplit();
        if (split) {
          auto *target = findSplitByDirection(split, Direction::Down);
          if (target) {
            m_controller->setCurrentViewSplit(target->getWorkspaceId(), true);
          }
        }
      });
    }
  }

  // OneSplitUp.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::OneSplitUp), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *split = getCurrentViewSplit();
        if (split) {
          auto *target = findSplitByDirection(split, Direction::Up);
          if (target) {
            m_controller->setCurrentViewSplit(target->getWorkspaceId(), true);
          }
        }
      });
    }
  }

  // OneSplitRight.
  {
    auto shortcut = WidgetUtils::createShortcut(
        coreConfig.getShortcut(CoreConfig::Shortcut::OneSplitRight), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *split = getCurrentViewSplit();
        if (split) {
          auto *target = findSplitByDirection(split, Direction::Right);
          if (target) {
            m_controller->setCurrentViewSplit(target->getWorkspaceId(), true);
          }
        }
      });
    }
  }

  // OpenLastClosedFile — skipped: depends on legacy HistoryMgr singleton (not yet migrated).
}

ViewAreaController *ViewArea2::getController() const { return m_controller; }

ViewWindow2 *ViewArea2::getCurrentViewWindow() const {
  return windowForId(m_controller->getCurrentWindowId());
}

// ============ Split Factory ============

ViewSplit2 *ViewArea2::createViewSplit(const QString &p_workspaceId) {
  auto *split = new ViewSplit2(m_services, p_workspaceId, nullptr);
  split->setVisibleWorkspaceIdsFunc([this]() { return m_splits.keys(); });
  return split;
}

QSplitter *ViewArea2::createSplitter(Qt::Orientation p_orientation) {
  auto *splitter = new QSplitter(p_orientation, nullptr);
  splitter->setChildrenCollapsible(false);
  return splitter;
}

// ============ Layout Management ============

ViewSplit2 *ViewArea2::addFirstViewSplitWidget(const QString &p_workspaceId) {
  Q_ASSERT(!m_topWidget);
  auto *split = createViewSplit(p_workspaceId);
  m_splits[p_workspaceId] = split;
  setTopWidget(split);
  return split;
}

ViewSplit2 *ViewArea2::splitAt(ViewSplit2 *p_split, Direction p_direction,
                               const QString &p_workspaceId) {
  Qt::Orientation orient = (p_direction == Direction::Left || p_direction == Direction::Right)
                               ? Qt::Horizontal
                               : Qt::Vertical;
  bool insertBefore = (p_direction == Direction::Left || p_direction == Direction::Up);

  auto *newSplit = createViewSplit(p_workspaceId);
  m_splits[p_workspaceId] = newSplit;
  auto *parentSplitter = findParentSplitter(p_split);

  if (!parentSplitter) {
    auto *splitter = createSplitter(orient);
    m_contentsLayout->removeWidget(p_split);
    if (insertBefore) {
      splitter->addWidget(newSplit);
      splitter->addWidget(p_split);
    } else {
      splitter->addWidget(p_split);
      splitter->addWidget(newSplit);
    }
    setTopWidget(splitter);
  } else if (parentSplitter->orientation() == orient) {
    int idx = parentSplitter->indexOf(p_split);
    int insertIdx = insertBefore ? idx : idx + 1;
    parentSplitter->insertWidget(insertIdx, newSplit);
  } else {
    int idx = parentSplitter->indexOf(p_split);
    auto *subSplitter = createSplitter(orient);
    parentSplitter->insertWidget(idx, subSplitter);
    if (insertBefore) {
      subSplitter->addWidget(newSplit);
      subSplitter->addWidget(p_split);
    } else {
      subSplitter->addWidget(p_split);
      subSplitter->addWidget(newSplit);
    }
  }

  return newSplit;
}

void ViewArea2::removeViewSplitWidget(ViewSplit2 *p_split) {
  auto *parentSplitter = findParentSplitter(p_split);
  if (!parentSplitter) {
    m_contentsLayout->removeWidget(p_split);
    p_split->setParent(nullptr);
    m_topWidget = nullptr;
    return;
  }
  p_split->setParent(nullptr);
  if (parentSplitter->count() == 1) {
    unwrapSingleChildSplitter(parentSplitter);
  }
}

void ViewArea2::maximizeViewSplitWidget(ViewSplit2 *p_split) {
  QWidget *target = p_split;
  auto *splitter = findParentSplitter(target);
  while (splitter) {
    const auto currentSizes = splitter->sizes();
    QList<int> sizes;
    int totalSize = 0;
    for (int i = 0; i < splitter->count(); ++i) {
      totalSize += currentSizes.value(i);
    }
    int targetIdx = splitter->indexOf(target);
    for (int i = 0; i < splitter->count(); ++i) {
      sizes.append(i == targetIdx ? totalSize : 0);
    }
    splitter->setSizes(sizes);
    target = splitter;
    splitter = findParentSplitter(target);
  }
}

void ViewArea2::distributeViewSplitWidgets() {
  auto *splitter = qobject_cast<QSplitter *>(m_topWidget);
  if (splitter) {
    distributeSplitter(splitter);
  }
}

ViewSplit2 *ViewArea2::findSplitByDirection(ViewSplit2 *p_split, Direction p_direction) const {
  Qt::Orientation orient = (p_direction == Direction::Left || p_direction == Direction::Right)
                               ? Qt::Horizontal
                               : Qt::Vertical;
  int step = (p_direction == Direction::Right || p_direction == Direction::Down) ? 1 : -1;

  QWidget *target = p_split;
  auto *splitter = findParentSplitter(target);
  while (splitter) {
    if (splitter->orientation() == orient) {
      int idx = splitter->indexOf(target);
      int newIdx = idx + step;
      if (newIdx >= 0 && newIdx < splitter->count()) {
        QWidget *adjacent = splitter->widget(newIdx);
        return (step > 0) ? findFirstViewSplit(adjacent) : findLastViewSplit(adjacent);
      }
    }
    target = splitter;
    splitter = findParentSplitter(target);
  }
  return nullptr;
}

// ============ State ============

QVector<ViewSplit2 *> ViewArea2::getAllViewSplits() const {
  QVector<ViewSplit2 *> splits;
  if (m_topWidget) {
    collectViewSplits(m_topWidget, splits);
  }
  return splits;
}

ViewSplit2 *ViewArea2::getCurrentViewSplit() const {
  for (auto *split : getAllViewSplits()) {
    if (split->isActive()) {
      return split;
    }
  }
  return nullptr;
}

int ViewArea2::getViewSplitCount() const { return getAllViewSplits().size(); }

QVector<void *> ViewArea2::getVisibleNavigationItems() {
  QVector<void *> items;
  m_navigationItems.clear();

  int idx = 0;
  for (auto *split : getAllViewSplits()) {
    if (split->getViewWindowCount() == 0) {
      continue;
    }
    if (idx >= NavigationMode::c_maxNumOfNavigationItems) {
      break;
    }
    auto info = split->getNavigationModeInfo();
    for (int i = 0; i < info.size() && idx < NavigationMode::c_maxNumOfNavigationItems;
         ++i, ++idx) {
      items.push_back(info[i].m_viewWindow);
      m_navigationItems.push_back(info[i]);
    }
  }
  return items;
}

void ViewArea2::placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) {
  Q_UNUSED(p_item);
  Q_ASSERT(p_idx > -1);
  p_label->setParent(static_cast<QWidget *>(m_navigationItems[p_idx].m_viewWindow)->parentWidget());
  p_label->move(m_navigationItems[p_idx].m_topLeft);
}

void ViewArea2::handleTargetHit(void *p_item) {
  if (p_item) {
    auto *win = static_cast<ViewWindow2 *>(p_item);
    for (auto *split : getAllViewSplits()) {
      if (split->indexOf(win) != -1) {
        split->setCurrentViewWindow(win);
        split->focus();
        break;
      }
    }
  }
}

void ViewArea2::clearNavigation() {
  NavigationMode::clearNavigation();
  m_navigationItems.clear();
}

// ============ Top Widget ============

QWidget *ViewArea2::getTopWidget() const { return m_topWidget; }

void ViewArea2::setTopWidget(QWidget *p_widget) {
  if (m_topWidget) {
    m_contentsLayout->removeWidget(m_topWidget);
  }
  m_topWidget = p_widget;
  if (m_topWidget) {
    m_contentsLayout->addWidget(m_topWidget);
  }
}

QJsonObject ViewArea2::saveLayout() const {
  // If no buffers are open, save an empty layout so that on next startup
  // loadLayout() sees an empty splitterTree and shows the home screen.
  // This avoids persisting workspace IDs that saveSession() will later delete.
  if (m_windows.isEmpty()) {
    qInfo() << "ViewArea2::saveLayout: no open buffers, saving empty layout";
    return QJsonObject();
  }

  QJsonObject widgetTree;
  if (m_topWidget) {
    widgetTree = serializeWidget(m_topWidget);
  }
  return m_controller->saveLayout(widgetTree);
}

void ViewArea2::loadLayoutFromSession(const QJsonObject &p_layout) {
  if (!p_layout.isEmpty()) {
    m_controller->loadLayout(p_layout);
  }
}

void ViewArea2::saveSession() {
  qInfo() << "ViewArea2::saveSession: syncing buffer order and persisting session";

  auto *wsSvc = m_services.get<WorkspaceCoreService>();
  if (wsSvc) {
    // Sync tab order and current buffer from each visible ViewSplit2 to vxcore.
    // Note: hidden workspaces' state is already correct in vxcore —
    // synced when takeViewWindowsFromSplit was called during workspace switch.
    for (auto it = m_splits.constBegin(); it != m_splits.constEnd(); ++it) {
      QStringList bufferIds;
      auto windows = it.value()->getAllViewWindows();
      for (auto *win : windows) {
        QString bufferId = win->getBuffer().id();
        bufferIds.append(bufferId);

        // Persist per-buffer metadata (mode, cursor, scroll position).
        QJsonObject bufMeta;
        ViewWindowMode mode = win->getMode();
        bufMeta[QStringLiteral("mode")] =
            (mode == ViewWindowMode::Edit) ? QStringLiteral("Edit") : QStringLiteral("Read");
        bufMeta[QStringLiteral("cursorPosition")] = win->getCursorPosition();
        bufMeta[QStringLiteral("scrollPosition")] = win->getScrollPosition();
        wsSvc->setBufferMetadata(it.key(), bufferId, bufMeta);
      }
      wsSvc->setBufferOrder(it.key(), bufferIds);

      auto *currentWin = it.value()->getCurrentViewWindow();
      if (currentWin) {
        wsSvc->setCurrentBuffer(it.key(), currentWin->getBuffer().id());
      }

      qDebug() << "  Synced workspace" << it.key() << "buffer order:" << bufferIds
               << "currentBuffer:"
               << (currentWin ? currentWin->getBuffer().id() : QStringLiteral("(none)"));
    }

    // Remove empty workspaces (no buffers) from vxcore.
    QJsonArray allWorkspaces = wsSvc->listWorkspaces();
    for (int i = 0; i < allWorkspaces.size(); ++i) {
      QJsonObject wsObj = allWorkspaces[i].toObject();
      QString wsId = wsObj.value(QStringLiteral("id")).toString();
      QJsonArray bufferIds = wsObj.value(QStringLiteral("bufferIds")).toArray();
      if (bufferIds.isEmpty()) {
        qDebug() << "  Removing empty workspace:" << wsId;
        wsSvc->deleteWorkspace(wsId);
      }
    }
  }

  // Disable propagation so buffer closes during teardown don't touch vxcore.
  m_controller->setShouldPropagateToCore(false);

  qInfo() << "ViewArea2::saveSession: done";
}

void ViewArea2::restoreSession() {
  QStringList layoutWorkspaceIds = m_splits.keys();
  m_controller->restoreSession(layoutWorkspaceIds);
}

// ============ ID resolution helpers ============

ViewSplit2 *ViewArea2::splitForWorkspace(const QString &p_workspaceId) const {
  return m_splits.value(p_workspaceId, nullptr);
}

ViewWindow2 *ViewArea2::windowForId(ID p_windowId) const {
  return m_windows.value(p_windowId, nullptr);
}

// ============ wireSplitSignals ============

void ViewArea2::wireSplitSignals(ViewSplit2 *p_split) {
  const QString wsId = p_split->getWorkspaceId();

  // Signals forwarded to controller (ID-based).
  connect(p_split, &ViewSplit2::focused, this,
          [this, wsId](ViewSplit2 *) { m_controller->setCurrentViewSplit(wsId, false); });
  connect(p_split, &ViewSplit2::viewWindowCloseRequested, this,
          [this](ViewWindow2 *w) { m_controller->closeViewWindow(w->getViewWindowId(), false); });
  connect(p_split, &ViewSplit2::currentViewWindowChanged, this, [this](ViewWindow2 *w) {
    QString bufferId;
    if (w) {
      bufferId = w->getBuffer().id();
    }
    qDebug() << "ViewArea2: split currentViewWindowChanged, win:" << w << "bufferId:" << bufferId;
    m_controller->setCurrentViewWindow(
        w ? w->getViewWindowId() : ViewAreaController::InvalidViewWindowId, bufferId);
  });
  connect(p_split, &ViewSplit2::splitRequested, this,
          [this, wsId](ViewSplit2 *, Direction d) { m_controller->splitViewSplit(wsId, d); });
  connect(p_split, &ViewSplit2::maximizeSplitRequested, this,
          [this, wsId](ViewSplit2 *) { m_controller->maximizeViewSplit(wsId); });
  connect(p_split, &ViewSplit2::distributeSplitsRequested, this,
          [this]() { m_controller->distributeViewSplits(); });

  // Signals handled by ViewArea2 (need pointer context).
  connect(p_split, &ViewSplit2::removeSplitRequested, this, &ViewArea2::onRemoveSplitRequested);
  connect(p_split, &ViewSplit2::removeSplitAndWorkspaceRequested, this,
          &ViewArea2::onRemoveSplitAndWorkspaceRequested);
  connect(p_split, &ViewSplit2::moveViewWindowOneSplitRequested, this,
          &ViewArea2::onMoveViewWindowOneSplitRequested);

  // Workspace signals.
  connect(p_split, &ViewSplit2::newWorkspaceRequested, this, [this](ViewSplit2 *s) {
    QString defaultName = m_controller->generateWorkspaceName();
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("New Workspace"), tr("Workspace name:"),
                                         QLineEdit::Normal, defaultName, &ok);
    if (!ok) {
      return;
    }
    name = name.trimmed();
    if (name.isEmpty()) {
      name = defaultName;
    }
    m_controller->newWorkspace(s->getWorkspaceId(), name);
  });
  connect(p_split, &ViewSplit2::removeWorkspaceRequested, this, [this](ViewSplit2 *s) {
    // Copy the workspace ID since the split's ID may change during removeWorkspace
    // (switchWorkspace modifies the split's workspace identity).
    QString wsId = s->getWorkspaceId();
    m_controller->removeWorkspace(wsId, false);
  });
  connect(p_split, &ViewSplit2::renameWorkspaceRequested, this, [this](ViewSplit2 *s) {
    QString wsId = s->getWorkspaceId();
    QString currentName = m_controller->getWorkspaceName(wsId);
    bool ok = false;
    QString newName = QInputDialog::getText(this, tr("Rename Workspace"), tr("Workspace name:"),
                                            QLineEdit::Normal, currentName, &ok);
    if (!ok) {
      return;
    }
    newName = newName.trimmed();
    if (newName.isEmpty() || newName == currentName) {
      return;
    }
    m_controller->renameWorkspace(wsId, newName);
  });
  connect(p_split, &ViewSplit2::switchWorkspaceRequested, this,
          [this](ViewSplit2 *s, const QString &wsId) {
            m_controller->switchWorkspace(s->getWorkspaceId(), wsId);
          });
  connect(p_split, &ViewSplit2::bufferOrderChanged, this,
          [this](ViewSplit2 *s, const QStringList &ids) {
            m_controller->setBufferOrder(s->getWorkspaceId(), ids);
          });

  // Tab context menu: close multiple tabs.
  connect(p_split, &ViewSplit2::closeTabsRequested, this,
          [this](ViewSplit2 *s, int p_tabIndex, CloseTabMode p_mode) {
            m_controller->closeTabs(s->getWorkspaceId(), p_tabIndex, p_mode);
          });

  // Tab context menu: locate node in notebook explorer.
  connect(p_split, &ViewSplit2::locateNodeRequested, m_controller,
          &ViewAreaController::locateNodeRequested);
}

// ============ ViewAreaView Interface Implementations ============

void ViewArea2::addFirstViewSplit(const QString &p_workspaceId) {
  auto *split = addFirstViewSplitWidget(p_workspaceId);
  wireSplitSignals(split);
  m_controller->setCurrentViewSplit(p_workspaceId, true);
  updateScreenVisibility();
}

void ViewArea2::split(const QString &p_workspaceId, Direction p_direction,
                      const QString &p_newWorkspaceId) {
  auto *existingSplit = splitForWorkspace(p_workspaceId);
  if (!existingSplit) {
    qWarning() << "ViewArea2::split: workspace not found:" << p_workspaceId;
    return;
  }
  auto *newSplit = splitAt(existingSplit, p_direction, p_newWorkspaceId);
  wireSplitSignals(newSplit);
  m_controller->setCurrentViewSplit(p_newWorkspaceId, true);
}

void ViewArea2::removeViewSplit(const QString &p_workspaceId) {
  auto *split = splitForWorkspace(p_workspaceId);
  if (!split) {
    return;
  }

  // Remove all view windows from m_windows before deleting the split
  // (Qt parent-child ownership will delete the ViewWindow2s).
  auto windows = split->getAllViewWindows();
  for (auto *win : windows) {
    ID winId = win->getViewWindowId();
    if (winId != ViewAreaController::InvalidViewWindowId) {
      m_windows.remove(winId);
    }
  }

  m_splits.remove(p_workspaceId);
  removeViewSplitWidget(split);
  // Use deleteLater instead of delete because this may be called from within
  // a signal handler chain originating from this split (e.g., tab close →
  // onViewWindowClosed → auto-remove → removeViewSplit). Deleting the split
  // synchronously would cause a use-after-free when Qt returns to the caller.
  split->deleteLater();
  updateScreenVisibility();
}

void ViewArea2::maximizeViewSplit(const QString &p_workspaceId) {
  auto *split = splitForWorkspace(p_workspaceId);
  if (split) {
    maximizeViewSplitWidget(split);
  }
}

void ViewArea2::distributeViewSplits() { distributeViewSplitWidgets(); }

void ViewArea2::openBuffer(const Buffer2 &p_buffer, const QString &p_fileType,
                           const QString &p_workspaceId, const FileOpenSettings &p_settings) {
  auto *split = splitForWorkspace(p_workspaceId);
  if (!split) {
    qWarning() << "ViewArea2::openBuffer: workspace not found:" << p_workspaceId;
    return;
  }
  auto *factory = m_services.get<ViewWindowFactory>();
  if (!factory || !factory->hasCreator(p_fileType)) {
    qWarning() << "ViewArea2: no creator for file type" << p_fileType;
    return;
  }
  auto *win = factory->create(p_fileType, m_services, p_buffer, split, p_settings.m_mode);
  if (!win) {
    qWarning() << "ViewArea2: failed creating view window for type" << p_fileType;
    return;
  }
  ID id = m_nextWindowId++;
  m_windows[id] = win;
  win->setViewWindowId(id);
  split->addViewWindow(win);
  m_controller->onViewWindowOpened(id, p_buffer, p_settings);

  // QTabWidget::addTab does not make the new tab current (except the first).
  // When focus is requested (e.g., restoring the current buffer), explicitly
  // select the new tab.
  if (p_settings.m_focus) {
    split->setCurrentViewWindow(win);
  }

  // Apply file open settings (scroll to line, search highlight).
  win->applyFileOpenSettings(p_settings);

  updateScreenVisibility();
}

void ViewArea2::openWidgetContent(IViewWindowContent *p_content, const Buffer2 &p_buffer,
                                  const QString &p_workspaceId) {
  auto *split = splitForWorkspace(p_workspaceId);
  if (!split) {
    qWarning() << "ViewArea2::openWidgetContent: workspace not found:" << p_workspaceId;
    delete p_content;
    return;
  }

  auto *win = new WidgetViewWindow2(m_services, p_buffer, p_content, split);
  ID id = m_nextWindowId++;
  m_windows[id] = win;
  win->setViewWindowId(id);
  split->addViewWindow(win);

  // Use default FileOpenSettings for notification.
  FileOpenSettings settings;
  settings.m_focus = true;
  m_controller->onViewWindowOpened(id, p_buffer, settings);
  split->setCurrentViewWindow(win);

  updateScreenVisibility();
}

void ViewArea2::applyFileOpenSettings(ID p_windowId, const FileOpenSettings &p_settings) {
  auto *win = windowForId(p_windowId);
  if (win) {
    win->applyFileOpenSettings(p_settings);
  }
}

bool ViewArea2::closeViewWindow(ID p_windowId, bool p_force) {
  auto *win = windowForId(p_windowId);
  if (!win) {
    return false;
  }

  if (!win->aboutToClose(p_force)) {
    return false;
  }

  // Find the owning split.
  ViewSplit2 *ownerSplit = nullptr;
  QString bufferId;
  QString workspaceId;
  for (auto it = m_splits.begin(); it != m_splits.end(); ++it) {
    if (it.value()->indexOf(win) != -1) {
      ownerSplit = it.value();
      workspaceId = it.key();
      break;
    }
  }
  if (ownerSplit) {
    Buffer2 buf = win->getBuffer();
    bufferId = buf.id();
    ownerSplit->takeViewWindow(win);
  }

  m_windows.remove(p_windowId);
  delete win;

  m_controller->onViewWindowClosed(p_windowId, bufferId, workspaceId);
  updateScreenVisibility();
  return true;
}

void ViewArea2::setCurrentViewSplit(const QString &p_workspaceId, bool p_focus) {
  for (auto *split : getAllViewSplits()) {
    split->setActive(false);
  }
  auto *split = splitForWorkspace(p_workspaceId);
  if (split) {
    split->setActive(true);
    if (p_focus) {
      split->focus();
    }
  }
}

void ViewArea2::focusViewSplit(const QString &p_workspaceId) {
  auto *split = splitForWorkspace(p_workspaceId);
  if (split) {
    split->focus();
  }
}

void ViewArea2::moveViewWindowToSplit(ID p_windowId, const QString &p_srcWorkspaceId,
                                      const QString &p_dstWorkspaceId) {
  auto *win = windowForId(p_windowId);
  auto *srcSplit = splitForWorkspace(p_srcWorkspaceId);
  auto *dstSplit = splitForWorkspace(p_dstWorkspaceId);
  if (!win || !srcSplit || !dstSplit) {
    return;
  }

  srcSplit->takeViewWindow(win);
  dstSplit->addViewWindow(win);
  dstSplit->setCurrentViewWindow(win);
}

// ============ ViewSplit2 signal handlers ============

void ViewArea2::onMoveViewWindowOneSplitRequested(ViewSplit2 *p_split, ViewWindow2 *p_win,
                                                  Direction p_direction) {
  auto *target = findSplitByDirection(p_split, p_direction);
  if (!target) {
    m_controller->splitViewSplit(p_split->getWorkspaceId(), p_direction);
    target = findSplitByDirection(p_split, p_direction);
    if (!target) {
      return;
    }
  }
  m_controller->moveViewWindowOneSplit(p_split->getWorkspaceId(), p_win->getViewWindowId(),
                                       p_direction, target->getWorkspaceId(),
                                       p_win->getBuffer().id());
}

void ViewArea2::onRemoveSplitRequested(ViewSplit2 *p_split) {
  // Hide-only mode: workspace becomes hidden, split is removed.
  m_controller->removeViewSplit(p_split->getWorkspaceId(), true, false);
}

void ViewArea2::onRemoveSplitAndWorkspaceRequested(ViewSplit2 *p_split) {
  // Full removal mode: workspace is closed (with abort-on-cancel), then split is removed.
  m_controller->removeViewSplit(p_split->getWorkspaceId(), false, false);
}

// ============ Layout serialization/deserialization ============

void ViewArea2::loadLayout(const QJsonObject &p_layout) {
  const QJsonObject tree = p_layout.value(QStringLiteral("splitterTree")).toObject();
  if (tree.isEmpty()) {
    return;
  }

  const QString type = tree.value(QStringLiteral("type")).toString();
  if (type == QStringLiteral("workspace")) {
    QString wsId = tree.value(QStringLiteral("workspaceId")).toString();
    auto *split = addFirstViewSplitWidget(wsId);
    wireSplitSignals(split);
  } else if (type == QStringLiteral("splitter")) {
    Qt::Orientation orient =
        (tree.value(QStringLiteral("orientation")).toString() == QStringLiteral("horizontal"))
            ? Qt::Horizontal
            : Qt::Vertical;
    auto *splitter = createSplitter(orient);
    deserializeSplitterNode(tree, splitter);
    setTopWidget(splitter);
  }

  // Note: currentWorkspaceId is NOT restored here. It is restored by
  // ViewAreaController::restoreSession() from vxcore state.
  updateScreenVisibility();
}

void ViewArea2::switchWorkspace(const QString &p_currentWorkspaceId,
                                const QString &p_newWorkspaceId) {
  // This method is now only used during session restore for stale workspace
  // replacement. Normal workspace switching is orchestrated by the controller
  // via the 3 reparenting primitives (takeViewWindowsFromSplit,
  // updateSplitWorkspaceId, placeViewWindowsInSplit).
  //
  // For restore, the split has no windows to reparent — just update identity.
  auto *split = splitForWorkspace(p_currentWorkspaceId);
  if (!split) {
    qWarning() << "ViewArea2::switchWorkspace: workspace not found:" << p_currentWorkspaceId;
    return;
  }

  // Remove any existing windows (shouldn't have any during restore, but be safe).
  auto windows = split->getAllViewWindows();
  for (auto *win : windows) {
    split->takeViewWindow(win);
    ID winId = win->getViewWindowId();
    if (winId != ViewAreaController::InvalidViewWindowId) {
      m_windows.remove(winId);
    }
    delete win;
  }

  // Update split identity.
  m_splits.remove(p_currentWorkspaceId);
  m_splits.insert(p_newWorkspaceId, split);
  split->setWorkspaceId(p_newWorkspaceId);

  disconnect(split, nullptr, this, nullptr);
  wireSplitSignals(split);

  updateScreenVisibility();
}

QVector<QObject *> ViewArea2::takeViewWindowsFromSplit(const QString &p_workspaceId,
                                                       int *p_outCurrentIndex) {
  auto *split = splitForWorkspace(p_workspaceId);
  if (!split) {
    return {};
  }

  qDebug() << "ViewArea2::takeViewWindowsFromSplit: workspace:" << p_workspaceId
           << "windowCount:" << split->getViewWindowCount()
           << "propagateToCore:" << m_controller->shouldPropagateToCore();

  // Sync tab order and current buffer to vxcore before hiding.
  if (m_controller->shouldPropagateToCore() && split->getViewWindowCount() > 0) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc) {
      QStringList bufferIds;
      for (auto *win : split->getAllViewWindows()) {
        bufferIds.append(win->getBuffer().id());
      }
      qDebug() << "  Syncing buffer order before hide:" << bufferIds;
      wsSvc->setBufferOrder(p_workspaceId, bufferIds);

      // Also sync the current buffer so it's correct if the workspace
      // is restored from vxcore state (fresh workspace without cached windows).
      auto *currentWin = split->getCurrentViewWindow();
      if (currentWin) {
        QString curBufId = currentWin->getBuffer().id();
        qDebug() << "  Syncing current buffer before hide:" << curBufId;
        wsSvc->setCurrentBuffer(p_workspaceId, curBufId);
      }
    }
  }

  if (p_outCurrentIndex) {
    *p_outCurrentIndex = split->currentIndex();
  }

  QVector<QObject *> result;
  auto viewWindows = split->getAllViewWindows();
  for (auto *win : viewWindows) {
    qDebug() << "  Taking window for buffer:" << win->getBuffer().id();
    split->takeViewWindow(win); // removeTab + disconnect + setVisible(false) + setParent(nullptr)
    result.append(win);
  }
  qDebug() << "  Took" << result.size() << "windows from split";
  // NOTE: m_windows map entries are KEPT — the ViewWindow2 still exists, just hidden.
  return result;
}

void ViewArea2::placeViewWindowsInSplit(const QString &p_workspaceId,
                                        const QVector<QObject *> &p_windows, int p_currentIndex) {
  auto *split = splitForWorkspace(p_workspaceId);
  if (!split) {
    return;
  }

  for (auto *obj : p_windows) {
    auto *win = qobject_cast<ViewWindow2 *>(obj);
    if (win) {
      split->addViewWindow(win); // addTab + connect signals + setVisible(true)
    }
  }

  if (p_currentIndex >= 0 && p_currentIndex < p_windows.size()) {
    split->setCurrentViewWindow(p_currentIndex);
  }

  updateScreenVisibility();
}

void ViewArea2::updateSplitWorkspaceId(const QString &p_oldWorkspaceId,
                                       const QString &p_newWorkspaceId) {
  auto *split = splitForWorkspace(p_oldWorkspaceId);
  if (!split) {
    return;
  }

  m_splits.remove(p_oldWorkspaceId);
  m_splits.insert(p_newWorkspaceId, split);
  split->setWorkspaceId(p_newWorkspaceId);

  // IMPORTANT: Must disconnect+reconnect because wireSplitSignals captures
  // the workspace ID by value in several lambdas (focused, splitRequested,
  // maximizeSplitRequested). Without rewiring, those lambdas would carry
  // the old workspace ID.
  disconnect(split, nullptr, this, nullptr);
  wireSplitSignals(split);
}

void ViewArea2::setCurrentBuffer(const QString &p_workspaceId, const QString &p_bufferId,
                                 bool p_focus) {
  auto *split = splitForWorkspace(p_workspaceId);
  if (!split) {
    qWarning() << "ViewArea2::setCurrentBuffer: no split for workspace:" << p_workspaceId;
    return;
  }

  qDebug() << "ViewArea2::setCurrentBuffer: workspace:" << p_workspaceId << "buffer:" << p_bufferId
           << "focus:" << p_focus << "currentWidget:" << split->currentWidget();

  // Find the ViewWindow2 with the matching buffer ID.
  auto windows = split->getAllViewWindows();
  for (auto *win : windows) {
    if (win->getBuffer().id() == p_bufferId) {
      qDebug() << "ViewArea2::setCurrentBuffer: found win:" << win
               << "calling setCurrentViewWindow";
      split->setCurrentViewWindow(win);
      qDebug() << "ViewArea2::setCurrentBuffer: after setCurrentViewWindow, currentWidget:"
               << split->currentWidget();
      if (p_focus) {
        split->focus();
      }
      return;
    }
  }

  qWarning() << "ViewArea2::setCurrentBuffer: buffer not found:" << p_bufferId
             << "in workspace:" << p_workspaceId;
}

QStringList ViewArea2::getVisibleWorkspaceIds() const { return m_splits.keys(); }

QVector<ID> ViewArea2::getViewWindowIdsForWorkspace(const QString &p_workspaceId) const {
  QVector<ID> ids;
  auto *split = splitForWorkspace(p_workspaceId);
  if (!split) {
    return ids;
  }

  auto windows = split->getAllViewWindows();
  for (auto *win : windows) {
    ID winId = win->getViewWindowId();
    if (winId != ViewAreaController::InvalidViewWindowId) {
      ids.append(winId);
    }
  }
  return ids;
}

ID ViewArea2::findWindowIdByBufferId(const QString &p_workspaceId,
                                     const QString &p_bufferId) const {
  auto *split = splitForWorkspace(p_workspaceId);
  if (!split) {
    return ViewAreaController::InvalidViewWindowId;
  }

  auto windows = split->getAllViewWindows();
  for (auto *win : windows) {
    if (win->getBuffer().id() == p_bufferId) {
      return win->getViewWindowId();
    }
  }
  return ViewAreaController::InvalidViewWindowId;
}

QString ViewArea2::getCurrentBufferIdForWorkspace(const QString &p_workspaceId) const {
  auto *split = splitForWorkspace(p_workspaceId);
  if (!split) {
    return QString();
  }

  auto *win = split->getCurrentViewWindow();
  if (!win) {
    return QString();
  }

  return win->getBuffer().id();
}

void ViewArea2::onNodeRenamed(const NodeIdentifier &p_oldNodeId,
                              const NodeIdentifier &p_newNodeId) {
  for (auto it = m_windows.constBegin(); it != m_windows.constEnd(); ++it) {
    ViewWindow2 *win = it.value();
    if (win->getNodeId() == p_oldNodeId) {
      win->onNodeRenamed(p_newNodeId);
    }
  }
}

void ViewArea2::notifyEditorConfigChanged() {
  for (auto it = m_windows.constBegin(); it != m_windows.constEnd(); ++it) {
    it.value()->handleEditorConfigChange();
  }
}

QJsonObject ViewArea2::serializeWidget(const QWidget *p_widget) {
  QJsonObject node;
  auto *splitter = qobject_cast<const QSplitter *>(p_widget);
  if (splitter) {
    node[QStringLiteral("type")] = QStringLiteral("splitter");
    node[QStringLiteral("orientation")] = (splitter->orientation() == Qt::Horizontal)
                                              ? QStringLiteral("horizontal")
                                              : QStringLiteral("vertical");
    QJsonArray sizes;
    for (int s : splitter->sizes()) {
      sizes.append(s);
    }
    node[QStringLiteral("sizes")] = sizes;
    QJsonArray children;
    for (int i = 0; i < splitter->count(); ++i) {
      children.append(serializeWidget(splitter->widget(i)));
    }
    node[QStringLiteral("children")] = children;
  } else {
    auto *split = qobject_cast<const ViewSplit2 *>(p_widget);
    if (split) {
      node[QStringLiteral("type")] = QStringLiteral("workspace");
      node[QStringLiteral("workspaceId")] = split->getWorkspaceId();
    }
  }
  return node;
}

void ViewArea2::deserializeSplitterNode(const QJsonObject &p_node, QSplitter *p_splitter) {
  const QJsonArray children = p_node.value(QStringLiteral("children")).toArray();
  for (const auto &childVal : children) {
    const QJsonObject child = childVal.toObject();
    const QString type = child.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("workspace")) {
      const QString wsId = child.value(QStringLiteral("workspaceId")).toString();
      auto *split = createViewSplit(wsId);
      m_splits[wsId] = split;
      wireSplitSignals(split);
      p_splitter->addWidget(split);
    } else if (type == QStringLiteral("splitter")) {
      Qt::Orientation orient =
          (child.value(QStringLiteral("orientation")).toString() == QStringLiteral("horizontal"))
              ? Qt::Horizontal
              : Qt::Vertical;
      auto *childSplitter = createSplitter(orient);
      p_splitter->addWidget(childSplitter);
      deserializeSplitterNode(child, childSplitter);
    }
  }
  const QJsonArray sizesArr = p_node.value(QStringLiteral("sizes")).toArray();
  if (!sizesArr.isEmpty()) {
    QList<int> sizes;
    for (const auto &s : sizesArr) {
      sizes.append(s.toInt());
    }
    p_splitter->setSizes(sizes);
  }
}

// ============ Private helpers ============

void ViewArea2::collectViewSplits(QWidget *p_widget, QVector<ViewSplit2 *> &p_splits) const {
  auto *split = qobject_cast<ViewSplit2 *>(p_widget);
  if (split) {
    p_splits.append(split);
    return;
  }
  auto *splitter = qobject_cast<QSplitter *>(p_widget);
  if (splitter) {
    for (int i = 0; i < splitter->count(); ++i) {
      collectViewSplits(splitter->widget(i), p_splits);
    }
  }
}

void ViewArea2::distributeSplitter(QSplitter *p_splitter) {
  int cnt = p_splitter->count();
  if (cnt == 0) {
    return;
  }
  int total =
      (p_splitter->orientation() == Qt::Horizontal) ? p_splitter->width() : p_splitter->height();
  int each = total / cnt;
  QList<int> sizes;
  for (int i = 0; i < cnt; ++i) {
    sizes.append(each);
  }
  p_splitter->setSizes(sizes);
  for (int i = 0; i < cnt; ++i) {
    auto *child = qobject_cast<QSplitter *>(p_splitter->widget(i));
    if (child) {
      distributeSplitter(child);
    }
  }
}

QSplitter *ViewArea2::findParentSplitter(QWidget *p_widget) const {
  if (!p_widget) {
    return nullptr;
  }
  QWidget *parent = p_widget->parentWidget();
  while (parent && parent != this) {
    auto *splitter = qobject_cast<QSplitter *>(parent);
    if (splitter) {
      return splitter;
    }
    parent = parent->parentWidget();
  }
  return nullptr;
}

void ViewArea2::unwrapSingleChildSplitter(QSplitter *p_splitter) {
  Q_ASSERT(p_splitter->count() == 1);
  QWidget *child = p_splitter->widget(0);
  auto *grandParent = findParentSplitter(p_splitter);
  if (!grandParent) {
    m_contentsLayout->removeWidget(p_splitter);
    child->setParent(nullptr);
    p_splitter->setParent(nullptr);
    setTopWidget(child);
  } else {
    int idx = grandParent->indexOf(p_splitter);
    child->setParent(nullptr);
    p_splitter->setParent(nullptr);
    grandParent->insertWidget(idx, child);
  }
  p_splitter->deleteLater();
}

ViewSplit2 *ViewArea2::findFirstViewSplit(QWidget *p_widget) {
  auto *split = qobject_cast<ViewSplit2 *>(p_widget);
  if (split) {
    return split;
  }
  auto *splitter = qobject_cast<QSplitter *>(p_widget);
  if (splitter && splitter->count() > 0) {
    return findFirstViewSplit(splitter->widget(0));
  }
  return nullptr;
}

ViewSplit2 *ViewArea2::findLastViewSplit(QWidget *p_widget) {
  auto *split = qobject_cast<ViewSplit2 *>(p_widget);
  if (split) {
    return split;
  }
  auto *splitter = qobject_cast<QSplitter *>(p_widget);
  if (splitter && splitter->count() > 0) {
    return findLastViewSplit(splitter->widget(splitter->count() - 1));
  }
  return nullptr;
}

// ============ Screen switching ============

void ViewArea2::showHomeScreen() { m_stackedLayout->setCurrentIndex(HomeScreen); }

void ViewArea2::showContentsScreen() { m_stackedLayout->setCurrentIndex(ContentsScreen); }

void ViewArea2::updateScreenVisibility() {
  const int targetPage = m_windows.isEmpty() ? HomeScreen : ContentsScreen;
  if (m_stackedLayout->currentIndex() != targetPage) {
    m_stackedLayout->setCurrentIndex(targetPage);
  }
}
