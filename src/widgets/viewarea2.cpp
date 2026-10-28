#include "viewarea2.h"

#include <QApplication>
#include <QCoreApplication>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScopeGuard>
#include <QSet>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <vxcore/notebook_json_keys.h>

#include <controllers/viewareacontroller.h>
#include <core/configmgr2.h>
#include <core/coreconfig.h>
#include <core/hookcontext.h>
#include <core/hooknames.h>
#include <core/iviewwindowcontent.h>
#include <core/logging.h>
#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/workspacecoreservice.h>
#include <gui/services/themeservice.h>
#include <gui/services/viewwindowfactory.h>
#include <gui/utils/widgetutils.h>

#include "detachedwindow.h"
#include "linkpreviewoverlay.h"
#include "viewsplit2.h"
#include "viewwindow2.h"
#include "widgetviewwindow2.h"

using namespace vnotex;

namespace {
// Session tabs retain visible metadata only until explicit authentication.
class LockedNoteView final : public ViewWindow2 {
  Q_DECLARE_TR_FUNCTIONS(LockedNoteView)
public:
  LockedNoteView(ServiceLocator &p_services, const Buffer2 &p_buffer,
                 const FileOpenSettings &p_settings, QWidget *p_parent)
      : ViewWindow2(p_services, p_buffer, p_parent), m_settings(p_settings) {
    m_mode = p_settings.m_mode;
    auto *body = new QWidget(this);
    auto *layout = new QVBoxLayout(body);
    layout->addStretch();
    auto *label = new QLabel(tr("This note is locked. Unlock it to read its contents."), body);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignCenter);
    layout->addWidget(label);
    auto *unlock = new QPushButton(tr("Unlock Note"), body);
    unlock->setObjectName(QStringLiteral("unlockProtectedNote"));
    layout->addWidget(unlock, 0, Qt::AlignHCenter);
    layout->addStretch();
    connect(unlock, &QPushButton::clicked, this, [this]() {
      auto settings = m_settings;
      settings.m_focus = true;
      getServices().get<BufferService>()->openBuffer(getNodeId(), settings);
    });
    setCentralWidget(body);
  }
  QString getLatestContent() const override { return QString(); }
  QIcon getIcon() const override { return QIcon(QStringLiteral(":/core/icons/lock.svg")); }
  bool isSaveSupported() const override { return false; }
  void setMode(ViewWindowMode p_mode) override { m_mode = m_settings.m_mode = p_mode; }

protected:
  // No text document, undo stack, writer or preview exists in a locked tab.
  void syncEditorFromBuffer() override {}
  void setModified(bool) override {}
  void scrollUp() override {}
  void scrollDown() override {}
  void zoom(bool) override {}
  bool isPrintSupported() const override { return false; }

private:
  FileOpenSettings m_settings;
};
} // namespace

ViewArea2::ViewArea2(ServiceLocator &p_services, QWidget *p_parent)
    : QWidget(p_parent),
      NavigationMode(NavigationMode::Type::DoubleKeys, this, p_services.get<ThemeService>()),
      m_services(p_services) {
  setContentsMargins(0, 0, 0, 0);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

  // The home dashboard (vx://home) is a real virtual-buffer tab, so the view
  // area no longer needs a separate placeholder "home" page. A single contents
  // surface holds the splitter tree; maybeOpenHome() opens the dashboard tab
  // whenever the area becomes empty.
  auto *outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(0, 0, 0, 0);

  m_contentsWidget = new QWidget(this);
  m_contentsWidget->setContentsMargins(0, 0, 0, 0);
  m_contentsLayout = new QVBoxLayout(m_contentsWidget);
  m_contentsLayout->setContentsMargins(0, 0, 0, 0);
  outerLayout->addWidget(m_contentsWidget);

  setupController();
  setupShortcuts();
}

ViewArea2::~ViewArea2() {
  clearNavigation();
  // Normal close/quit already passed cancellable gates. This is resource teardown only.
  for (const auto &host : m_previewHosts) {
    if (host.source) {
      host.source->m_closeLinkPreview = {};
    }
    if (host.overlay) {
      disconnect(host.overlay, nullptr, this, nullptr);
    }
  }
  m_previewHosts.clear();
  // Detached windows are parent-less top-level widgets; delete them explicitly so
  // they don't leak (or keep the app alive) if the view area is torn down while
  // any remain open.
  const auto detached = m_detachedWindows;
  m_detachedWindows.clear();
  for (auto *dw : detached) {
    if (dw) {
      delete dw;
    }
  }
}

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
            qCDebug(lcWorkspace) << "ViewArea2: propagation to core re-enabled";
            // Re-sync current identity from the selected UI tab after restore.
            // Core reconciliation also runs when deferred selection signals
            // have already established the same controller window ID.
            ViewWindow2 *currentWin = nullptr;
            auto *currentSplit = getCurrentViewSplit();
            if (!currentSplit && !m_splits.isEmpty()) {
              currentSplit = m_splits.first();
            }
            if (currentSplit) {
              currentWin = currentSplit->getCurrentViewWindow();
            }
            qCDebug(lcWorkspace) << "ViewArea2: singleShot sync: currentSplit=" << currentSplit
                                 << "currentWin=" << currentWin;
            ID winId = currentWin ? currentWin->getViewWindowId()
                                  : ViewAreaController::InvalidViewWindowId;
            QString bufferId = currentWin ? currentWin->getBuffer().id() : QString();
            m_controller->setCurrentViewWindow(winId, bufferId);

            // If nothing was restored (virtual buffers like vx://home are
            // session-excluded), open the home dashboard so the area is never
            // empty on startup.
            if (m_windows.isEmpty()) {
              maybeOpenHome();
            }

            // Propagation is now enabled and the view area is ready to receive
            // buffers; notify consumers (e.g. MainWindow2 opens command-line /
            // forwarded files here).
            emit corePropagationReady();
          });
        },
        10);
    hookMgr->addAction(
        HookNames::MainWindowBeforeClose,
        [this](HookContext &p_ctx, const QVariantMap &p_args) {
          Q_UNUSED(p_args)
          if (!m_controller->closeAllPreviewWorkspaces()) {
            p_ctx.cancel();
            return;
          }
          // Reattach any detached windows first so their buffers route through
          // the normal close-all path and no top-level window keeps the app alive.
          reattachAllDetachedWindows();
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
          qCDebug(lcWorkspace) << "ViewArea2: shutdown cancelled, re-enabling propagation";
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

  // CloseFocus (Ctrl+G, X) is handled by MainWindow2::setupCloseFocusedShortcut(),
  // which dispatches on the focused widget (hide focused dock, else close tab).

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

  // OpenLastClosedFile.
  {
    auto shortcut = WidgetUtils::createShortcut(
        coreConfig.getShortcut(CoreConfig::Shortcut::OpenLastClosedFile), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this,
              [this]() { m_controller->openLastClosedFile(); });
    }
  }

  // Detach the current tab of the current split into a new window.
  {
    auto shortcut =
        WidgetUtils::createShortcut(coreConfig.getShortcut(CoreConfig::Shortcut::Detach), this);
    if (shortcut) {
      connect(shortcut, &QShortcut::activated, this, [this]() {
        auto *split = getCurrentViewSplit();
        if (!split) {
          return;
        }
        auto *win = split->getCurrentViewWindow();
        if (win) {
          onDetachViewWindowRequested(split, win);
        }
      });
    }
  }
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
  const auto current = m_controller->getCurrentWorkspaceId();
  if (m_previewHosts.contains(current) && isPreviewWorkspaceAvailable(current)) {
    return hostedSplitForWorkspace(current);
  }
  for (auto *split : getAllViewSplits()) {
    if (split->isActive()) {
      return split;
    }
  }
  return nullptr;
}

int ViewArea2::getViewSplitCount() const { return getAllViewSplits().size(); }

QVector<ViewSplit2 *> ViewArea2::navigationViewSplits() const {
  QVector<ViewSplit2 *> result;
  for (auto *split : getAllViewSplits()) {
    const auto *source = split->getCurrentViewWindow();
    const auto preview = source ? previewWorkspaceForWindow(source->getViewWindowId()) : QString();
    if (preview.isEmpty() || !isPreviewWorkspaceAvailable(preview)) {
      result.append(split);
    }
  }
  for (auto it = m_previewHosts.constBegin(); it != m_previewHosts.constEnd(); ++it) {
    if (isPreviewWorkspaceAvailable(it.key())) {
      result.append(it->overlay->getViewSplit());
    }
  }
  return result;
}

void ViewArea2::showNavigation() {
  NavigationMode::showNavigation();
  m_navigationActive = true;
  const auto generation = m_navigationGeneration;
  if (!isVisible() || m_navigationItems.size() >= c_maxNumOfNavigationItems) {
    return;
  }

  // Prepare every slot before a producer can deliver an inline completion.
  for (auto *split : navigationViewSplits()) {
    auto *window = split->getCurrentViewWindow();
    if (split->isVisible() && window && window->isVisible()) {
      NavigationRequest request;
      request.m_split = split;
      request.m_window = window;
      request.m_bufferGeneration = window->getBufferGeneration();
      request.m_mode = window->getMode();
      m_navigationRequests.append(std::move(request));
    }
  }
  const QPointer<ViewArea2> self(this);
  const int count = m_navigationRequests.size();
  for (int index = 0; index < count; ++index) {
    if (!self || !self->m_navigationActive || self->m_navigationGeneration != generation) {
      return;
    }
    const auto owner = self->m_navigationRequests[index].m_window;
    auto complete = [self, generation, index](QVector<NavigationTarget> p_targets) {
      if (!self || !self->m_navigationActive || self->m_navigationGeneration != generation) {
        return;
      }
      auto &request = self->m_navigationRequests[index];
      if (request.m_completed) {
        return;
      }
      if (request.m_window && request.m_split && request.m_window->isVisible() &&
          request.m_split->isVisible() && request.m_window->getMode() == request.m_mode &&
          request.m_window->getBufferGeneration() == request.m_bufferGeneration &&
          self->navigationViewSplits().contains(request.m_split) &&
          request.m_split->getCurrentViewWindow() == request.m_window) {
        request.m_targets = std::move(p_targets);
      }
      request.m_completed = true;
      self->appendReadyNavigationTargets();
    };
    if (owner) {
      owner->fetchNavigationTargets(std::move(complete));
    } else {
      complete({});
    }
  }
}

QVector<void *> ViewArea2::getVisibleNavigationItems() {
  QVector<void *> items;
  m_navigationItems.reserve(c_maxNumOfNavigationItems);
  items.reserve(c_maxNumOfNavigationItems);
  for (auto *split : navigationViewSplits()) {
    if (split->getViewWindowCount() == 0) {
      continue;
    }
    const auto info = split->getNavigationModeInfo();
    for (const auto &tab : info) {
      if (m_navigationItems.size() >= c_maxNumOfNavigationItems) {
        return items;
      }
      NavigationItem item;
      item.m_window = tab.m_viewWindow;
      item.m_bufferGeneration = tab.m_viewWindow->getBufferGeneration();
      item.m_target.m_widget = split;
      item.m_target.m_rect = QRect(tab.m_topLeft, QSize(1, 1));
      m_navigationItems.append(std::move(item));
      items.append(&m_navigationItems.last());
    }
  }
  return items;
}

void ViewArea2::appendReadyNavigationTargets() {
  while (m_nextNavigationRequest < m_navigationRequests.size() &&
         m_navigationRequests[m_nextNavigationRequest].m_completed) {
    auto request = std::move(m_navigationRequests[m_nextNavigationRequest++]);
    if (!request.m_window || !request.m_split || !request.m_window->isVisible() ||
        !request.m_split->isVisible() || request.m_window->getMode() != request.m_mode ||
        request.m_window->getBufferGeneration() != request.m_bufferGeneration ||
        !navigationViewSplits().contains(request.m_split) ||
        request.m_split->getCurrentViewWindow() != request.m_window) {
      continue;
    }
    for (auto &target : request.m_targets) {
      if (m_navigationItems.size() >= c_maxNumOfNavigationItems) {
        break;
      }
      if (!target.m_widget || !target.m_widget->isVisible() || !target.m_activate) {
        continue;
      }
      target.m_rect = target.m_rect.intersected(target.m_widget->rect());
      if (target.m_rect.isEmpty()) {
        continue;
      }
      NavigationItem item;
      item.m_window = request.m_window;
      item.m_bufferGeneration = request.m_bufferGeneration;
      item.m_target = std::move(target);
      item.m_content = true;
      m_navigationItems.append(std::move(item));
      appendNavigationItem(&m_navigationItems.last());
    }
  }
}

void ViewArea2::placeNavigationLabel(int p_idx, void *p_item, QLabel *p_label) {
  Q_UNUSED(p_idx);
  const auto &item = *static_cast<NavigationItem *>(p_item);
  auto *widget = item.m_target.m_widget.data();
  p_label->setParent(widget);
  p_label->adjustSize();
  const auto topLeft = item.m_target.m_rect.intersected(widget->rect()).topLeft();
  p_label->move(qBound(0, topLeft.x(), qMax(0, widget->width() - p_label->width())),
                qBound(0, topLeft.y(), qMax(0, widget->height() - p_label->height())));
}

void ViewArea2::handleTargetHit(void *p_item) {
  if (!p_item) {
    return;
  }
  // Selection exits NavigationModeService only after this call returns. Copy
  // before any focus/callback can clear the pointer-backed item storage.
  const auto item = *static_cast<NavigationItem *>(p_item);
  if (!item.m_window || item.m_window->getBufferGeneration() != item.m_bufferGeneration) {
    return;
  }
  if (item.m_content) {
    QTimer::singleShot(0, this, [this, item]() {
      if (!isVisible() || !item.m_window || !item.m_window->isVisible() ||
          item.m_window->getBufferGeneration() != item.m_bufferGeneration ||
          !item.m_target.m_widget || !item.m_target.m_widget->isVisible() ||
          !item.m_target.m_activate) {
        return;
      }
      for (auto *split : navigationViewSplits()) {
        if (split->isVisible() && split->getCurrentViewWindow() == item.m_window) {
          item.m_target.m_activate();
          return;
        }
      }
    });
    return;
  }
  for (auto *split : navigationViewSplits()) {
    if (split->indexOf(item.m_window) != -1) {
      const QPointer<ViewSplit2> guardedSplit(split);
      split->setCurrentViewWindow(item.m_window);
      if (guardedSplit) {
        guardedSplit->focus();
      }
      return;
    }
  }
}

void ViewArea2::clearNavigation() {
  ++m_navigationGeneration;
  m_navigationActive = false;
  m_navigationRequests.clear();
  m_nextNavigationRequest = 0;
  NavigationMode::clearNavigation();
  m_navigationItems.clear();
}

QSize ViewArea2::sizeHint() const {
  const QSize preferredSize(400, 300);
  auto sz = QWidget::sizeHint();
  if (sz.width() < preferredSize.width()) {
    sz = preferredSize;
  }
  return sz;
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

bool ViewArea2::hasRestorableWindows() const {
  auto *bufferSvc = m_services.get<BufferService>();
  for (auto it = m_windows.constBegin(); it != m_windows.constEnd(); ++it) {
    ViewWindow2 *win = it.value();
    if (win && (!bufferSvc || !bufferSvc->isVirtualBuffer(win->getBuffer().id()))) {
      return true;
    }
  }
  return false;
}

QJsonObject ViewArea2::saveLayout() const {
  // If no restorable (non-virtual) buffers are open, save an empty layout so that
  // on next startup loadLayout() sees an empty splitterTree. Virtual buffers
  // (e.g. vx://home, vx://settings) are session-excluded by vxcore and must not
  // pin a workspace/splitter layout that references a buffer that won't exist
  // after restart.
  if (!hasRestorableWindows()) {
    qCDebug(lcWorkspace) << "ViewArea2::saveLayout: no restorable buffers, saving empty layout";
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
  qCDebug(lcWorkspace) << "ViewArea2::saveSession: syncing buffer order and persisting session";

  auto *bufferSvc = m_services.get<BufferService>();
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
        // Virtual buffers (vx://home, vx://settings, ...) are not restorable.
        // Explicitly REMOVE them from the workspace: setBufferOrder() alone is
        // reorder-only (vxcore re-appends any existing ID omitted from the new
        // order), so omission does not drop them. removeBuffer() also clears the
        // workspace's currentBufferId if it pointed at the virtual buffer.
        if (bufferSvc && bufferSvc->isVirtualBuffer(bufferId)) {
          wsSvc->removeBuffer(it.key(), bufferId);
          continue;
        }
        bufferIds.append(bufferId);

        // Persist per-buffer metadata (mode, positions, read-only state).
        QJsonObject bufMeta;
        ViewWindowMode mode = win->getMode();
        bufMeta[QStringLiteral("mode")] =
            (mode == ViewWindowMode::Edit) ? QStringLiteral("Edit") : QStringLiteral("Read");
        bufMeta[QStringLiteral("cursorPosition")] = win->getCursorPosition();
        bufMeta[QStringLiteral("scrollPosition")] = win->getScrollPosition();
        bufMeta[QLatin1String(vxcore::kJsonKeyReadOnly)] = win->getBuffer().isReadOnly();
        wsSvc->setBufferMetadata(it.key(), bufferId, bufMeta);
      }
      wsSvc->setBufferOrder(it.key(), bufferIds);

      auto *currentWin = it.value()->getCurrentViewWindow();
      if (currentWin) {
        const QString currentBufferId = currentWin->getBuffer().id();
        if (!bufferSvc || !bufferSvc->isVirtualBuffer(currentBufferId)) {
          wsSvc->setCurrentBuffer(it.key(), currentBufferId);
        }
      }

      qCDebug(lcWorkspace) << "  Synced workspace" << it.key() << "buffer order:" << bufferIds
                           << "currentBuffer:"
                           << (currentWin ? currentWin->getBuffer().id()
                                          : QStringLiteral("(none)"));
    }

    // Remove empty workspaces (no buffers) from vxcore.
    QJsonArray allWorkspaces = wsSvc->listWorkspaces();
    for (int i = 0; i < allWorkspaces.size(); ++i) {
      QJsonObject wsObj = allWorkspaces[i].toObject();
      QString wsId = wsObj.value(QStringLiteral("id")).toString();
      QJsonArray bufferIds = wsObj.value(QStringLiteral("bufferIds")).toArray();
      if (bufferIds.isEmpty()) {
        qCDebug(lcWorkspace) << "  Removing empty workspace:" << wsId;
        wsSvc->deleteWorkspace(wsId);
      }
    }
  }

  // Disable propagation so buffer closes during teardown don't touch vxcore.
  m_controller->setShouldPropagateToCore(false);

  qCDebug(lcWorkspace) << "ViewArea2::saveSession: done";
}

void ViewArea2::restoreSession() {
  QStringList layoutWorkspaceIds = m_splits.keys();
  m_controller->restoreSession(layoutWorkspaceIds);
}

// ============ ID resolution helpers ============

ViewSplit2 *ViewArea2::splitForWorkspace(const QString &p_workspaceId) const {
  return m_splits.value(p_workspaceId, nullptr);
}

bool ViewArea2::isPreviewSourceCurrent(ID p_sourceId, quint64 p_generation) const {
  const auto *source = windowForId(p_sourceId);
  return source && source->getViewWindowId() == p_sourceId && source->isEnabled() &&
         source->isVisible() && source->getBufferGeneration() == p_generation &&
         source->getBuffer().isValid() && !source->getBuffer().getBuffer().isEmpty() &&
         !source->getNodeId().isVirtual() && !source->isNoteConversionFrozen();
}

ViewSplit2 *ViewArea2::hostedSplitForWorkspace(const QString &p_workspaceId) const {
  if (auto *split = splitForWorkspace(p_workspaceId)) {
    return split;
  }
  if (auto *split = detachedSplitForWorkspace(p_workspaceId)) {
    return split;
  }
  const auto host = m_previewHosts.constFind(p_workspaceId);
  return host != m_previewHosts.constEnd() && host->overlay ? host->overlay->getViewSplit()
                                                            : nullptr;
}

ViewSplit2 *ViewArea2::owningSplitForWindow(ID p_windowId) const {
  const auto *window = windowForId(p_windowId);
  if (!window) {
    return nullptr;
  }
  for (auto *split : m_splits) {
    if (split->indexOf(window) >= 0) {
      return split;
    }
  }
  for (auto *detached : m_detachedWindows) {
    auto *split = detached->getViewSplit();
    if (split && split->indexOf(window) >= 0) {
      return split;
    }
  }
  for (const auto &host : m_previewHosts) {
    auto *split = host.overlay ? host.overlay->getViewSplit() : nullptr;
    if (split && split->indexOf(window) >= 0) {
      return split;
    }
  }
  return nullptr;
}

QString ViewArea2::previewWorkspaceForWindow(ID p_windowId) const {
  if (auto *split = owningSplitForWindow(p_windowId); split && split->isPreview()) {
    return split->getWorkspaceId();
  }
  for (auto it = m_previewHosts.constBegin(); it != m_previewHosts.constEnd(); ++it) {
    if (it->sourceId == p_windowId && it->source == windowForId(p_windowId)) {
      return it.key();
    }
  }
  return QString();
}

bool ViewArea2::isPreviewWorkspaceAvailable(const QString &p_workspaceId) const {
  const auto host = m_previewHosts.constFind(p_workspaceId);
  return host != m_previewHosts.constEnd() && host->overlay && host->overlay->isVisible() &&
         host->source && host->source == windowForId(host->sourceId) &&
         isPreviewSourceCurrent(host->sourceId, host->generation) &&
         !m_controller->isPreviewWorkspaceClosing(p_workspaceId);
}

bool ViewArea2::rebindPreviewWorkspaceSource(const QString &p_workspaceId, ID p_sourceId) {
  auto host = m_previewHosts.find(p_workspaceId);
  auto *source = p_sourceId ? windowForId(p_sourceId) : nullptr;
  if (host == m_previewHosts.end() || !host->overlay || (p_sourceId && !source)) {
    return false;
  }
  if (source) {
    auto *owner = owningSplitForWindow(p_sourceId);
    const auto existing = previewWorkspaceForWindow(p_sourceId);
    if (!owner || owner->isPreview() || (!existing.isEmpty() && existing != p_workspaceId)) {
      return false;
    }
  }
  const QPointer<ViewArea2> self(this);
  const QPointer<LinkPreviewOverlay> overlay(host->overlay);
  const QPointer<ViewWindow2> guardedSource(source);
  const bool shown = !overlay->isHidden();
  if (host->source) {
    host->source->m_closeLinkPreview = {};
    host->source->removeEventFilter(this);
    for (auto *child : host->source->findChildren<QWidget *>()) {
      if (child != overlay && !overlay->isAncestorOf(child)) {
        child->removeEventFilter(this);
      }
    }
  }
  host->source = source;
  host->sourceId = p_sourceId;
  host->generation = source ? source->getBufferGeneration() : 0;
  if (!source) {
    return true;
  }
  source->installEventFilter(this);
  source->m_closeLinkPreview = [self, guardedSource, p_sourceId, p_workspaceId]() {
    if (!self || !guardedSource) {
      return true;
    }
    const auto current = self->m_previewHosts.constFind(p_workspaceId);
    if (current == self->m_previewHosts.constEnd() || current->source != guardedSource ||
        current->sourceId != p_sourceId) {
      return true;
    }
    return self->m_controller->closePreviewWorkspace(p_workspaceId);
  };
  overlay->setParent(source);
  if (!self || !overlay || !guardedSource) {
    return false;
  }
  updatePreviewAnchor(p_workspaceId);
  if (!self || !overlay || !guardedSource) {
    return false;
  }
  // setParent hides even a previously shown native child.
  if (shown) {
    overlay->show();
  }
  return self && overlay && guardedSource;
}

bool ViewArea2::hostPreviewWorkspace(ID p_sourceId, quint64 p_generation,
                                     const QString &p_workspaceId) {
  if (!isPreviewSourceCurrent(p_sourceId, p_generation) ||
      !previewWorkspaceForWindow(p_sourceId).isEmpty() || hostedSplitForWorkspace(p_workspaceId)) {
    return false;
  }
  auto *source = windowForId(p_sourceId);
  auto *overlay = new LinkPreviewOverlay(m_services, source, p_workspaceId);
  m_previewHosts.insert(p_workspaceId, {nullptr, 0, 0, overlay, false});
  rebindPreviewWorkspaceSource(p_workspaceId, p_sourceId);
  wirePreviewSplitSignals(overlay->getViewSplit());
  connect(overlay, &LinkPreviewOverlay::closeRequested, this,
          [this, p_workspaceId] { m_controller->closePreviewWorkspace(p_workspaceId); });
  connect(overlay, &LinkPreviewOverlay::openInNewTabsRequested, this, [this, p_workspaceId] {
    const auto host = m_previewHosts.constFind(p_workspaceId);
    if (host == m_previewHosts.constEnd() || !host->source) {
      return;
    }
    if (auto *owner = owningSplitForWindow(host->sourceId); owner && !owner->isPreview()) {
      const QString targetWorkspaceId = owner->getWorkspaceId();
      m_controller->openPreviewInNewTabs(p_workspaceId, targetWorkspaceId);
    }
  });
  connect(overlay, &LinkPreviewOverlay::visibilityChanged, this,
          [this, p_workspaceId](bool p_visible) {
            m_controller->setPreviewWorkspaceVisible(p_workspaceId, p_visible);
          });
  updatePreviewAnchor(p_workspaceId);
  overlay->show();
  return isPreviewWorkspaceAvailable(p_workspaceId);
}

void ViewArea2::removePreviewWorkspaceHost(const QString &p_workspaceId) {
  const bool active = m_controller->getCurrentWorkspaceId() == p_workspaceId;
  rebindPreviewWorkspaceSource(p_workspaceId, 0);
  const auto host = m_previewHosts.take(p_workspaceId);
  if (host.overlay) {
    host.overlay->hide();
    host.overlay->deleteLater();
  }
  if (active) {
    m_controller->clearActivePreviewWindow();
  }
}

bool ViewArea2::closePreviewsForSources(const QVector<ID> &p_sourceIds) {
  const QPointer<ViewArea2> guard(this);
  for (const ID sourceId : p_sourceIds) {
    const auto *source = windowForId(sourceId);
    const auto closePreview = source ? source->m_closeLinkPreview : std::function<bool()>();
    if (closePreview && !closePreview()) {
      return false;
    }
    if (!guard) {
      return false;
    }
  }
  return true;
}

QStringList ViewArea2::previewWorkspacesForSourceNotebook(const QString &p_notebookId) const {
  QStringList result;
  for (auto it = m_previewHosts.constBegin(); it != m_previewHosts.constEnd(); ++it) {
    if (it->source && it->source->getNodeId().notebookId == p_notebookId) {
      result.append(it.key());
    }
  }
  return result;
}

void ViewArea2::updatePreviewAnchor(const QString &p_workspaceId) {
  auto host = m_previewHosts.find(p_workspaceId);
  if (host == m_previewHosts.end() || !host->source || !host->overlay) {
    return;
  }
  host->anchorUpdatePending = false;
  auto *source = host->source.data();
  if (source->layout()) {
    source->layout()->activate();
  }
  if (source->m_contentLayout) {
    host->overlay->setAnchorRect(
        source->m_contentLayout->geometry().intersected(source->contentsRect()));
    // Observe late native reader children without treating preview descendants as anchors.
    for (auto *child : source->findChildren<QWidget *>()) {
      if (child != host->overlay && !host->overlay->isAncestorOf(child)) {
        child->installEventFilter(this);
      }
    }
  }
}

bool ViewArea2::eventFilter(QObject *p_object, QEvent *p_event) {
  switch (p_event->type()) {
  case QEvent::Resize:
  case QEvent::LayoutRequest:
  case QEvent::Show:
  case QEvent::WinIdChange:
  case QEvent::StyleChange:
  case QEvent::FontChange:
  case QEvent::PaletteChange:
  case QEvent::ChildAdded:
    for (auto it = m_previewHosts.begin(); it != m_previewHosts.end(); ++it) {
      auto *widget = qobject_cast<QWidget *>(p_object);
      if (!widget || !it->source || !it->overlay || it->anchorUpdatePending ||
          (widget != it->source && !it->source->isAncestorOf(widget)) || widget == it->overlay ||
          it->overlay->isAncestorOf(widget)) {
        continue;
      }
      it->anchorUpdatePending = true;
      QTimer::singleShot(0, this,
                         [this, workspaceId = it.key()] { updatePreviewAnchor(workspaceId); });
    }
    break;
  default:
    break;
  }
  return QWidget::eventFilter(p_object, p_event);
}

ViewWindow2 *ViewArea2::windowForId(ID p_windowId) const {
  return m_windows.value(p_windowId, nullptr);
}

void ViewArea2::getCurrentWindowInfoForWorkspace(const QString &p_workspaceId, ID &p_windowId,
                                                 QString &p_bufferId) const {
  p_windowId = ViewAreaController::InvalidViewWindowId;
  p_bufferId.clear();
  if (p_workspaceId.isEmpty()) {
    return;
  }
  auto *split = hostedSplitForWorkspace(p_workspaceId);
  if (!split) {
    return;
  }
  ViewWindow2 *win = split->getCurrentViewWindow();
  if (!win) {
    return;
  }
  p_windowId = win->getViewWindowId();
  p_bufferId = win->getBuffer().id();
}

void ViewArea2::saveAllViewWindows() {
  // Include hidden, detached and preview tabs. Save hooks may close windows.
  QVector<QPointer<ViewWindow2>> windows;
  windows.reserve(m_windows.size());
  for (auto *win : m_windows) {
    windows.append(QPointer<ViewWindow2>(win));
  }
  QSet<QString> savedBuffers;
  for (const auto &win : windows) {
    if (!win || !win->isSaveSupported() || win->isNoteConversionFrozen() || !win->isModified()) {
      continue;
    }
    const auto &buffer = win->getBuffer();
    if (!buffer.isValid() || buffer.isReadOnly() || savedBuffers.contains(buffer.id())) {
      continue;
    }
    savedBuffers.insert(buffer.id());
    win->save();
  }
}

void ViewArea2::wirePreviewSplitSignals(ViewSplit2 *p_split) {
  const auto workspaceId = p_split->getWorkspaceId();
  const auto activate = [this, workspaceId](ViewWindow2 *p_window) {
    if (p_window) {
      m_controller->setActivePreviewWindow(workspaceId, p_window->getViewWindowId(),
                                           p_window->getBuffer().id());
    } else if (m_controller->getCurrentWorkspaceId() == workspaceId) {
      m_controller->clearActivePreviewWindow();
    }
  };
  connect(p_split, &ViewSplit2::focused, this,
          [activate](ViewSplit2 *p_focused) { activate(p_focused->getCurrentViewWindow()); });
  connect(p_split, &ViewSplit2::currentViewWindowChanged, this, activate);
  connect(p_split, &ViewSplit2::viewWindowCloseRequested, this, [this](ViewWindow2 *p_window) {
    m_controller->closeViewWindow(p_window->getViewWindowId(), false);
  });
  connect(p_split, &ViewSplit2::bufferOrderChanged, this,
          [this, workspaceId](ViewSplit2 *, const QStringList &p_ids) {
            m_controller->setBufferOrder(workspaceId, p_ids);
          });
  connect(p_split, &ViewSplit2::saveAllRequested, this, &ViewArea2::saveAllViewWindows);
  connect(p_split, &ViewSplit2::closeTabsRequested, this,
          [this, workspaceId](ViewSplit2 *, int p_index, CloseTabMode p_mode) {
            m_controller->closeTabs(workspaceId, p_index, p_mode);
          });
  connect(p_split, &ViewSplit2::locateNodeRequested, m_controller,
          &ViewAreaController::locateNodeRequested);
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
    qCDebug(lcWorkspace) << "ViewArea2: split currentViewWindowChanged, win:" << w
                         << "bufferId:" << bufferId;
    m_controller->setCurrentViewWindow(
        w ? w->getViewWindowId() : ViewAreaController::InvalidViewWindowId, bufferId);
  });
  connect(p_split, &ViewSplit2::splitRequested, this,
          [this, wsId](ViewSplit2 *, Direction d) { m_controller->splitViewSplit(wsId, d); });
  connect(p_split, &ViewSplit2::maximizeSplitRequested, this,
          [this, wsId](ViewSplit2 *) { m_controller->maximizeViewSplit(wsId); });
  connect(p_split, &ViewSplit2::distributeSplitsRequested, this,
          [this]() { m_controller->distributeViewSplits(); });

  // Double click on the empty tab bar area: trigger a quick note using the same
  // logic as the New -> Quick Note action in the main window toolbar.
  connect(p_split, &ViewSplit2::newQuickNoteRequested, this,
          [this]() { m_controller->requestQuickNote(); });

  // Signals handled by ViewArea2 (need pointer context).
  connect(p_split, &ViewSplit2::removeSplitRequested, this, &ViewArea2::onRemoveSplitRequested);
  connect(p_split, &ViewSplit2::removeSplitAndWorkspaceRequested, this,
          &ViewArea2::onRemoveSplitAndWorkspaceRequested);
  connect(p_split, &ViewSplit2::moveViewWindowOneSplitRequested, this,
          &ViewArea2::onMoveViewWindowOneSplitRequested);
  connect(p_split, &ViewSplit2::detachViewWindowRequested, this,
          &ViewArea2::onDetachViewWindowRequested);

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
  connect(p_split, &ViewSplit2::removeOtherWorkspacesRequested, this, [this](ViewSplit2 *s) {
    // Copy the workspace ID since the split's ID may change during teardown.
    QString keepId = s->getWorkspaceId();
    m_controller->removeOtherWorkspaces(keepId);
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

  connect(p_split, &ViewSplit2::saveAllRequested, this, &ViewArea2::saveAllViewWindows);

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
  const ID homeId = splitForWorkspace(p_workspaceId) ? loneHomeWindowId() : 0;
  auto *split = hostedSplitForWorkspace(p_workspaceId);
  if (split && split->isPreview() && !isPreviewWorkspaceAvailable(p_workspaceId)) {
    return;
  }
  if (!split) {
    qWarning() << "ViewArea2::openBuffer: workspace not found:" << p_workspaceId;
    return;
  }
  const bool locked = p_buffer.isEncrypted() && p_buffer.editorType().isEmpty();
  auto *factory = m_services.get<ViewWindowFactory>();
  if (!locked && (!factory || !factory->hasCreator(p_fileType))) {
    qWarning() << "ViewArea2: no creator for file type" << p_fileType;
    emit viewWindowCreationFailed(p_fileType, p_buffer.nodeId().relativePath);
    return;
  }
  const QPointer<ViewWindow2> previousWindow(split->getCurrentViewWindow());
  const QPointer<QWidget> previousFocus(QApplication::focusWidget());
  const auto restoreBackgroundFocus = qScopeGuard([previousFocus, &p_settings]() {
    if (!p_settings.m_focus && previousFocus && QApplication::focusWidget() != previousFocus) {
      previousFocus->setFocus(Qt::OtherFocusReason);
    }
  });
  QPointer<ViewWindow2> win;
  ID id = ViewAreaController::InvalidViewWindowId;
  {
    // Constructors and addViewWindow can focus/show the new editor before
    // registration. A background open must not publish that transient focus
    // as the main area's current tab or workspace.
    QSignalBlocker constructionSignals(split);
    if (p_settings.m_detachedView ||
        (p_settings.m_focus && p_workspaceId == m_controller->getCurrentWorkspaceId())) {
      constructionSignals.unblock();
    }
    win = locked ? static_cast<ViewWindow2 *>(
                       new LockedNoteView(m_services, p_buffer, p_settings, split))
                 : factory->create(p_fileType, m_services, p_buffer, split, p_settings.m_mode);
    if (!win) {
      qWarning() << "ViewArea2: failed creating view window for type" << p_fileType;
      emit viewWindowCreationFailed(p_fileType, p_buffer.nodeId().relativePath);
      return;
    }
    id = m_nextWindowId++;
    m_windows[id] = win;
    win->setViewWindowId(id);
    split->addViewWindow(win);
    // addViewWindow explicitly shows even a non-current tab. Preserve the
    // previous selection and keep the background editor hidden.
    if (!p_settings.m_focus && previousWindow && split->indexOf(previousWindow) >= 0) {
      split->setCurrentViewWindow(previousWindow);
    }
    win->setVisible(split->getCurrentViewWindow() == win);
  }
  connect(win, &ViewWindow2::closeRequested, this,
          [this, id]() { m_controller->closeViewWindow(id, true); });
  connect(win, &ViewWindow2::exportRequested, this, [this, win]() { emit exportRequested(win); });
  m_controller->onViewWindowOpened(id, p_buffer, p_settings, p_workspaceId);

  if (!win || findWindowIdByBufferId(p_workspaceId, p_buffer.id()) != id) {
    return;
  }
  // Select a focused open after registration and its after-open hook, as for
  // ordinary tab creation. The first tab was reconciled after membership add.
  if (p_settings.m_focus) {
    setCurrentBuffer(p_workspaceId, p_buffer.id(), true);
  }

  // Apply file open settings (scroll to line, search highlight).
  win->applyFileOpenSettings(p_settings);

  // The Home dashboard is a placeholder for an empty view area: once a real
  // file is open it has served its purpose, so close it. Done AFTER the new
  // window exists, so the main area is never empty and maybeOpenHome() does
  // not immediately re-open it.
  if (homeId != 0) {
    m_controller->closeViewWindow(homeId, true);
  }

  updateScreenVisibility();
}

bool ViewArea2::canReuseViewWindow(ID p_windowId, const QString &p_fileType) const {
  const auto *window = windowForId(p_windowId);
  return window && window->supportsBufferReplacement() &&
         window->getFileType() == p_fileType.toLower();
}

bool ViewArea2::replaceViewWindowBuffer(ID p_windowId, const Buffer2 &p_buffer,
                                        const FileOpenSettings &p_settings,
                                        ClosedTabRecord *p_closedTab) {
  const QPointer<ViewWindow2> window(windowForId(p_windowId));
  if (!window || !window->supportsBufferReplacement() || window->getNodeId().isVirtual()) {
    return false;
  }
  QPointer<ViewSplit2> owner(owningSplitForWindow(p_windowId));
  if (!owner) {
    return false;
  }
  const ClosedTabRecord record{window->getNodeId(), window->getMode(), window->getCursorPosition()};
  clearNavigation();
  // Preserve internal mode/action updates, but do not publish split focus/current
  // changes while the controller still owns both workspace memberships.
  const bool wasBlocked = owner->blockSignals(true);
  const auto unblock = qScopeGuard([owner, wasBlocked]() {
    if (owner) {
      owner->blockSignals(wasBlocked);
    }
  });
  const bool replaced = window->replaceBuffer(p_buffer, p_settings);
  if (!replaced || !window) {
    return false;
  }
  if (p_closedTab) {
    *p_closedTab = record;
  }
  const auto generation = window->getBufferGeneration();
  QTimer::singleShot(0, this, [window, generation]() {
    if (window && window->getBufferGeneration() == generation) {
      emit window->nameChanged();
      if (window && window->getBufferGeneration() == generation) {
        emit window->statusChanged();
      }
    }
  });
  return true;
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
  connect(win, &ViewWindow2::closeRequested, this,
          [this, id]() { m_controller->closeViewWindow(id, true); });
  connect(win, &ViewWindow2::exportRequested, this, [this, win]() { emit exportRequested(win); });

  // Use default FileOpenSettings for notification.
  FileOpenSettings settings;
  settings.m_focus = true;
  m_controller->onViewWindowOpened(id, p_buffer, settings, p_workspaceId);
  split->setCurrentViewWindow(win);

  updateScreenVisibility();
}

void ViewArea2::applyFileOpenSettings(ID p_windowId, const FileOpenSettings &p_settings) {
  auto *win = windowForId(p_windowId);
  if (win) {
    win->applyFileOpenSettings(p_settings);
  }
}

bool ViewArea2::setNoteConversionFrozen(const QVector<ID> &p_windowIds, bool p_frozen) {
  for (const ID id : p_windowIds) {
    if (!windowForId(id)) {
      return false;
    }
  }
  for (const ID id : p_windowIds) {
    windowForId(id)->setNoteConversionFrozen(p_frozen);
  }
  return true;
}

bool ViewArea2::recreateNoteViews(
    const QVector<ID> &p_windowIds, const Buffer2 &p_buffer, const QString &p_editorType,
    const std::function<void(QObject *, QObject *)> &p_replaceHidden) {
  auto *factory = m_services.get<ViewWindowFactory>();
  const QString fileType = p_editorType == QLatin1String("markdown")  ? QStringLiteral("Markdown")
                           : p_editorType == QLatin1String("text")    ? QStringLiteral("Text")
                           : p_editorType == QLatin1String("mindmap") ? QStringLiteral("MindMap")
                                                                      : QString();
  if (!factory || fileType.isEmpty() || !factory->hasCreator(fileType)) {
    return false;
  }
  struct Replacement {
    ID id;
    ViewWindow2 *oldWindow;
    ViewWindow2 *newWindow;
    ViewSplit2 *split;
    ViewWindow2::ViewPositionState position;
  };
  QVector<Replacement> replacements;
  for (const ID id : p_windowIds) {
    auto *oldWindow = windowForId(id);
    if (!oldWindow || !oldWindow->isNoteConversionFrozen()) {
      for (const auto &item : replacements) {
        delete item.newWindow;
      }
      return false;
    }
    ViewSplit2 *owner = owningSplitForWindow(id);
    auto *newWindow =
        factory->create(fileType, m_services, p_buffer, nullptr, oldWindow->getMode());
    if (!newWindow) {
      for (const auto &item : replacements) {
        delete item.newWindow;
      }
      return false;
    }
    newWindow->setNoteConversionFrozen(true);
    newWindow->setAutoReload(oldWindow->autoReload());
    replacements.append({id, oldWindow, newWindow, owner, oldWindow->capturePositionState()});
  }

  // All replacement owners exist before any old document is released. Do not
  // call aboutToClose(): it saves dirty plaintext and runs legacy-asset cleanup.
  for (const auto &item : replacements) {
    item.newWindow->setViewWindowId(item.id);
    m_windows[item.id] = item.newWindow;
    if (item.split) {
      const QSignalBlocker blocker(item.split);
      const int index = item.split->indexOf(item.oldWindow);
      const bool current = item.split->getCurrentViewWindow() == item.oldWindow;
      item.split->takeViewWindow(item.oldWindow);
      item.split->addViewWindow(item.newWindow);
      item.split->tabBar()->moveTab(item.split->indexOf(item.newWindow), index);
      if (current) {
        item.split->setCurrentViewWindow(item.newWindow);
      }
    } else {
      p_replaceHidden(item.oldWindow, item.newWindow);
    }
    connect(item.newWindow, &ViewWindow2::closeRequested, this,
            [this, id = item.id]() { m_controller->closeViewWindow(id, true); });
    connect(item.newWindow, &ViewWindow2::exportRequested, this,
            [this, win = item.newWindow]() { emit exportRequested(win); });
    delete item.oldWindow;
    item.newWindow->restorePositionState(item.position);
    item.newWindow->setNoteConversionFrozen(false);
  }
  updateScreenVisibility();
  return true;
}

void ViewArea2::navigateWidgetContent(ID p_windowId, const QStringList &p_pathSegments,
                                      const QString &p_fragment) {
  auto *win = windowForId(p_windowId);
  if (!win) {
    return;
  }
  auto *widgetWindow = dynamic_cast<WidgetViewWindow2 *>(win);
  if (!widgetWindow) {
    return;
  }
  auto *content = widgetWindow->getContent();
  if (content) {
    content->navigateTo(p_pathSegments, p_fragment);
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

  auto *ownerSplit = owningSplitForWindow(p_windowId);
  DetachedWindow *ownerDetached = nullptr;
  QString bufferId;
  QString workspaceId = ownerSplit ? ownerSplit->getWorkspaceId() : QString();
  for (auto *dw : m_detachedWindows) {
    if (dw->getViewSplit() == ownerSplit) {
      ownerDetached = dw;
      break;
    }
  }
  if (ownerSplit) {
    Buffer2 buf = win->getBuffer();
    bufferId = buf.id();
    ownerSplit->takeViewWindow(win);
  } else {
    // Hidden ordinary sources retain their preview close gate too. Release the
    // wrapper owner before destruction regardless of encryption classification.
    bufferId = win->getBuffer().id();
    workspaceId = m_controller->releaseHiddenViewWindow(win);
  }

  // Capture state for "Open Last Closed File" before the window is destroyed.
  ClosedTabRecord closedTab;
  closedTab.nodeId = win->getNodeId();
  closedTab.mode = win->getMode();
  closedTab.cursorPosition = win->getCursorPosition();

  m_windows.remove(p_windowId);
  // Use deleteLater() instead of delete: closeViewWindow may be reached
  // synchronously from within a ViewWindow2 method (e.g. handleExternalChange
  // emitting closeRequested). deleteLater defers destruction until the event
  // loop is idle, after any in-flight signals targeting `win` have drained,
  // making the close primitive safe regardless of caller stack.
  const auto finishClose = [this, p_windowId, bufferId, workspaceId, closedTab,
                            detached = QPointer<DetachedWindow>(ownerDetached)]() {
    m_controller->onViewWindowClosed(p_windowId, bufferId, workspaceId, closedTab);
    if (detached && detached->getViewSplit() &&
        detached->getViewSplit()->getViewWindowCount() == 0) {
      m_detachedWindows.removeAll(detached.data());
      m_controller->reattachDetachedWorkspace(workspaceId, QString(), {});
      detached->deleteLater();
    }
  };
  if (win->getBuffer().isEncrypted()) {
    connect(win, &QObject::destroyed, this, finishClose);
    win->deleteLater();
  } else {
    win->deleteLater();
    finishClose();
  }

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
  auto *split = hostedSplitForWorkspace(p_workspaceId);
  if (split) {
    split->focus();
  }
}

void ViewArea2::moveViewWindowToSplit(ID p_windowId, const QString &p_srcWorkspaceId,
                                      const QString &p_dstWorkspaceId) {
  auto *win = windowForId(p_windowId);
  auto *srcSplit = owningSplitForWindow(p_windowId);
  auto *dstSplit = hostedSplitForWorkspace(p_dstWorkspaceId);
  if (!win || !srcSplit || !dstSplit || srcSplit->isPreview() || dstSplit->isPreview() ||
      srcSplit->getWorkspaceId() != p_srcWorkspaceId) {
    return;
  }

  srcSplit->takeViewWindow(win);
  dstSplit->addViewWindow(win);
  dstSplit->setCurrentViewWindow(win);
}

bool ViewArea2::promotePreviewViewWindow(ID p_windowId, const QString &p_previewWorkspaceId,
                                         const QString &p_targetWorkspaceId,
                                         const std::function<bool()> &p_closeDuplicate) {
  const QPointer<ViewArea2> self(this);
  const QPointer<ViewWindow2> incoming(windowForId(p_windowId));
  const QPointer<ViewSplit2> preview(owningSplitForWindow(p_windowId));
  const QPointer<ViewSplit2> target(hostedSplitForWorkspace(p_targetWorkspaceId));
  if (!incoming || !preview || !target || !preview->isPreview() || target->isPreview() ||
      preview == target || preview->getWorkspaceId() != p_previewWorkspaceId ||
      hostedSplitForWorkspace(p_previewWorkspaceId) != preview) {
    return false;
  }
  const auto host = m_previewHosts.constFind(p_previewWorkspaceId);
  if (host == m_previewHosts.constEnd() || !host->source || !host->overlay) {
    return false;
  }
  const QPointer<ViewWindow2> oldSource(host->source);
  const ID oldSourceId = host->sourceId;
  const QString bufferId = incoming->getBuffer().id();
  const bool replaceSource =
      oldSource->getBuffer().id() == bufferId && owningSplitForWindow(oldSourceId) == target;
  const int index = preview->indexOf(incoming);
  const QPointer<ViewWindow2> previewCurrent(preview->getCurrentViewWindow());
  const QPointer<ViewWindow2> targetCurrent(target->getCurrentViewWindow());
  const auto registered = [&] {
    return self && incoming && preview && target && windowForId(p_windowId) == incoming &&
           hostedSplitForWorkspace(p_previewWorkspaceId) == preview &&
           hostedSplitForWorkspace(p_targetWorkspaceId) == target && !target->isPreview();
  };
  {
    QSignalBlocker previewSignals(preview), targetSignals(target);
    const auto unblock = qScopeGuard([&] {
      if (!preview) {
        previewSignals.dismiss();
      }
      if (!target) {
        targetSignals.dismiss();
      }
    });
    preview->takeViewWindow(incoming);
    if (!registered()) {
      return false;
    }
    // Append: normal close finds the first same-buffer tab, the old destination.
    target->addViewWindow(incoming);
    if (!registered() || owningSplitForWindow(p_windowId) != target) {
      return false;
    }
    // Reparent only after incoming is no longer a descendant of the overlay.
    if (replaceSource && !rebindPreviewWorkspaceSource(p_previewWorkspaceId, p_windowId)) {
      return false;
    }
  }
  if (!registered() || owningSplitForWindow(p_windowId) != target) {
    return false;
  }
  if (p_closeDuplicate) {
    p_closeDuplicate();
  }
  if (!registered() || owningSplitForWindow(p_windowId) != target) {
    return false;
  }
  bool duplicateRemains = false;
  for (auto *window : target->getAllViewWindows()) {
    if (window != incoming && window->getBuffer().id() == bufferId) {
      duplicateRemains = true;
      break;
    }
  }
  // Ownership, not the callback result, decides: a hook may have closed a stale ID.
  if (!duplicateRemains) {
    return true;
  }
  {
    QSignalBlocker previewSignals(preview), targetSignals(target);
    const auto unblock = qScopeGuard([&] {
      if (!preview) {
        previewSignals.dismiss();
      }
      if (!target) {
        targetSignals.dismiss();
      }
    });
    if (replaceSource &&
        (!oldSource || !rebindPreviewWorkspaceSource(p_previewWorkspaceId, oldSourceId))) {
      return false;
    }
    if (!registered()) {
      return false;
    }
    target->takeViewWindow(incoming);
    if (!registered()) {
      return false;
    }
    preview->addViewWindow(incoming);
    if (!registered() || owningSplitForWindow(p_windowId) != preview) {
      return false;
    }
    preview->tabBar()->moveTab(preview->indexOf(incoming), index);
    if (!registered()) {
      return false;
    }
    if (previewCurrent && preview->indexOf(previewCurrent) >= 0) {
      preview->setCurrentViewWindow(previewCurrent);
    }
    if (!registered()) {
      return false;
    }
    if (targetCurrent && target->indexOf(targetCurrent) >= 0) {
      target->setCurrentViewWindow(targetCurrent);
    }
  }
  return false;
}

// ============ ViewSplit2 signal handlers ============

void ViewArea2::onMoveViewWindowOneSplitRequested(ViewSplit2 *p_split, ViewWindow2 *p_win,
                                                  Direction p_direction) {
  auto *target = findSplitByDirection(p_split, p_direction);

  // Only-tab guard (legacy parity: viewarea.cpp:1425-1428). Creating a new split
  // solely to immediately empty the source is a visual no-op.
  const int srcCountBefore = p_split->getAllViewWindows().size();
  if (!target && srcCountBefore <= 1) {
    return;
  }

  bool createdDst = false;
  if (!target) {
    // Auto-create an empty destination split. Pass false so splitViewSplit does
    // not clone the current buffer — the move transfer below fills the new split.
    m_controller->splitViewSplit(p_split->getWorkspaceId(), p_direction,
                                 /*p_openCurrentBuffer=*/false);
    target = findSplitByDirection(p_split, p_direction);
    if (!target) {
      return;
    }
    createdDst = true;
  }

  const QString srcWsId = p_split->getWorkspaceId();
  const QString dstWsId = target->getWorkspaceId();

  m_controller->moveViewWindowOneSplit(srcWsId, p_win->getViewWindowId(), p_direction, dstWsId,
                                       p_win->getBuffer().id());

  // Rollback: if the move was cancelled by ViewWindowBeforeMove (or otherwise
  // no-op'd), an auto-created destination would be left empty. Remove it via
  // the controller so hooks fire.
  if (createdDst) {
    auto *dstSplit = splitForWorkspace(dstWsId);
    if (dstSplit && dstSplit->getAllViewWindows().isEmpty()) {
      m_controller->removeViewSplit(dstWsId, /*p_keepWorkspace=*/false, /*p_force=*/true);
      return;
    }
  }

  // Auto-remove empty source after a successful only-tab move (legacy parity:
  // viewarea.cpp:1443-1447).
  auto *srcSplit = splitForWorkspace(srcWsId);
  if (srcSplit && srcSplit->getAllViewWindows().isEmpty()) {
    m_controller->removeViewSplit(srcWsId, /*p_keepWorkspace=*/false, /*p_force=*/true);
  }
}

void ViewArea2::onRemoveSplitRequested(ViewSplit2 *p_split) {
  // Hide-only mode: workspace becomes hidden, split is removed.
  m_controller->removeViewSplit(p_split->getWorkspaceId(), true, false);
}

void ViewArea2::onRemoveSplitAndWorkspaceRequested(ViewSplit2 *p_split) {
  // Full removal mode: workspace is closed (with abort-on-cancel), then split is removed.
  m_controller->removeViewSplit(p_split->getWorkspaceId(), false, false);
}

// ============ Detach / Reattach ============

ViewSplit2 *ViewArea2::detachedSplitForWorkspace(const QString &p_workspaceId) const {
  for (auto *dw : m_detachedWindows) {
    if (dw->getWorkspaceId() == p_workspaceId) {
      return dw->getViewSplit();
    }
  }
  return nullptr;
}

void ViewArea2::onDetachViewWindowRequested(ViewSplit2 *p_split, ViewWindow2 *p_win) {
  if (!p_split || !p_win) {
    return;
  }
  const QString srcWsId = p_split->getWorkspaceId();
  const bool detached =
      m_controller->detachViewWindow(srcWsId, p_win->getViewWindowId(), p_win->getBuffer().id());
  if (!detached) {
    return;
  }

  // Auto-remove the source split if it is now empty and other main splits remain
  // (parity with onMoveViewWindowOneSplitRequested). When it was the only split,
  // the emptied main area re-opens Home via updateScreenVisibility().
  auto *srcSplit = splitForWorkspace(srcWsId);
  if (srcSplit && srcSplit->getViewWindowCount() == 0 && getViewSplitCount() > 1) {
    m_controller->removeViewSplit(srcWsId, /*p_keepWorkspace=*/false, /*p_force=*/true);
  }
}

void ViewArea2::hostWorkspaceInDetachedWindow(const QString &p_workspaceId) {
  auto *dw = new DetachedWindow(m_services, p_workspaceId, nullptr);
  m_detachedWindows.append(dw);

  auto *split = dw->getViewSplit();

  connect(dw, &DetachedWindow::closeRequested, this, &ViewArea2::onCloseDetachedWindow);

  // Wire the minimal subset of split signals needed for a detached window:
  // tab close (routes through the controller) and current-tab tracking (updates
  // the window title). Splitting/moving/workspace ops from a detached window are
  // out of scope for now.
  connect(split, &ViewSplit2::focused, this, [this](ViewSplit2 *) {
    // An ordinary detached editor is not its child preview's command target.
    m_controller->clearActivePreviewWindow();
  });
  connect(split, &ViewSplit2::viewWindowCloseRequested, this,
          [this](ViewWindow2 *w) { m_controller->closeViewWindow(w->getViewWindowId(), false); });
  connect(split, &ViewSplit2::currentViewWindowChanged, dw, [dw](ViewWindow2 *w) {
    dw->setWindowTitle(w ? w->getTitle() : qApp->applicationDisplayName());
  });

  dw->show();
}

bool ViewArea2::moveViewWindowToDetached(ID p_windowId, const QString &p_srcWorkspaceId,
                                         const QString &p_detachedWorkspaceId) {
  auto *win = windowForId(p_windowId);
  auto *srcSplit = owningSplitForWindow(p_windowId);
  auto *dstSplit = hostedSplitForWorkspace(p_detachedWorkspaceId);
  if (srcSplit && (srcSplit->isPreview() || srcSplit->getWorkspaceId() != p_srcWorkspaceId)) {
    return false;
  }
  if (dstSplit && dstSplit->isPreview()) {
    return false;
  }
  if (!win || !srcSplit || !dstSplit) {
    // Transfer cannot proceed. Destroy the empty detached-window shell so the
    // controller can roll back the just-created workspace cleanly.
    for (auto *dw : m_detachedWindows) {
      if (dw->getWorkspaceId() == p_detachedWorkspaceId) {
        m_detachedWindows.removeAll(dw);
        dw->deleteLater();
        break;
      }
    }
    return false;
  }

  srcSplit->takeViewWindow(win);
  dstSplit->addViewWindow(win);
  dstSplit->setCurrentViewWindow(win);

  // The source split may now be empty (e.g. detaching its only tab); refresh so
  // the Home dashboard is opened if the main area became empty.
  updateScreenVisibility();

  // Let source cleanup, Home creation and the tab context menu finish before taking focus.
  QTimer::singleShot(0, dstSplit, [dstSplit]() {
    auto *window = dstSplit->window();
    window->raise();
    window->activateWindow();
    dstSplit->focus();
  });
  return true;
}

void ViewArea2::onCloseDetachedWindow(DetachedWindow *p_window) {
  if (!p_window || !m_detachedWindows.contains(p_window)) {
    return;
  }
  auto *split = p_window->getViewSplit();
  if (!split) {
    return;
  }

  QPointer<ViewArea2> area(this);
  QPointer<DetachedWindow> window(p_window);
  QVector<ID> windowIds;
  windowIds.reserve(split->count());
  for (int idx = 0; idx < split->count(); ++idx) {
    if (auto *win = qobject_cast<ViewWindow2 *>(split->widget(idx))) {
      windowIds.append(win->getViewWindowId());
    }
  }

  // Prompts can run nested event loops: retain IDs, not hosted view pointers.
  for (ID id : windowIds) {
    if (!area || !window || !m_detachedWindows.contains(window.data())) {
      return;
    }
    auto *win = windowForId(id);
    auto *hostedSplit = window->getViewSplit();
    if (!win || !hostedSplit || hostedSplit->indexOf(win) == -1) {
      continue;
    }
    const bool closed = m_controller->closeViewWindow(id, /*p_force=*/false);
    if (!area || !window || !closed) {
      return;
    }
  }
}

void ViewArea2::reattachDetachedWindow(DetachedWindow *p_window) {
  if (!p_window || !m_detachedWindows.contains(p_window)) {
    return;
  }

  auto *detSplit = p_window->getViewSplit();
  const QString detWsId = p_window->getWorkspaceId();

  // Resolve a main split to receive the windows.
  ViewSplit2 *target = getCurrentViewSplit();
  if (!target && !m_splits.isEmpty()) {
    target = m_splits.first();
  }

  // No main split exists to receive the windows. Rather than dropping them
  // (which would orphan their buffers), abort the reattach and keep the detached
  // window alive/visible so no data is lost. This is an extreme edge case since
  // the main area normally always hosts at least the Home dashboard.
  if (!target) {
    qWarning() << "ViewArea2::reattachDetachedWindow: no main split to reattach into; "
                  "keeping detached window open";
    p_window->show();
    return;
  }

  QStringList bufferIds;
  ViewWindow2 *lastWin = nullptr;
  if (detSplit) {
    const auto windows = detSplit->getAllViewWindows();
    for (auto *win : windows) {
      bufferIds.append(win->getBuffer().id());
      detSplit->takeViewWindow(win);
      target->addViewWindow(win);
      lastWin = win;
    }
  }
  if (lastWin) {
    target->setCurrentViewWindow(lastWin);
  }

  m_detachedWindows.removeAll(p_window);

  m_controller->reattachDetachedWorkspace(detWsId, target->getWorkspaceId(), bufferIds);

  m_controller->setCurrentViewSplit(target->getWorkspaceId(), true);

  p_window->deleteLater();
  updateScreenVisibility();
}

void ViewArea2::reattachAllDetachedWindows() {
  const auto detached = m_detachedWindows; // copy: reattach mutates m_detachedWindows
  for (auto *dw : detached) {
    reattachDetachedWindow(dw);
  }
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
  //
  // Deliberately do NOT call updateScreenVisibility() here. At this point the
  // split(s) just built from the saved layout are still EMPTY — restoreSession()
  // (fired later from MainWindowAfterStart) has not yet reopened their buffers.
  // Calling updateScreenVisibility() now sees m_windows empty and arms
  // maybeOpenHome(), which races the restore and can surface an unwanted Home
  // dashboard tab alongside the restored files. The "nothing was restored, open
  // Home" decision is made post-restore in setupController()'s
  // MainWindowAfterStart handler, which checks emptiness AFTER restoreSession().
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

  qCDebug(lcWorkspace) << "ViewArea2::takeViewWindowsFromSplit: workspace:" << p_workspaceId
                       << "windowCount:" << split->getViewWindowCount()
                       << "propagateToCore:" << m_controller->shouldPropagateToCore();

  // Sync tab order and current buffer to vxcore before hiding.
  if (m_controller->shouldPropagateToCore() && split->getViewWindowCount() > 0) {
    auto *wsSvc = m_services.get<WorkspaceCoreService>();
    if (wsSvc) {
      QStringList bufferIds;
      auto *bufferSvc = m_services.get<BufferService>();
      for (auto *win : split->getAllViewWindows()) {
        const QString bufferId = win->getBuffer().id();
        bufferIds.append(bufferId);
        if (!bufferSvc || !bufferSvc->isVirtualBuffer(bufferId)) {
          // Hidden workspaces are not visited by saveSession(). Preserve their
          // existing view metadata while recording the shared buffer state.
          auto metadata = wsSvc->getBufferMetadata(p_workspaceId, bufferId);
          metadata[QLatin1String(vxcore::kJsonKeyReadOnly)] = win->getBuffer().isReadOnly();
          wsSvc->setBufferMetadata(p_workspaceId, bufferId, metadata);
        }
      }
      qCDebug(lcWorkspace) << "  Syncing buffer order before hide:" << bufferIds;
      wsSvc->setBufferOrder(p_workspaceId, bufferIds);

      // Also sync the current buffer so it's correct if the workspace
      // is restored from vxcore state (fresh workspace without cached windows).
      auto *currentWin = split->getCurrentViewWindow();
      if (currentWin) {
        QString curBufId = currentWin->getBuffer().id();
        qCDebug(lcWorkspace) << "  Syncing current buffer before hide:" << curBufId;
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
    qCDebug(lcWorkspace) << "  Taking window for buffer:" << win->getBuffer().id();
    split->takeViewWindow(win); // removeTab + disconnect + setVisible(false) + setParent(nullptr)
    result.append(win);
  }
  qCDebug(lcWorkspace) << "  Took" << result.size() << "windows from split";
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
  auto *split = hostedSplitForWorkspace(p_workspaceId);
  const bool detached = split && !split->isPreview() && !splitForWorkspace(p_workspaceId);
  if (!split) {
    qWarning() << "ViewArea2::setCurrentBuffer: no split for workspace:" << p_workspaceId;
    return;
  }

  qCDebug(lcWorkspace) << "ViewArea2::setCurrentBuffer: workspace:" << p_workspaceId
                       << "buffer:" << p_bufferId << "focus:" << p_focus
                       << "currentWidget:" << split->currentWidget();

  if (split->isPreview() && p_focus) {
    // A partial promotion may have rebound the overlay to a noncurrent tab.
    // Reveal that source before focusing the remaining preview, without reopening it.
    const auto host = m_previewHosts.constFind(p_workspaceId);
    if (host != m_previewHosts.constEnd() && host->source) {
      const QPointer<ViewArea2> guard(this);
      const QPointer<ViewSplit2> preview(split);
      if (auto *owner = owningSplitForWindow(host->sourceId);
          owner && !owner->isPreview() && owner->getCurrentViewWindow() != host->source) {
        owner->setCurrentViewWindow(host->source);
      }
      if (!guard || !preview) {
        return;
      }
    }
  }

  // Find the ViewWindow2 with the matching buffer ID.
  auto windows = split->getAllViewWindows();
  for (auto *win : windows) {
    if (win->getBuffer().id() == p_bufferId) {
      qCDebug(lcWorkspace) << "ViewArea2::setCurrentBuffer: found win:" << win
                           << "calling setCurrentViewWindow";
      split->setCurrentViewWindow(win);
      qCDebug(lcWorkspace)
          << "ViewArea2::setCurrentBuffer: after setCurrentViewWindow, currentWidget:"
          << split->currentWidget();
      if (p_focus) {
        if (detached) {
          auto *host = split->window();
          if (host->isMinimized()) {
            host->showNormal();
          }
          host->raise();
          host->activateWindow();
        }
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
  auto *split = hostedSplitForWorkspace(p_workspaceId);
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
  auto *split = hostedSplitForWorkspace(p_workspaceId);
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

QVector<ID> ViewArea2::findWindowIdsByNode(const NodeIdentifier &p_nodeId, bool p_isFolder) const {
  QVector<ID> result;
  const QString folderPrefix = p_isFolder ? (p_nodeId.relativePath + QLatin1Char('/')) : QString();
  for (auto it = m_windows.constBegin(); it != m_windows.constEnd(); ++it) {
    auto *win = it.value();
    if (!win)
      continue;
    const auto &id = win->getNodeId();
    if (id.notebookId != p_nodeId.notebookId)
      continue;
    if (p_isFolder) {
      if (id.relativePath.startsWith(folderPrefix))
        result.append(it.key());
    } else {
      if (id.relativePath == p_nodeId.relativePath)
        result.append(it.key());
    }
  }
  return result;
}

QString ViewArea2::getCurrentBufferIdForWorkspace(const QString &p_workspaceId) const {
  auto *split = hostedSplitForWorkspace(p_workspaceId);
  if (!split) {
    return QString();
  }

  auto *win = split->getCurrentViewWindow();
  if (!win) {
    return QString();
  }

  return win->getBuffer().id();
}

QVector<ViewWindowNavInfo> ViewArea2::getViewWindowNavInfos(const QString &p_workspaceId) const {
  QVector<ViewWindowNavInfo> infos;
  auto *split = hostedSplitForWorkspace(p_workspaceId);
  if (!split) {
    return infos;
  }

  const auto windows = split->getAllViewWindows();
  for (auto *win : windows) {
    if (!win) {
      continue;
    }
    ViewWindowNavInfo info;
    info.windowId = win->getViewWindowId();
    info.bufferId = win->getBuffer().id();
    info.name = win->getName();
    info.title = win->getTitle();
    info.icon = win->getIcon();
    infos.append(info);
  }
  return infos;
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

QJsonObject ViewArea2::serializeWidget(const QWidget *p_widget) const {
  QJsonObject node;
  auto *splitter = qobject_cast<const QSplitter *>(p_widget);
  if (splitter) {
    QJsonArray children;
    for (int i = 0; i < splitter->count(); ++i) {
      QJsonObject child = serializeWidget(splitter->widget(i));
      // Drop pruned (virtual-only) subtrees so they are not restored blank.
      if (!child.isEmpty()) {
        children.append(child);
      }
    }
    // Collapse: an empty splitter contributes nothing; a single surviving child
    // replaces the splitter node entirely.
    if (children.isEmpty()) {
      return QJsonObject();
    }
    if (children.size() == 1) {
      return children.first().toObject();
    }
    node[QStringLiteral("type")] = QStringLiteral("splitter");
    node[QStringLiteral("orientation")] = (splitter->orientation() == Qt::Horizontal)
                                              ? QStringLiteral("horizontal")
                                              : QStringLiteral("vertical");
    QJsonArray sizes;
    for (int s : splitter->sizes()) {
      sizes.append(s);
    }
    node[QStringLiteral("sizes")] = sizes;
    node[QStringLiteral("children")] = children;
  } else {
    auto *split = qobject_cast<const ViewSplit2 *>(p_widget);
    if (split) {
      // Prune workspace nodes whose only windows are virtual (e.g. vx://home):
      // their buffers are not restored, so persisting them yields a blank split.
      if (!splitHasRestorableWindows(split)) {
        return QJsonObject();
      }
      node[QStringLiteral("type")] = QStringLiteral("workspace");
      node[QStringLiteral("workspaceId")] = split->getWorkspaceId();
    }
  }
  return node;
}

bool ViewArea2::splitHasRestorableWindows(const ViewSplit2 *p_split) const {
  if (!p_split) {
    return false;
  }
  auto *bufferSvc = m_services.get<BufferService>();
  const auto windows = p_split->getAllViewWindows();
  for (auto *win : windows) {
    if (win && (!bufferSvc || !bufferSvc->isVirtualBuffer(win->getBuffer().id()))) {
      return true;
    }
  }
  return false;
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

void ViewArea2::updateScreenVisibility() {
  if (isMainAreaEmpty()) {
    maybeOpenHome();
  }
}

bool ViewArea2::isMainAreaEmpty() const {
  for (auto it = m_splits.constBegin(); it != m_splits.constEnd(); ++it) {
    if (it.value() && it.value()->getViewWindowCount() > 0) {
      return false;
    }
  }
  return true;
}

ID ViewArea2::loneHomeWindowId() const {
  ViewWindow2 *only = nullptr;
  int total = 0;
  for (auto it = m_splits.constBegin(); it != m_splits.constEnd(); ++it) {
    auto *split = it.value();
    if (!split) {
      continue;
    }
    const auto wins = split->getAllViewWindows();
    total += wins.size();
    if (total > 1) {
      return 0;
    }
    if (!wins.isEmpty()) {
      only = wins.first();
    }
  }
  if (total == 1 && only &&
      only->getBuffer().nodeId().relativePath == QStringLiteral("vx://home")) {
    return only->getViewWindowId();
  }
  return 0;
}

void ViewArea2::maybeOpenHome() {
  if (m_openingHome || !isMainAreaEmpty()) {
    return;
  }
  m_openingHome = true;
  // Defer so the current close/delete stack (win->deleteLater()) unwinds first,
  // and so opening home during teardown doesn't re-enter close paths. Dedup by
  // virtualAddress() in ViewAreaController prevents duplicate home tabs.
  QTimer::singleShot(0, this, [this]() {
    m_openingHome = false;
    if (isMainAreaEmpty() && m_controller) {
      m_controller->openVxUrl(QUrl(QStringLiteral("vx://home")));
    }
  });
}
