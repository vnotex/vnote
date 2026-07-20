#include "notebookexplorer2.h"

#include <QActionGroup>
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <controllers/newnotebookcontroller.h>
#include <controllers/notebooknodecontroller.h>
#include <controllers/opennotebookcontroller.h>
#include <controllers/recyclebincontroller.h>
#include <core/configmgr2.h>
#include <core/exception.h>
#include <core/fileopensettings.h>
#include <core/global.h>
#include <core/hookevents.h>
#include <core/hooknames.h>
#include <core/logging.h>
#include <core/nodeinfo.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <core/services/hookmanager.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/synccredentialsstore.h>
#include <core/services/synclog.h>
#include <core/services/syncservice.h>
#include <core/services/syncstateclassifier.h>
#include <core/sessionconfig.h>
#include <core/widgetconfig.h>
#include <gui/services/navigationmodeservice.h>
#include <gui/services/themeservice.h>
#include <utils/fileutils2.h>
#include <utils/notebookpathhelpers.h>
#include <utils/widgetutils.h>
#include <views/combinednodeexplorer.h>
#include <views/inodeexplorer.h>
#include <views/twocolumnsnodeexplorer.h>
#include <vxcore/notebook_json_keys.h>
// TODO: Migrate dialogs to use ServiceLocator DI pattern
#include <core/services/templateservice.h>
#include <core/services/snippetcoreservice.h>
#include <widgets/dialogs/importfolderdialog2.h>
#include <widgets/dialogs/managenotebooksdialog2.h>
#include <widgets/dialogs/marknodedialog2.h>
#include <widgets/dialogs/newfolderdialog2.h>
#include <widgets/dialogs/newnotebookdialog2.h>
#include <widgets/dialogs/newnotedialog2.h>
#include <widgets/dialogs/nodepropertiesdialog2.h>
#include <widgets/dialogs/notebooksyncinfodialog2.h>
#include <widgets/dialogs/opennotebookdialog2.h>
#include <widgets/dialogs/openvnote3notebookdialog2.h>
#include <widgets/dialogs/selectdialog.h>
#include <widgets/dialogs/sortdialog2.h>
#include <widgets/dialogs/viewtagsdialog2.h>
#include <widgets/messageboxhelper.h>
#include <widgets/notebookexplorer2_sortseam.h>
#include <widgets/notebookselector2.h>
#include <widgets/titlebar.h>

using namespace vnotex;

namespace {

const quint32 c_sessionVersion = 2;

bool isNodeExplorerStateEmpty(const NodeExplorerState &p_state) {
  return p_state.expandedFolders.isEmpty() && !p_state.currentNodeId.isValid() &&
         !p_state.displayRootId.isValid();
}

NodeExplorerState mergeNodeExplorerStateForCache(const NodeExplorerState &p_capturedState,
                                                 const NodeExplorerState &p_existingState) {
  if (isNodeExplorerStateEmpty(p_capturedState) && !isNodeExplorerStateEmpty(p_existingState)) {
    return p_existingState;
  }

  NodeExplorerState mergedState = p_capturedState;
  if (!mergedState.displayRootId.isValid() && p_existingState.displayRootId.isValid()) {
    mergedState.displayRootId = p_existingState.displayRootId;
  }

  return mergedState;
}

} // namespace

NotebookExplorer2::NotebookExplorer2(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  setupUI();
  setupFileWatcher();

  // Subscribe to NotebookAfterClose to refresh the explorer.
  auto *hookMgr = m_services.get<HookManager>();
  if (hookMgr) {
    hookMgr->addAction<NotebookCloseEvent>(
        HookNames::NotebookAfterClose,
        [this](HookContext &p_ctx, const NotebookCloseEvent &) {
          Q_UNUSED(p_ctx)
          loadNotebooks();
        },
        10);
  }

  // Sync UI lifecycle wiring (T15). Refresh the Sync button + menu entry on
  // any sync state transition for ANY notebook (the slot filters by current).
  if (auto *syncSvc = m_services.get<SyncService>()) {
    connect(syncSvc, &SyncService::syncStarted, this, [this](const QString &p_notebookId) {
      // Maintain active-sync fs-event suppression: only suppress on
      // the currently displayed notebook (a sync on a different
      // notebook MUST NOT swallow fs events on the displayed one).
      if (p_notebookId == m_currentNotebookId) {
        m_activeSyncFsSuppression.insert(p_notebookId);
      }
      // A new sync resets the grace clock; stop any in-flight grace
      // timer so its timeout doesn't later wipe state added by a
      // newer sync.
      if (m_syncGraceTimer) {
        m_syncGraceTimer->stop();
      }
      updateSyncButtonState();
    });
    connect(syncSvc, &SyncService::syncFinished, this,
            [this](const QString &p_notebookId, VxCoreError p_result) {
              // Clear reconcile error on successful sync (sync supersedes reconcile)
              m_lastReconcileError.remove(p_notebookId);
              if (p_result == VXCORE_OK) {
                // Reset failure-popup suppression so the NEXT failure (if any)
                // surfaces again.
                m_authFailureNotified.remove(p_notebookId);
                m_networkFailureNotified.remove(p_notebookId);
                // Drop any pending credential-retry arm — sync clearly works now.
                m_credentialUpdateRetryArm.remove(p_notebookId);
                // Manual Sync Now success confirmation: only fires when the user
                // explicitly invoked Sync Now (or T3 auto-retry tagged it). Auto-
                // sync ticks do NOT push status messages.
                if (m_pendingManualSyncFeedback.remove(p_notebookId)) {
                  // Note: success feedback to user is currently log-only.
                  // MainWindow2 deliberately has no status bar; VNoteX::getInst()
                  // is legacy and triggers throwing ThemeMgr init from this
                  // code path (crash). A future MainWindow2-native status
                  // surface would be the right place to show "Sync complete".
                  qCInfo(lcSync) << "Manual sync completed successfully for notebook"
                                 << p_notebookId;
                }

                // T4 (sync-in-progress-ux): consume any deferred credential-retry
                // for this notebook. Placed AFTER the existing remove-and-show so
                // the insert below tags the TRAILING sync (not the just-finished
                // one). Order matters.
                if (m_deferredCredentialRetry.remove(p_notebookId)) {
                  if (auto *svc = m_services.get<SyncService>()) {
                    m_pendingManualSyncFeedback.insert(p_notebookId);
                    qCInfo(lcSync) << "NotebookExplorer2: consuming deferred credential retry "
                                      "after sync completion"
                                   << "notebookId:" << p_notebookId;
                    svc->triggerSyncNow(p_notebookId);
                  }
                }
              } else {
                // Failed manual sync: drop the pending-feedback tag so we don't
                // double-notify (the failure dialog already provides feedback).
                m_pendingManualSyncFeedback.remove(p_notebookId);
              }
              updateSyncButtonState();
              // Start grace timer to keep fs-event suppression active for
              // 2 s after syncFinished, so late deliveries from the post-
              // stage network rebase are still swallowed. Only arm when the
              // finished notebook is the currently displayed one AND its
              // suppression is currently active.
              if (p_notebookId == m_currentNotebookId &&
                  m_activeSyncFsSuppression.contains(p_notebookId) && m_syncGraceTimer) {
                m_syncGraceTimer->start();
              }
            });
    connect(syncSvc, &SyncService::syncFailed, this, &NotebookExplorer2::onSyncFailedSurface);
    connect(syncSvc, &SyncService::enableFinished, this,
            [this](const QString &p_notebookId, VxCoreError p_result, const QString &) {
              if (p_result == VXCORE_OK) {
                // An explicit user-driven enable succeeded — give failure popups a
                // fresh chance on the next sync attempt.
                m_authFailureNotified.remove(p_notebookId);
                m_networkFailureNotified.remove(p_notebookId);
              }
              updateSyncButtonState();
            });
    connect(syncSvc, &SyncService::credentialsSetFinished, this,
            [this](const QString &p_notebookId, VxCoreError p_result) {
              if (p_result == VXCORE_OK) {
                // User supplied new credentials successfully. Reset suppression
                // so any next failure surfaces. If we had previously shown an
                // auth-failure dialog for this notebook (m_credentialUpdateRetryArm
                // set), immediately re-trigger a sync so the user sees the
                // verdict of their new PAT WITHOUT an extra Sync Now click; tag
                // it for manual feedback so success surfaces via the status bar
                // and failure surfaces via onSyncFailedSurface as usual.
                m_authFailureNotified.remove(p_notebookId);
                m_networkFailureNotified.remove(p_notebookId);
                if (m_credentialUpdateRetryArm.remove(p_notebookId)) {
                  if (auto *svc = m_services.get<SyncService>()) {
                    if (svc->isSyncInProgress(p_notebookId)) {
                      // Sync is currently running; defer ONE retry until it completes
                      // (consumed in syncFinished(OK) lambda). User still sees verdict
                      // of the new PAT, just one sync cycle later. Idempotent QSet
                      // insert prevents stacking.
                      m_deferredCredentialRetry.insert(p_notebookId);
                      qCInfo(lcSync) << "NotebookExplorer2: credentialsSetFinished OK while sync "
                                        "in flight; deferring retry"
                                     << "notebookId:" << p_notebookId;
                    } else {
                      m_pendingManualSyncFeedback.insert(p_notebookId);
                      qCDebug(lcSync)
                          << "NotebookExplorer2: credentialsSetFinished OK with armed retry; "
                             "triggering sync"
                          << "notebookId:" << p_notebookId;
                      svc->triggerSyncNow(p_notebookId);
                    }
                  }
                }
              }
              updateSyncButtonState();
            });
    connect(syncSvc, &SyncService::disableFinished, this,
            [this](const QString &p_notebookId, VxCoreError) {
              m_authFailureNotified.remove(p_notebookId);
              m_networkFailureNotified.remove(p_notebookId);
              m_credentialUpdateRetryArm.remove(p_notebookId);
              m_deferredCredentialRetry.remove(p_notebookId);
              m_pendingManualSyncFeedback.remove(p_notebookId);
              // Drop fs-event suppression for this notebook and stop any
              // pending grace timer so it doesn't fire after disable.
              m_activeSyncFsSuppression.remove(p_notebookId);
              if (m_syncGraceTimer) {
                m_syncGraceTimer->stop();
              }
              updateSyncButtonState();
            });
    // Drop fs-event suppression on syncCancelled so a cancel that races with
    // the grace-window does not leave the set stale. Handles both wasQueued
    // (pending-cancel) and in-flight cancel cases identically.
    connect(syncSvc, &SyncService::syncCancelled, this, [this](const QString &p_notebookId, bool) {
      m_activeSyncFsSuppression.remove(p_notebookId);
      if (m_syncGraceTimer) {
        m_syncGraceTimer->stop();
      }
    });
    // Wire reconcileFinished to store error code and update button state (W4.T3)
    connect(syncSvc, &SyncService::reconcileFinished, this,
            [this](const QString &p_notebookId, VxCoreError p_result) {
              if (p_result != VXCORE_OK) {
                m_lastReconcileError[p_notebookId] = static_cast<int>(p_result);
                qCWarning(lcSync) << "reconcile error for notebookId=" << p_notebookId
                                  << "result=" << p_result;
              } else {
                m_lastReconcileError.remove(p_notebookId);
              }
              // Auto-prompt-on-open (sync-info-on-open): if this notebook was
              // just interactively opened and its sync config is complete on
              // disk but the PAT is missing from the keychain (state S2,
              // surfaced here as VXCORE_ERR_SYNC_AUTH_FAILED after the async
              // credential lookup), pop the Sync Info dialog in bootstrap mode
              // so the user can supply the PAT. We remove the id on the FIRST
              // reconcileFinished for it (any result) so a stale entry can never
              // mis-fire a later prompt.
              const bool wasPendingOpen = m_pendingOpenSyncPrompt.remove(p_notebookId);
              if (wasPendingOpen && p_result == VXCORE_ERR_SYNC_AUTH_FAILED) {
                maybePromptSyncInfoForMissingPat(p_notebookId);
              }
              updateSyncButtonState();
            });
  }
  connect(this, &NotebookExplorer2::currentNotebookChanged, this, [this](const QString &) {
    // Reset all per-notebook UI feedback state on notebook switch.
    m_authFailureNotified.clear();
    m_networkFailureNotified.clear();
    m_credentialUpdateRetryArm.clear();
    m_deferredCredentialRetry.clear();
    m_pendingManualSyncFeedback.clear();
    // Drop stale active-sync fs-event suppression from the previously
    // displayed notebook AND stop any pending grace timer (otherwise its
    // delayed timeout would wipe state that belongs to the new notebook's
    // freshly-armed suppression).
    m_activeSyncFsSuppression.clear();
    if (m_syncGraceTimer) {
      m_syncGraceTimer->stop();
    }
    updateSyncButtonState();
  });

  // Grace timer: keeps active-sync fs-event suppression armed for 2 s after
  // syncFinished so late deliveries from the post-stage network rebase are
  // still swallowed (the existing one-shot expectFsChange would have fired
  // by then).
  m_syncGraceTimer = new QTimer(this);
  m_syncGraceTimer->setSingleShot(true);
  m_syncGraceTimer->setInterval(2000);
  connect(m_syncGraceTimer, &QTimer::timeout, this, [this]() {
    // Drop suppression for the currently displayed notebook. The grace timer
    // is only ever (re)started for the current notebook (see syncFinished
    // lambda), so clearing on the current id is the correct scope.
    if (!m_currentNotebookId.isEmpty()) {
      m_activeSyncFsSuppression.remove(m_currentNotebookId);
    }
  });

  // Subscribe to FileBeforeSave so user-initiated saves arm an expectFsChange
  // for the buffer's parent directory; the directoryChanged event Qt fires
  // after the file is rewritten would otherwise reload the explorer and wipe
  // selection. Synchronous subscription on the UI thread is REQUIRED for the
  // arm to happen BEFORE the disk write — do not switch to QueuedConnection.
  if (auto *hookMgr = m_services.get<HookManager>()) {
    m_fileBeforeSaveHookId = hookMgr->addAction<BufferEvent>(
        HookNames::FileBeforeSave,
        [this](HookContext &p_ctx, const BufferEvent &p_event) {
          Q_UNUSED(p_ctx) // observe-only — do NOT call p_ctx.cancel().
          const QString parentAbs =
              resolveExpectFsChangePathForBuffer(m_services, p_event.bufferId, m_currentNotebookId);
          if (!parentAbs.isEmpty()) {
            // Default 3000 ms window matches the hooked-mutator pattern at
            // line 1357 et al.
            expectFsChange(parentAbs);
          }
        },
        /*priority=*/10);
  }

  // Initial state.
  updateSyncButtonState();
}

NotebookExplorer2::~NotebookExplorer2() {
  if (m_fileBeforeSaveHookId >= 0) {
    if (auto *hookMgr = m_services.get<HookManager>()) {
      hookMgr->removeAction(m_fileBeforeSaveHookId);
    }
    m_fileBeforeSaveHookId = -1;
  }
}

void NotebookExplorer2::setupUI() {
  m_mainLayout = new QVBoxLayout(this);
  m_mainLayout->setContentsMargins(0, 0, 0, 0);
  m_mainLayout->setSpacing(0);

  // Title bar with actions and menu
  setupTitleBar();
  m_mainLayout->addWidget(m_titleBar);

  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();

  // Notebook selector
  m_notebookSelector = new NotebookSelector2(m_services, this);
  m_notebookSelector->setWhatsThis(tr("Select one of all the notebooks as current notebook.<br/>"
                                      "Move mouse on one item to check its details."));
  m_notebookSelector->setViewOrder(widgetConfig.getNotebookSelectorViewOrder());
  m_mainLayout->addWidget(m_notebookSelector);

  // T26: Read-only badge label. Hidden by default; visibility is driven by
  // setCurrentNotebookInternal() based on NotebookCoreService::isNotebookReadOnly.
  // Lives directly under the selector so users see the lock + reason inline
  // with the active notebook name. Object name is exported for test discovery
  // and themed via QSS (objectName == "readOnlyBadgeLabel").
  m_readOnlyBadgeLabel = new QLabel(this);
  m_readOnlyBadgeLabel->setObjectName(QStringLiteral("readOnlyBadgeLabel"));
  m_readOnlyBadgeLabel->setContentsMargins(6, 2, 6, 2);
  m_readOnlyBadgeLabel->setTextFormat(Qt::RichText);
  m_readOnlyBadgeLabel->setToolTip(tr("Read-only notebook (no PAT)"));
  m_readOnlyBadgeLabel->setStyleSheet(QStringLiteral("QLabel { font-style: italic; }"));
  m_readOnlyBadgeLabel->hide();
  m_mainLayout->addWidget(m_readOnlyBadgeLabel);

  // Get initial explore mode from config (0=Combined, 1=TwoColumns)
  int mode = m_services.get<ConfigMgr2>()->getWidgetConfig().getNodeExplorerExploreMode();
  m_exploreMode = (mode == TwoColumns) ? TwoColumns : Combined;

  // Create the initial explorer
  if (m_exploreMode == Combined) {
    setupCombinedMode();
  } else {
    setupTwoColumnsMode();
  }

  // Connect notebook selector - activated fires on user interaction only
  // Note: We don't use currentIndexChanged because it fires during addItem() before item data is
  // set
  connect(m_notebookSelector, QOverload<int>::of(&QComboBox::activated), this, [this](int p_idx) {
    QString guid = m_notebookSelector->itemData(p_idx, NotebookGuidRole).toString();
    setCurrentNotebookInternal(guid);
  });
  connect(m_notebookSelector, &NotebookSelector2::newNotebookRequested, this,
          &NotebookExplorer2::newNotebook);

  updateFocusProxy();
}

void NotebookExplorer2::setupTitleBar() {
  m_titleBar =
      new TitleBar(m_services.get<ThemeService>(), QString(), false, TitleBar::Action::Menu, this);
  m_titleBar->setWhatsThis(
      tr("This title bar contains buttons and menu to manage notebooks and notes."));
  m_titleBar->setActionButtonsAlwaysShown(true);

  // New Notebook button
  {
    auto btn = m_titleBar->addActionButton("add.svg", tr("New Notebook"));
    connect(btn, &QToolButton::clicked, this, &NotebookExplorer2::newNotebook);
  }

  // Open/Import Notebook button
  {
    auto btn = m_titleBar->addActionButton("open_notebook.svg", tr("Open Notebook"));
    connect(btn, &QToolButton::clicked, this, &NotebookExplorer2::importNotebook);
  }

  // Open VNote3 Notebook button
  {
    auto btn = m_titleBar->addActionButton("open_legacy_notebook.svg", tr("Open VNote3 Notebook"));
    connect(btn, &QToolButton::clicked, this, &NotebookExplorer2::openVNote3Notebook);
  }

  // Sync button (T15) — always visible.
  // Enabled state and tooltip are driven by updateSyncButtonState().
  m_syncButton = m_titleBar->addActionButton(QStringLiteral("sync_now.svg"), tr("Sync"));
  m_syncButton->setEnabled(false);
  connect(m_syncButton, &QToolButton::clicked, this, &NotebookExplorer2::onSyncButtonClicked);

  setupTitleBarMenu();
}

void NotebookExplorer2::setupTitleBarMenu() {
  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();

  m_titleBar->addMenuAction(tr("Manage Notebooks"), m_titleBar, [this]() { manageNotebooks(); });

  m_titleBar->addMenuAction(tr("Open Folder as &Raw Notebook"), m_titleBar,
                            [this]() { newNotebookFromFolder(); });

  m_titleBar->addMenuSeparator();

  // "Notebook Sync Info..." entry (T15). Enabled only when current notebook is
  // bundled AND has sync configured; managed by updateSyncButtonState().
  m_syncInfoAction = m_titleBar->addMenuAction(tr("Sync Info"), m_titleBar,
                                               [this]() { onSyncInfoActionTriggered(); });
  m_syncInfoAction->setEnabled(false);

  m_rebuildDatabaseAction = m_titleBar->addMenuAction(tr("Rebuild Database"), m_titleBar,
                                                      [this]() { rebuildDatabase(); });

  setupRecycleBinMenu();

  {
    m_titleBar->addMenuSeparator();
    auto notebookMenu = m_titleBar->addMenuSubMenu(tr("Notebooks View Order"));
    setupViewMenu(notebookMenu, true);

    auto nodeMenu = m_titleBar->addMenuSubMenu(tr("Notes View Order"));
    setupViewMenu(nodeMenu, false);
  }

  // External files options
  {
    m_titleBar->addMenuSeparator();
    auto showAct =
        m_titleBar->addMenuAction(tr("Show External Files"), m_titleBar, [this](bool p_checked) {
          m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerExternalFilesVisible(
              p_checked);
          // Apply to current explorer
          if (m_nodeExplorer) {
            cacheCurrentExplorerState();
            m_nodeExplorer->setExternalNodesVisible(p_checked);
            applyCachedExplorerState(m_currentNotebookId);
          }
        });
    showAct->setCheckable(true);
    showAct->setChecked(widgetConfig.isNodeExplorerExternalFilesVisible());

    auto importAct = m_titleBar->addMenuAction(
        tr("Import External Files on Open"), m_titleBar, [this](bool p_checked) {
          m_services.get<ConfigMgr2>()
              ->getWidgetConfig()
              .setNodeExplorerAutoImportExternalFilesEnabled(p_checked);
        });
    importAct->setCheckable(true);
    importAct->setChecked(widgetConfig.getNodeExplorerAutoImportExternalFilesEnabled());
  }

  {
    m_titleBar->addMenuSeparator();
    auto act = m_titleBar->addMenuAction(
        tr("Close File Before External Open"), m_titleBar, [this](bool p_checked) {
          m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerCloseBeforeOpenWithEnabled(
              p_checked);
        });
    act->setCheckable(true);
    act->setChecked(widgetConfig.getNodeExplorerCloseBeforeOpenWithEnabled());
  }

  {
    m_titleBar->addMenuSeparator();
    auto act = m_titleBar->addMenuAction(
        tr("Single Click Activation"), m_titleBar, [this](bool p_checked) {
          m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerSingleClickActivation(
              p_checked);
        });
    act->setCheckable(true);
    act->setChecked(widgetConfig.isNodeExplorerSingleClickActivation());
  }

  setupExploreModeMenu();

  // Apply the initial enabled/disabled state for notebook-dependent menu
  // actions. At construction no notebook is active, so Rebuild Database and the
  // recycle bin actions start disabled until a notebook is selected.
  updateTitleBarMenuState();
}

void NotebookExplorer2::setupRecycleBinMenu() {
  m_openRecycleBinAction = m_titleBar->addMenuAction(tr("Open Recycle Bin"), this, [this]() {
    if (m_currentNotebookId.isEmpty()) {
      return;
    }

    RecycleBinController controller(m_services);
    RecycleBinResult result = controller.prepareRecycleBinPath(m_currentNotebookId);
    if (result.success) {
      WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(result.path));
    } else {
      MessageBoxHelper::notify(MessageBoxHelper::Information, result.errorMessage, window());
    }
  });

  m_emptyRecycleBinAction = m_titleBar->addMenuAction(tr("Empty Recycle Bin"), this, [this]() {
    if (m_currentNotebookId.isEmpty()) {
      return;
    }

    RecycleBinController controller(m_services);

    // Check if recycle bin is supported.
    QString recycleBinPath = controller.getRecycleBinPath(m_currentNotebookId);
    if (recycleBinPath.isEmpty()) {
      MessageBoxHelper::notify(MessageBoxHelper::Information,
                               tr("Recycle bin is not supported for this notebook type."),
                               window());
      return;
    }

    // Confirmation dialog.
    QString notebookName = controller.getNotebookName(m_currentNotebookId);
    if (notebookName.isEmpty()) {
      notebookName = tr("current notebook");
    }

    int ret = MessageBoxHelper::questionOkCancel(
        MessageBoxHelper::Question,
        tr("Are you sure you want to empty the recycle bin of notebook \"%1\"?\n\n"
           "This action is irreversible.")
            .arg(notebookName),
        QString(), QString(), window());

    if (ret != QMessageBox::Ok) {
      return;
    }

    RecycleBinResult result = controller.emptyRecycleBin(m_currentNotebookId);
    if (result.success) {
      MessageBoxHelper::notify(MessageBoxHelper::Information,
                               tr("Recycle bin emptied successfully."), window());
    } else {
      MessageBoxHelper::notify(MessageBoxHelper::Warning, result.errorMessage, window());
    }
  });
}

void NotebookExplorer2::updateTitleBarMenuState() {
  const bool hasNotebook = !m_currentNotebookId.isEmpty();

  // Rebuild Database requires an open notebook.
  if (m_rebuildDatabaseAction) {
    m_rebuildDatabaseAction->setEnabled(hasNotebook);
  }

  if (!m_openRecycleBinAction || !m_emptyRecycleBinAction) {
    return;
  }

  // Recycle bin actions require an open notebook whose type supports a recycle
  // bin (non-empty path). Empty Recycle Bin additionally requires the notebook
  // to be writable.
  bool recycleBinSupported = false;
  if (hasNotebook) {
    RecycleBinController controller(m_services);
    QString path = controller.getRecycleBinPath(m_currentNotebookId);
    recycleBinSupported = !path.isEmpty();
  }

  m_openRecycleBinAction->setEnabled(recycleBinSupported);
  m_emptyRecycleBinAction->setEnabled(recycleBinSupported && !isCurrentNotebookReadOnly());
}

void NotebookExplorer2::setupExploreModeMenu() {
  m_titleBar->addMenuSeparator();

  auto ag = new QActionGroup(m_titleBar);

  auto act = ag->addAction(tr("Combined View"));
  act->setCheckable(true);
  act->setData(static_cast<int>(ExploreMode::Combined));
  m_titleBar->addMenuAction(act);

  act = ag->addAction(tr("Separate View, Double Columns"));
  act->setCheckable(true);
  act->setData(static_cast<int>(ExploreMode::TwoColumns));
  m_titleBar->addMenuAction(act);

  int mode = m_services.get<ConfigMgr2>()->getWidgetConfig().getNodeExplorerExploreMode();

  for (const auto &action : ag->actions()) {
    if (action->data().toInt() == mode) {
      action->setChecked(true);
    }
  }

  connect(ag, &QActionGroup::triggered, this, [this](QAction *action) {
    int mode = action->data().toInt();
    m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerExploreMode(mode);
    setExploreMode(static_cast<ExploreMode>(mode));
  });
}

void NotebookExplorer2::setupViewMenu(QMenu *p_menu, bool p_isNotebookView) {
  auto ag = new QActionGroup(p_menu);

  auto act = ag->addAction(tr("View By Configuration"));
  act->setCheckable(true);
  act->setChecked(true);
  act->setData(ViewOrder::OrderedByConfiguration);
  p_menu->addAction(act);

  act = ag->addAction(tr("View By Name"));
  act->setCheckable(true);
  act->setData(ViewOrder::OrderedByName);
  p_menu->addAction(act);

  act = ag->addAction(tr("View By Name (Reversed)"));
  act->setCheckable(true);
  act->setData(ViewOrder::OrderedByNameReversed);
  p_menu->addAction(act);

  act = ag->addAction(tr("View By Created Time"));
  act->setCheckable(true);
  act->setData(ViewOrder::OrderedByCreatedTime);
  p_menu->addAction(act);

  act = ag->addAction(tr("View By Created Time (Reversed)"));
  act->setCheckable(true);
  act->setData(ViewOrder::OrderedByCreatedTimeReversed);
  p_menu->addAction(act);

  if (!p_isNotebookView) {
    act = ag->addAction(tr("View By Modified Time"));
    act->setCheckable(true);
    act->setData(ViewOrder::OrderedByModifiedTime);
    p_menu->addAction(act);

    act = ag->addAction(tr("View By Modified Time (Reversed)"));
    act->setCheckable(true);
    act->setData(ViewOrder::OrderedByModifiedTimeReversed);
    p_menu->addAction(act);
  }

  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();
  int viewOrder = p_isNotebookView ? widgetConfig.getNotebookSelectorViewOrder()
                                   : widgetConfig.getNodeExplorerViewOrder();
  for (const auto &action : ag->actions()) {
    if (action->data().toInt() == viewOrder) {
      action->setChecked(true);
    }
  }

  connect(ag, &QActionGroup::triggered, this, [this, p_isNotebookView](QAction *p_action) {
    const int order = p_action->data().toInt();
    if (p_isNotebookView) {
      m_services.get<ConfigMgr2>()->getWidgetConfig().setNotebookSelectorViewOrder(order);
      m_notebookSelector->setViewOrder(order);
    } else {
      m_services.get<ConfigMgr2>()->getWidgetConfig().setNodeExplorerViewOrder(order);
      setNodeViewOrder(static_cast<ViewOrder>(order));
    }
  });
}

void NotebookExplorer2::rebuildDatabase() {
  if (m_currentNotebookId.isEmpty()) {
    return;
  }

  auto &notebookService = *m_services.get<NotebookCoreService>();

  // Get notebook name for the dialog.
  QJsonObject config = notebookService.getNotebookConfig(m_currentNotebookId);
  QString notebookName = config.value(QLatin1String(vxcore::kJsonKeyName)).toString();
  if (notebookName.isEmpty()) {
    notebookName = tr("current notebook");
  }

  // Confirmation dialog.
  int ret = MessageBoxHelper::questionOkCancel(
      MessageBoxHelper::Question,
      tr("Are you sure you want to rebuild the database for notebook \"%1\"?\n\n"
         "This will re-scan all files and rebuild the metadata cache from the filesystem.")
          .arg(notebookName),
      QString(), QString(), window());
  if (ret != QMessageBox::Ok) {
    return;
  }

  // Show progress dialog (indeterminate for sync operation).
  QProgressDialog progress(tr("Rebuilding database for \"%1\"...").arg(notebookName),
                           QString(), // No cancel button for sync operation
                           0, 0,      // Indeterminate progress
                           window());
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0); // Show immediately
  progress.show();
  QCoreApplication::processEvents(); // Ensure dialog is displayed

  // Perform the rebuild (synchronous).
  bool success = notebookService.rebuildNotebookCache(m_currentNotebookId);

  progress.close();

  // Reload the current notebook to reflect changes.
  if (m_nodeExplorer) {
    m_nodeExplorer->setNotebookId(m_currentNotebookId);
  }

  if (success) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Database rebuilt successfully for \"%1\".").arg(notebookName),
                             window());
  } else {
    MessageBoxHelper::notify(MessageBoxHelper::Warning,
                             tr("Failed to rebuild database for \"%1\".").arg(notebookName),
                             window());
  }
}

void NotebookExplorer2::setupCombinedMode() {
  // Create encapsulated MVC widget for combined mode
  auto *explorer = new CombinedNodeExplorer(m_services, this);

  // Apply initial external files visibility from config
  const auto &widgetConfig = m_services.get<ConfigMgr2>()->getWidgetConfig();
  explorer->setExternalNodesVisible(widgetConfig.isNodeExplorerExternalFilesVisible());

  // Add to layout
  m_mainLayout->addWidget(explorer, 1);

  // Connect GUI request signals from CombinedNodeExplorer
  connect(explorer, &CombinedNodeExplorer::newNoteRequested, this,
          &NotebookExplorer2::onNewNoteRequested);
  connect(explorer, &CombinedNodeExplorer::newFolderRequested, this,
          &NotebookExplorer2::onNewFolderRequested);
  connect(explorer, &CombinedNodeExplorer::renameRequested, this,
          &NotebookExplorer2::onRenameRequested);
  connect(explorer, &CombinedNodeExplorer::deleteRequested, this,
          &NotebookExplorer2::onDeleteRequested);
  connect(explorer, &CombinedNodeExplorer::removeFromNotebookRequested, this,
          &NotebookExplorer2::onRemoveFromNotebookRequested);
  // T12 (missing-files-on-disk): batch prompt for indexed-but-missing nodes.
  connect(explorer, &CombinedNodeExplorer::missingNodeRemovalRequested, this,
          &NotebookExplorer2::onMissingNodeRemovalRequested);
  connect(explorer, &CombinedNodeExplorer::propertiesRequested, this,
          &NotebookExplorer2::onPropertiesRequested);
  connect(explorer, &CombinedNodeExplorer::errorOccurred, this,
          &NotebookExplorer2::onErrorOccurred);
  connect(explorer, &CombinedNodeExplorer::infoMessage, this, &NotebookExplorer2::onInfoMessage);
  connect(explorer, &CombinedNodeExplorer::nodeActivated, this,
          &NotebookExplorer2::onNodeActivated);
  connect(explorer, &CombinedNodeExplorer::exportNodeRequested, this,
          &NotebookExplorer2::exportNodeRequested);
  connect(explorer, &CombinedNodeExplorer::markRequested, this,
          &NotebookExplorer2::onMarkRequested);
  connect(explorer, &CombinedNodeExplorer::ignoreRequested, this,
          &NotebookExplorer2::onIgnoreRequested);
  connect(explorer, &CombinedNodeExplorer::manageTagsRequested, this,
          &NotebookExplorer2::onManageTagsRequested);
  // T11 (notebook-explorer-drag-reorder): SortDialog2 lives in NotebookExplorer2
  // because controllers MUST NOT show QDialog (src/controllers/AGENTS.md).
  connect(explorer, &CombinedNodeExplorer::sortRequested, this,
          &NotebookExplorer2::onSortRequested);

  // File system watcher: track expand/collapse
  connect(explorer, &INodeExplorer::folderExpanded, this, [this](const NodeIdentifier &p_folderId) {
    auto *nbService = m_services.get<NotebookCoreService>();
    if (!nbService)
      return;
    QString folderPath = nbService->buildAbsolutePath(m_currentNotebookId, p_folderId.relativePath);
    addWatchPath(folderPath);
  });
  connect(explorer, &INodeExplorer::folderCollapsed, this,
          [this](const NodeIdentifier &p_folderId) {
            auto *nbService = m_services.get<NotebookCoreService>();
            if (!nbService)
              return;
            QString folderPath =
                nbService->buildAbsolutePath(m_currentNotebookId, p_folderId.relativePath);
            removeWatchPath(folderPath);
          });

  m_nodeExplorer = explorer;
}

void NotebookExplorer2::setupTwoColumnsMode() {
  auto *explorer = new TwoColumnsNodeExplorer(m_services, this);

  // Add to layout
  m_mainLayout->addWidget(explorer, 1);

  // Connect GUI request signals
  connect(explorer, &TwoColumnsNodeExplorer::newNoteRequested, this,
          &NotebookExplorer2::onNewNoteRequested);
  connect(explorer, &TwoColumnsNodeExplorer::newFolderRequested, this,
          &NotebookExplorer2::onNewFolderRequested);
  connect(explorer, &TwoColumnsNodeExplorer::renameRequested, this,
          &NotebookExplorer2::onRenameRequested);
  connect(explorer, &TwoColumnsNodeExplorer::deleteRequested, this,
          &NotebookExplorer2::onDeleteRequested);
  connect(explorer, &TwoColumnsNodeExplorer::removeFromNotebookRequested, this,
          &NotebookExplorer2::onRemoveFromNotebookRequested);
  // T12 (missing-files-on-disk): batch prompt for indexed-but-missing nodes.
  connect(explorer, &TwoColumnsNodeExplorer::missingNodeRemovalRequested, this,
          &NotebookExplorer2::onMissingNodeRemovalRequested);
  connect(explorer, &TwoColumnsNodeExplorer::propertiesRequested, this,
          &NotebookExplorer2::onPropertiesRequested);
  connect(explorer, &TwoColumnsNodeExplorer::errorOccurred, this,
          &NotebookExplorer2::onErrorOccurred);
  connect(explorer, &TwoColumnsNodeExplorer::infoMessage, this, &NotebookExplorer2::onInfoMessage);
  connect(explorer, &TwoColumnsNodeExplorer::nodeActivated, this,
          &NotebookExplorer2::onNodeActivated);
  connect(explorer, &TwoColumnsNodeExplorer::exportNodeRequested, this,
          &NotebookExplorer2::exportNodeRequested);
  connect(explorer, &TwoColumnsNodeExplorer::markRequested, this,
          &NotebookExplorer2::onMarkRequested);
  connect(explorer, &TwoColumnsNodeExplorer::ignoreRequested, this,
          &NotebookExplorer2::onIgnoreRequested);
  connect(explorer, &TwoColumnsNodeExplorer::manageTagsRequested, this,
          &NotebookExplorer2::onManageTagsRequested);
  // T12 (notebook-explorer-drag-reorder): SortDialog2 lives in
  // NotebookExplorer2 because controllers MUST NOT show QDialog
  // (src/controllers/AGENTS.md). Reuses the slot wired in T11 for
  // CombinedNodeExplorer — one slot serves both explorer types.
  connect(explorer, &TwoColumnsNodeExplorer::sortRequested, this,
          &NotebookExplorer2::onSortRequested);

  // File system watcher: track expand/collapse
  connect(explorer, &INodeExplorer::folderExpanded, this, [this](const NodeIdentifier &p_folderId) {
    auto *nbService = m_services.get<NotebookCoreService>();
    if (!nbService)
      return;
    QString folderPath = nbService->buildAbsolutePath(m_currentNotebookId, p_folderId.relativePath);
    addWatchPath(folderPath);
  });
  connect(explorer, &INodeExplorer::folderCollapsed, this,
          [this](const NodeIdentifier &p_folderId) {
            auto *nbService = m_services.get<NotebookCoreService>();
            if (!nbService)
              return;
            QString folderPath =
                nbService->buildAbsolutePath(m_currentNotebookId, p_folderId.relativePath);
            removeWatchPath(folderPath);
          });

  m_nodeExplorer = explorer;
}

void NotebookExplorer2::loadNotebooks() {
  if (!m_notebookSelector) {
    return;
  }

  m_notebookSelector->loadNotebooks();

  // Sync m_currentNotebookId with the selector's restored selection.
  // This must happen AFTER loadNotebooks() completes because item data
  // is only available after all items are added.
  QString selectedId = m_notebookSelector->currentNotebookId();
  if (!selectedId.isEmpty()) {
    setCurrentNotebookInternal(selectedId);
  } else if (!m_currentNotebookId.isEmpty()) {
    // Last notebook closed: route through the internal setter (NOT the
    // public setCurrentNotebook, whose equality guard would no-op). This
    // resets the node model, emits currentNotebookChanged(""), and lets
    // downstream panels clear.
    setCurrentNotebookInternal(QString());
  }
}

void NotebookExplorer2::setCurrentNotebook(const QString &p_notebookId) {
  if (m_currentNotebookId == p_notebookId) {
    return;
  }

  m_notebookSelector->setCurrentNotebook(p_notebookId);
  setCurrentNotebookInternal(p_notebookId);
}

void NotebookExplorer2::setCurrentNotebookInternal(const QString &p_notebookId) {
  if (m_nodeExplorer && !m_currentNotebookId.isEmpty() && m_currentNotebookId != p_notebookId) {
    cacheCurrentExplorerState();
  }

  m_currentNotebookId = p_notebookId;

  // Re-setup file watcher for the new notebook.
  teardownFileWatcher();
  if (!m_currentNotebookId.isEmpty()) {
    auto *nbService = m_services.get<NotebookCoreService>();
    if (nbService) {
      QString rootPath = nbService->buildAbsolutePath(m_currentNotebookId, QString());
      addWatchPath(rootPath);
    }
  }

  updateTitleBarMenuState();

  // T26: surface read-only state to the user inline. Hidden for writable
  // notebooks; rich-text label with embedded lock icon when read-only. The
  // tooltip ("Read-only notebook (no PAT)") was set once in setupUI() and
  // does not need to be updated per-switch.
  bool readOnly = false;
  if (!m_currentNotebookId.isEmpty()) {
    auto *nbService = m_services.get<NotebookCoreService>();
    if (nbService) {
      readOnly = nbService->isNotebookReadOnly(m_currentNotebookId);
    }
  }
  if (m_readOnlyBadgeLabel) {
    if (readOnly) {
      // Use HTML img to embed the same Qt resource icon shown in the
      // selector combobox. Sized at 14px to match label x-height roughly.
      m_readOnlyBadgeLabel->setText(
          tr("<img src=\":/vnotex/data/core/icons/read_only.svg\" width=\"14\" height=\"14\"> "
             "Read-only"));
      m_readOnlyBadgeLabel->show();
    } else {
      m_readOnlyBadgeLabel->hide();
    }
  }

  // Surface read-only state to interested parties (e.g. MainWindow2 toggles the
  // File-toolbar mutation actions). Loose coupling: emit only.
  emit readOnlyStateChanged(readOnly);

  // Update current explorer
  if (m_nodeExplorer) {
    m_nodeExplorer->setNotebookId(p_notebookId);
    if (!m_hasRestoredSessionStatePending) {
      applyCachedExplorerState(p_notebookId);
    }
  }

  qCDebug(lcUi) << "NotebookExplorer2::setCurrentNotebookInternal: transition notebookId:"
                << p_notebookId;
  emit currentNotebookChanged(p_notebookId);
  emit currentExploredFolderChanged(currentExploredFolderId());
}

QString NotebookExplorer2::currentNotebookId() const { return m_currentNotebookId; }

void NotebookExplorer2::setCurrentNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid() || !m_nodeExplorer) {
    return;
  }

  m_nodeExplorer->expandToNode(p_nodeId);
  m_nodeExplorer->selectNode(p_nodeId);
  m_nodeExplorer->scrollToNode(p_nodeId);
}

void NotebookExplorer2::locateNode(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Switch to the node's notebook if not already showing it.
  if (m_currentNotebookId != p_nodeId.notebookId) {
    setCurrentNotebook(p_nodeId.notebookId);
  }

  setCurrentNode(p_nodeId);
}

NodeIdentifier NotebookExplorer2::currentNodeId() const {
  return m_nodeExplorer ? m_nodeExplorer->currentNodeId() : NodeIdentifier();
}

NotebookSelector2 *NotebookExplorer2::getNotebookSelector() const { return m_notebookSelector; }

INodeExplorer *NotebookExplorer2::getNodeExplorer() const { return m_nodeExplorer; }

void NotebookExplorer2::setExploreMode(ExploreMode p_mode) {
  if (m_exploreMode == p_mode) {
    return;
  }

  m_exploreMode = p_mode;
  updateExploreMode();
}

NotebookExplorer2::ExploreMode NotebookExplorer2::exploreMode() const { return m_exploreMode; }

void NotebookExplorer2::updateExploreMode() {
  if (m_nodeExplorer && !m_currentNotebookId.isEmpty()) {
    cacheCurrentExplorerState();
  }

  // Unregister old navigation wrapper before deleting explorer (prevents dangling pointer)
  auto *navService = m_services.get<NavigationModeService>();
  if (navService && m_nodeExplorer) {
    if (auto *oldWrapper = m_nodeExplorer->getNavigationModeWrapper()) {
      navService->unregisterNavigationTarget(oldWrapper);
    }
  }

  // Delete old explorer and create new one for the current mode
  m_mainLayout->removeWidget(m_nodeExplorer);
  delete m_nodeExplorer;

  if (m_exploreMode == Combined) {
    setupCombinedMode();
  } else {
    setupTwoColumnsMode();
  }

  // Register new navigation wrapper
  if (navService && m_nodeExplorer) {
    if (auto *newWrapper = m_nodeExplorer->getNavigationModeWrapper()) {
      navService->registerNavigationTarget(newWrapper);
    }
  }

  // Sync notebook to the explorer
  if (!m_currentNotebookId.isEmpty()) {
    m_nodeExplorer->setNotebookId(m_currentNotebookId);
    applyCachedExplorerState(m_currentNotebookId);
    syncWatchedPaths();
  }

  updateFocusProxy();
}

void NotebookExplorer2::cacheCurrentExplorerState() {
  if (!m_nodeExplorer) {
    return;
  }

  const auto notebookId = m_nodeExplorer->getNotebookId();
  if (notebookId.isEmpty()) {
    return;
  }

  const auto capturedState = m_nodeExplorer->captureState();
  if (m_notebookStateCache.contains(notebookId)) {
    m_notebookStateCache[notebookId] =
        mergeNodeExplorerStateForCache(capturedState, m_notebookStateCache.value(notebookId));
    return;
  }

  m_notebookStateCache[notebookId] = capturedState;
}

void NotebookExplorer2::applyCachedExplorerState(const QString &p_notebookId) {
  if (!m_nodeExplorer || p_notebookId.isEmpty() || !m_notebookStateCache.contains(p_notebookId)) {
    return;
  }

  m_nodeExplorer->applyState(m_notebookStateCache.value(p_notebookId));
}

void NotebookExplorer2::updateFocusProxy() { setFocusProxy(m_nodeExplorer); }

// --- Public Slots Implementation ---

void NotebookExplorer2::newNotebook() {
  // Use window() to get top-level MainWindow2 for dialog centering.
  NewNotebookDialog2 dialog(m_services, window());
  if (dialog.exec() == QDialog::Accepted) {
    const QString newNotebookId = dialog.getNewNotebookId();

    // Reload notebooks and select the newly created one.
    loadNotebooks();
    setCurrentNotebook(newNotebookId);

    // Sync configuration is now collected BEFORE notebook creation via the
    // "Configure..." button inside NewNotebookDialog2 (per ADR-9). The dialog
    // chains createNotebook + bootstrapSync atomically before returning Accepted.
    // By the time we reach this point, the notebook is either fully sync-ready
    // or doesn't exist (bootstrapSync rolled back). No post-create bootstrap needed.
  }
}

void NotebookExplorer2::newNotebookFromFolder() {
  // Get default path from session config.
  QString defaultPath;
  {
    auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
    defaultPath = sessionConfig.getNewNotebookDefaultRootFolderPath();
    if (defaultPath.isEmpty()) {
      defaultPath = QDir::homePath();
    }
  }

  QString rootFolder = QFileDialog::getExistingDirectory(
      window(), tr("Select Folder to Open as Raw Notebook"), defaultPath,
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if (rootFolder.isEmpty()) {
    return;
  }

  // Build input for raw notebook creation.
  NewNotebookInput input;
  input.name = QFileInfo(rootFolder).fileName();
  input.rootFolderPath = rootFolder;
  input.type = NotebookType::Raw;

  // Create notebook via controller.
  NewNotebookController controller(m_services);
  NewNotebookResult result = controller.createNotebook(input);

  if (!result.success) {
    MessageBoxHelper::notify(MessageBoxHelper::Critical, result.errorMessage, window());
    return;
  }

  // Save parent dir as default for next time.
  {
    QFileInfo fi(rootFolder);
    auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
    sessionConfig.setNewNotebookDefaultRootFolderPath(fi.absolutePath());
  }

  // Reload notebooks and select the newly created one.
  loadNotebooks();
  setCurrentNotebook(result.notebookId);
}

void NotebookExplorer2::importNotebook() {
  // T25 (open-notebook-remote-readonly): the inline QFileDialog +
  // stack-allocated OpenNotebookController path was replaced by the new
  // OpenNotebookDialog2 widget which supports BOTH the local-folder pick
  // and the remote-URL clone path. The dialog owns its own controller
  // instance (heap-allocated, parented to the dialog) so this slot stays
  // simple: open the dialog, refresh the selector when it signals success.
  //
  // The dialog emits notebookOpened(QString) ONLY on a successful local
  // open in T24's MVP (remote mode is still stubbed pending T25 wiring of
  // OpenNotebookController::cloneAndOpen). When that signal fires we
  // refresh NotebookSelector2 and switch the explorer to the freshly
  // opened notebook, matching the behavior the old QFileDialog path used
  // to provide.
  OpenNotebookDialog2 dialog(m_services, window());
  connect(&dialog, &OpenNotebookDialog2::notebookOpened, this,
          [this](const QString &p_notebookId, bool p_suppressSyncPrompt) {
            // Record the interactively-opened notebook so the reconcileFinished handler
            // can auto-prompt for a missing PAT (state S2) right after open. Scoped to
            // this id only, so the startup reconcile sweep never pops a dialog.
            //
            // EXCEPTION (silent S2 clone): a no-PAT remote clone lands as a writable S2
            // notebook that must open SILENTLY. The dialog signals that via
            // p_suppressSyncPrompt == true; in that case we do NOT arm the prompt, so
            // the AUTH_FAILED that reconcile emits for the tokenless notebook will not
            // pop the Sync Info dialog. Selector refresh / setCurrentNotebook is
            // unchanged for both cases.
            if (!p_suppressSyncPrompt && !p_notebookId.isEmpty()) {
              m_pendingOpenSyncPrompt.insert(p_notebookId);
            }
            if (m_notebookSelector) {
              m_notebookSelector->loadNotebooks();
              setCurrentNotebook(p_notebookId);
            }
          });
  dialog.exec();
}

void NotebookExplorer2::openVNote3Notebook() {
  OpenVNote3NotebookDialog2 dialog(m_services, window());
  if (dialog.exec() == QDialog::Accepted) {
    loadNotebooks();
    setCurrentNotebook(dialog.getOpenedNotebookId());
  }
}

void NotebookExplorer2::manageNotebooks() {
  QString currentNotebookId;
  if (m_notebookSelector) {
    currentNotebookId = m_notebookSelector->currentNotebookId();
  }

  ManageNotebooksDialog2 dialog(m_services, currentNotebookId, window());
  dialog.exec();

  // Reload notebooks after dialog closes (changes may have occurred).
  if (m_notebookSelector) {
    m_notebookSelector->loadNotebooks();
  }
}

void NotebookExplorer2::newFolder() {
  NodeIdentifier parentId = currentExploredFolderId();
  if (!parentId.isValid()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Please first create a notebook to hold your data."), window());
    return;
  }

  // Functional read-only guard: silently no-op for read-only notebooks.
  if (isCurrentNotebookReadOnly()) {
    return;
  }

  // Delegate to the shared handler
  onNewFolderRequested(parentId);
}

void NotebookExplorer2::newNote() {
  NodeIdentifier parentId = currentExploredFolderId();
  if (!parentId.isValid()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Please first create a notebook to hold your data."), window());
    return;
  }

  // Functional read-only guard: silently no-op for read-only notebooks.
  if (isCurrentNotebookReadOnly()) {
    return;
  }

  // Delegate to the shared handler
  onNewNoteRequested(parentId);
}

void NotebookExplorer2::newQuickNote() {
  // Functional read-only guard: silently no-op for read-only notebooks.
  if (isCurrentNotebookReadOnly()) {
    return;
  }

  // Get quick note schemes from session config.
  auto &sessionConfig = m_services.get<ConfigMgr2>()->getSessionConfig();
  const auto &schemes = sessionConfig.getQuickNoteSchemes();
  if (schemes.isEmpty()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Please set up quick note schemes in the Settings dialog first."),
                             window());
    return;
  }

  // Show selection dialog.
  auto *themeService = m_services.get<ThemeService>();
  SelectDialog dialog(
      tr("New Quick Note"),
      themeService->paletteColor(QStringLiteral("widgets#quickselector#item_icon#fg")),
      themeService->paletteColor(QStringLiteral("widgets#quickselector#item_icon#border")),
      window());
  for (int i = 0; i < schemes.size(); ++i) {
    dialog.addSelection(schemes[i].m_name, i);
  }

  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  int selection = dialog.getSelection();
  const auto &scheme = schemes[selection];

  // Resolve target folder.
  QString notebookId = m_currentNotebookId;
  QString folderPath;

  // Expand snippet magic words (e.g. date tokens) in the scheme folder path so a
  // quick note can target a dynamic, date-based directory such as journal/%yyyy%/%MM%.
  QString expandedFolder =
      m_services.get<SnippetCoreService>()->applySnippetBySymbol(scheme.m_folderPath);

  if (!expandedFolder.isEmpty()) {
    // Try to resolve the scheme folder path to a notebook.
    auto &notebookService = *m_services.get<NotebookCoreService>();
    QJsonObject resolved = notebookService.resolvePathToNotebook(expandedFolder);
    if (!resolved.isEmpty()) {
      notebookId = resolved[QLatin1String(vxcore::kJsonKeyNotebookId)].toString();
      folderPath = resolved[QStringLiteral("relativePath")].toString();
    } else {
      // Path not found in any notebook - use expandedFolder as-is if current notebook is
      // valid.
      if (notebookId.isEmpty()) {
        MessageBoxHelper::notify(
            MessageBoxHelper::Information,
            tr("The quick note folder path (%1) is not within any open notebook.")
                .arg(expandedFolder),
            window());
        return;
      }
      // Assume relative path within current notebook.
      folderPath = expandedFolder;
    }
  } else {
    // Use current explored folder.
    NodeIdentifier parentId = currentExploredFolderId();
    if (parentId.isValid()) {
      notebookId = parentId.notebookId;
      folderPath = parentId.relativePath;
    }
  }

  if (notebookId.isEmpty()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("The quick note should be created within a notebook."), window());
    return;
  }

  // Generate filename using snippet expansion.
  QString expandedName =
      m_services.get<SnippetCoreService>()->applySnippetBySymbol(scheme.m_noteName);
  QFileInfo finfo(expandedName);

  // Get notebook root path to generate unique filename.
  auto &notebookService = *m_services.get<NotebookCoreService>();

  // Ensure the (possibly newly expanded/date-based) target folder exists before creating
  // the file: vxcore createFile requires the parent folder node to exist.
  if (!folderPath.isEmpty()) {
    QString folderId = notebookService.createFolderPath(notebookId, folderPath);
    if (folderId.isEmpty()) {
      MessageBoxHelper::notify(
          MessageBoxHelper::Information,
          tr("Failed to create the quick note folder (%1).").arg(folderPath), window());
      return;
    }
  }

  QJsonObject notebookConfig = notebookService.getNotebookConfig(notebookId);
  QString rootFolder = notebookConfig[QLatin1String(vxcore::kJsonKeyRootFolder)].toString();
  QString parentAbsPath = folderPath.isEmpty() ? rootFolder : QDir(rootFolder).filePath(folderPath);

  QString newFileName = FileUtils2::generateFileNameWithSequence(
      parentAbsPath, finfo.completeBaseName(), finfo.suffix());

  // Get template content if specified.
  QString templateContent;
  if (!scheme.m_template.isEmpty()) {
    auto &templateService = *m_services.get<TemplateService>();
    templateContent = templateService.getTemplateContent(scheme.m_template);
  }

  // Create the file via NotebookService.
  QString fileId = notebookService.createFile(notebookId, folderPath, newFileName);
  if (fileId.isEmpty()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Failed to create quick note from scheme (%1).").arg(scheme.m_name),
                             window());
    return;
  }

  // Write template content if present.
  if (!templateContent.isEmpty()) {
    QString filePath = QDir(parentAbsPath).filePath(newFileName);
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      file.write(templateContent.toUtf8());
      file.close();
    }
  }

  NodeIdentifier newNodeId;
  newNodeId.notebookId = notebookId;
  newNodeId.relativePath =
      folderPath.isEmpty() ? newFileName : folderPath + QStringLiteral("/") + newFileName;

  auto *bufferSvc = m_services.get<BufferService>();
  if (bufferSvc) {
    FileOpenSettings settings;
    settings.m_mode = ViewWindowMode::Edit;
    settings.m_forceMode = true;
    settings.m_newFile = true;
    bufferSvc->openBuffer(newNodeId, settings);
  }
}

void NotebookExplorer2::importFile() {
  NodeIdentifier folderId = currentExploredFolderId();
  if (!folderId.isValid()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Please first create a notebook to hold your data."), window());
    return;
  }

  // Functional read-only guard: silently no-op for read-only notebooks.
  if (isCurrentNotebookReadOnly()) {
    return;
  }

  onImportFilesRequested(folderId);
}

void NotebookExplorer2::importFolder() {
  NodeIdentifier folderId = currentExploredFolderId();
  if (!folderId.isValid()) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Please first create a notebook to hold your data."), window());
    return;
  }

  // Functional read-only guard: silently no-op for read-only notebooks.
  if (isCurrentNotebookReadOnly()) {
    return;
  }

  onImportFolderRequested(folderId);
}

bool NotebookExplorer2::isCurrentNotebookReadOnly() const {
  if (m_currentNotebookId.isEmpty()) {
    return false;
  }
  auto *nbService = m_services.get<NotebookCoreService>();
  return nbService && nbService->isNotebookReadOnly(m_currentNotebookId);
}

NodeIdentifier NotebookExplorer2::currentExploredFolderId() const {
  NodeIdentifier nodeId = currentNodeId();
  if (!nodeId.isValid()) {
    // Return notebook root if no selection
    if (!m_currentNotebookId.isEmpty()) {
      NodeIdentifier rootId;
      rootId.notebookId = m_currentNotebookId;
      rootId.relativePath = QString();
      return rootId;
    }
    return NodeIdentifier();
  }

  // Check if it's a folder
  NodeInfo info;
  if (m_nodeExplorer) {
    info = m_nodeExplorer->getNodeInfo(nodeId);
  }

  if (info.isFolder && !info.isExternal) {
    return nodeId;
  }

  // Return parent folder for file nodes
  NodeIdentifier parentId;
  parentId.notebookId = nodeId.notebookId;
  parentId.relativePath = nodeId.parentPath();
  return parentId;
}

NodeIdentifier NotebookExplorer2::currentExploredNodeId() const { return currentNodeId(); }

QByteArray NotebookExplorer2::saveState() const {
  // Flush the active notebook's latest expansion/selection into the cache
  // before serializing, so the current session state is not lost at shutdown.
  const_cast<NotebookExplorer2 *>(this)->cacheCurrentExplorerState();

  // Save current node path and view state
  QByteArray state;
  QDataStream stream(&state, QIODevice::WriteOnly);

  stream << c_sessionVersion;

  // Save explore mode
  stream << static_cast<int>(m_exploreMode);

  // Save splitter sizes if in TwoColumns mode
  auto *twoColumns = qobject_cast<TwoColumnsNodeExplorer *>(m_nodeExplorer);
  if (twoColumns) {
    stream << twoColumns->saveSplitterState();
  } else {
    stream << QByteArray();
  }

  stream << m_notebookStateCache;

  return state;
}

void NotebookExplorer2::restoreState(const QByteArray &p_data) {
  if (p_data.isEmpty()) {
    return;
  }

  QDataStream stream(p_data);

  quint32 versionOrMode = 0;
  stream >> versionOrMode;

  int mode = 0;
  bool readCache = false;
  if (versionOrMode == c_sessionVersion) {
    stream >> mode;
    readCache = true;
  } else {
    mode = static_cast<int>(versionOrMode);
  }

  setExploreMode(static_cast<ExploreMode>(mode));

  QByteArray splitterState;
  stream >> splitterState;
  if (!splitterState.isEmpty()) {
    auto *twoCol = qobject_cast<TwoColumnsNodeExplorer *>(m_nodeExplorer);
    if (twoCol) {
      twoCol->restoreSplitterState(splitterState);
    }
  }

  if (readCache) {
    QHash<QString, NodeExplorerState> notebookStateCache;
    stream >> notebookStateCache;
    if (stream.status() == QDataStream::Ok) {
      m_notebookStateCache = notebookStateCache;
      m_hasRestoredSessionStatePending = true;
    }
  }
}

void NotebookExplorer2::applyRestoredSessionState() {
  if (!m_hasRestoredSessionStatePending) {
    return;
  }

  applyCachedExplorerState(m_currentNotebookId);
  m_hasRestoredSessionStatePending = false;
}

void NotebookExplorer2::setNodeViewOrder(ViewOrder p_order) {
  m_nodeExplorer->setViewOrder(p_order);
}

// --- GUI request handlers from controller signals ---

void NotebookExplorer2::onNewNoteRequested(const NodeIdentifier &p_parentId) {
  // Suppress the fs-watcher's delayed reload for this parent: we perform the
  // model refresh synchronously below and need the selection to survive past
  // the watcher's 500 ms debounce window.
  if (auto *nbService = m_services.get<NotebookCoreService>()) {
    const QString parentAbsPath =
        nbService->buildAbsolutePath(p_parentId.notebookId, p_parentId.relativePath);
    expectFsChange(parentAbsPath);
  }

  NewNoteDialog2 dialog(m_services, p_parentId, window());
  if (dialog.exec() == QDialog::Accepted) {
    NodeIdentifier newNodeId = dialog.getNewNodeId();
    if (newNodeId.isValid()) {
      m_nodeExplorer->reloadNode(p_parentId);
      setCurrentNode(newNodeId);

      auto *bufferSvc = m_services.get<BufferService>();
      if (bufferSvc) {
        FileOpenSettings settings;
        settings.m_mode = ViewWindowMode::Edit;
        settings.m_forceMode = true;
        settings.m_newFile = true;
        bufferSvc->openBuffer(newNodeId, settings);
      }
    }
  }
}

void NotebookExplorer2::onNewFolderRequested(const NodeIdentifier &p_parentId) {
  // Suppress the fs-watcher's delayed reload for this parent: we perform the
  // model refresh synchronously below and need the selection to survive past
  // the watcher's 500 ms debounce window.
  if (auto *nbService = m_services.get<NotebookCoreService>()) {
    const QString parentAbsPath =
        nbService->buildAbsolutePath(p_parentId.notebookId, p_parentId.relativePath);
    expectFsChange(parentAbsPath);
  }

  NewFolderDialog2 dialog(m_services, p_parentId, window());
  if (dialog.exec() == QDialog::Accepted) {
    NodeIdentifier newNodeId = dialog.getNewNodeId();
    if (newNodeId.isValid()) {
      m_nodeExplorer->reloadNode(p_parentId);
      setCurrentNode(newNodeId);
    }
  }
}

void NotebookExplorer2::onRenameRequested(const NodeIdentifier &p_nodeId,
                                          const QString &p_currentName) {
  Q_UNUSED(p_currentName);
  m_nodeExplorer->startInlineRename(p_nodeId);
}

void NotebookExplorer2::onDeleteRequested(const QList<NodeIdentifier> &p_nodeIds,
                                          bool p_permanent) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  QString title = p_permanent ? tr("Delete Permanently") : tr("Delete");
  QString message;

  if (p_permanent) {
    message = tr("Permanently delete %n node(s)? This cannot be undone.", "", p_nodeIds.size());
  } else {
    message = tr("Move %n node(s) to recycle bin?", "", p_nodeIds.size());
  }

  int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Question, title, message,
                                               QString(), window());
  if (ret != QMessageBox::Ok) {
    return;
  }

  // Use unified interface to perform the delete
  m_nodeExplorer->handleDeleteConfirmed(p_nodeIds, p_permanent);
}

void NotebookExplorer2::onRemoveFromNotebookRequested(const QList<NodeIdentifier> &p_nodeIds) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  QString message =
      tr("Remove %n node(s) from notebook index? Files will remain on disk.", "", p_nodeIds.size());

  int ret = MessageBoxHelper::questionOkCancel(
      MessageBoxHelper::Question, tr("Remove from Notebook"), message, QString(), window());
  if (ret != QMessageBox::Ok) {
    return;
  }

  // Use unified interface to perform the removal
  m_nodeExplorer->handleRemoveConfirmed(p_nodeIds);
}

void NotebookExplorer2::onMissingNodeRemovalRequested(const QList<NodeIdentifier> &p_nodeIds) {
  if (p_nodeIds.isEmpty()) {
    return;
  }

  // ONE consolidated prompt for the whole batch. The files on disk are NOT
  // affected — only the notebook's index entries are removed on confirm.
  QString message =
      tr("%n item(s) indexed in this notebook are missing on disk. Remove them from the notebook "
         "index? The files on disk are NOT affected.",
         "", p_nodeIds.size());

  int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Question, tr("Missing Items"),
                                               message, QString(), window());
  if (ret == QMessageBox::Ok) {
    // Controller revalidates each id, unindexes the still-missing ones (folder
    // unindex cascades) and reloads the affected parents.
    m_nodeExplorer->handleMissingRemovalConfirmed(p_nodeIds);
  } else {
    // Decline: suppress for the rest of the session so they stay grayed and do
    // not re-prompt.
    m_nodeExplorer->suppressMissingNodes(p_nodeIds);
  }
}

void NotebookExplorer2::onImportFilesRequested(const NodeIdentifier &p_targetFolderId) {
  QStringList files =
      QFileDialog::getOpenFileNames(window(), tr("Import Files"), QString(), tr("All Files (*)"));
  if (files.isEmpty()) {
    return;
  }

  // Suppress the fs-watcher's delayed reload for this parent: we perform the
  // model refresh synchronously below and need the selection to survive past
  // the watcher's 500 ms debounce window.
  if (auto *nbService = m_services.get<NotebookCoreService>()) {
    const QString parentAbsPath =
        nbService->buildAbsolutePath(p_targetFolderId.notebookId, p_targetFolderId.relativePath);
    expectFsChange(parentAbsPath);
  }

  // Use NotebookService to perform the import, then reload
  auto &notebookService = *m_services.get<NotebookCoreService>();
  for (const QString &filePath : files) {
    QFileInfo fileInfo(filePath);
    QString destPath = p_targetFolderId.relativePath.isEmpty()
                           ? fileInfo.fileName()
                           : p_targetFolderId.relativePath + "/" + fileInfo.fileName();
    notebookService.importFile(p_targetFolderId.notebookId, filePath, destPath);
  }
  // Reload the folder to show imported files
  m_nodeExplorer->reloadNode(p_targetFolderId);
}

void NotebookExplorer2::onImportFolderRequested(const NodeIdentifier &p_targetFolderId) {
  // Suppress the fs-watcher's delayed reload for this parent: we perform the
  // model refresh synchronously below and need the selection to survive past
  // the watcher's 500 ms debounce window.
  if (auto *nbService = m_services.get<NotebookCoreService>()) {
    const QString parentAbsPath =
        nbService->buildAbsolutePath(p_targetFolderId.notebookId, p_targetFolderId.relativePath);
    expectFsChange(parentAbsPath);
  }

  ImportFolderDialog2 dialog(m_services, p_targetFolderId, window());
  if (dialog.exec() == QDialog::Accepted) {
    NodeIdentifier newNodeId = dialog.getNewNodeId();
    if (newNodeId.isValid()) {
      m_nodeExplorer->reloadNode(p_targetFolderId);
      setCurrentNode(newNodeId);
    }
  }
}

void NotebookExplorer2::onPropertiesRequested(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Get node info using unified interface
  NodeInfo nodeInfo = m_nodeExplorer->getNodeInfo(p_nodeId);

  if (!nodeInfo.isValid()) {
    return;
  }

  NodePropertiesDialog2 dialog(m_services, p_nodeId, nodeInfo, window());
  dialog.exec();
}

void NotebookExplorer2::onMarkRequested(const QList<NodeIdentifier> &p_ids) {
  if (p_ids.isEmpty() || !m_nodeExplorer) {
    return;
  }

  NodeInfo seedInfo = m_nodeExplorer->getNodeInfo(p_ids.first());

  MarkNodeDialog2 dialog(seedInfo.textColor, seedInfo.backgroundColor, window());
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  const QString textColor = dialog.textColor();
  const QString backgroundColor = dialog.backgroundColor();
  for (const auto &id : p_ids) {
    if (id.isValid()) {
      m_nodeExplorer->handleMarkResult(id, textColor, backgroundColor);
    }
  }
}

void NotebookExplorer2::onIgnoreRequested(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  // Extract name from relative path.
  const QString name =
      p_nodeId.relativePath.mid(p_nodeId.relativePath.lastIndexOf(QLatin1Char('/')) + 1);

  // Confirmation dialog.
  const QString title = tr("Ignore");
  const QString message = tr("Add \"%1\" to the ignore list of the notebook?").arg(name);
  int ret = MessageBoxHelper::questionOkCancel(MessageBoxHelper::Question, title, message,
                                               QString(), window());
  if (ret != QMessageBox::Ok) {
    return;
  }

  // Read-modify-write notebook config.
  auto *notebookService = m_services.get<NotebookCoreService>();
  QJsonObject config = notebookService->getNotebookConfig(p_nodeId.notebookId);
  QJsonArray ignored = config.value(QLatin1String(vxcore::kJsonKeyIgnored)).toArray();

  // Check for duplicate.
  for (const auto &val : ignored) {
    if (val.toString() == name) {
      return;
    }
  }

  ignored.append(name);
  config[QLatin1String(vxcore::kJsonKeyIgnored)] = ignored;

  const QString configStr = QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Compact));
  if (!notebookService->updateNotebookConfig(p_nodeId.notebookId, configStr)) {
    MessageBoxHelper::notify(MessageBoxHelper::Warning,
                             tr("Failed to update notebook configuration."), window());
  } else {
    // Reload parent node to reflect the updated ignore list.
    NodeIdentifier parentId;
    parentId.notebookId = p_nodeId.notebookId;
    parentId.relativePath = p_nodeId.parentPath();
    m_nodeExplorer->reloadNode(parentId);
  }
}

void NotebookExplorer2::onManageTagsRequested(const NodeIdentifier &p_nodeId) {
  if (!p_nodeId.isValid()) {
    return;
  }

  ViewTagsDialog2 dialog(m_services, p_nodeId, window());
  dialog.exec();
}

void NotebookExplorer2::onErrorOccurred(const QString &p_title, const QString &p_message) {
  MessageBoxHelper::notify(MessageBoxHelper::Critical, p_title + ": " + p_message, window());
}

void NotebookExplorer2::onInfoMessage(const QString &p_title, const QString &p_message) {
  MessageBoxHelper::notify(MessageBoxHelper::Information, p_title + ": " + p_message, window());
}

// T11 (notebook-explorer-drag-reorder): owns SortDialog2 lifecycle. Reads the
// parent folder's current children via listFolderChildren, shows up to two
// modal dialogs in sequence (folders then files; strict separation per locked
// decision), and routes the result through INodeExplorer::requestReorderNodes.
// MUST NOT call vxcore/service directly for persistence — go through the
// explorer bridge → controller → service chain. The unchanged-order check at
// the view layer prevents spurious service calls.
//
// The dialog-show logic is extracted into runSortDialogsForChildren (a public
// static helper) so widget tests can exercise it without instantiating a full
// NotebookExplorer2.
void NotebookExplorer2::onSortRequested(const NodeIdentifier &p_parentId) {
  auto *notebookService = m_services.get<NotebookCoreService>();
  if (!notebookService || !m_nodeExplorer) {
    return;
  }

  const QJsonObject childrenJson =
      notebookService->listFolderChildren(p_parentId.notebookId, p_parentId.relativePath);

  const SortDialogResult result = runSortDialogsForChildren(p_parentId, childrenJson, this);

  if (!result.newFolderOrder.isEmpty() || !result.newFileOrder.isEmpty()) {
    m_nodeExplorer->requestReorderNodes(p_parentId, result.newFolderOrder, result.newFileOrder);
  }
}

// NotebookExplorer2::runSortDialogsForChildren lives in
// notebookexplorer2_sortseam.cpp so widget tests can link the helper without
// dragging in the full NotebookExplorer2 TU and its ~20 transitive widget /
// service deps.
void NotebookExplorer2::onNodeActivated(const NodeIdentifier &p_nodeId,
                                        const FileOpenSettings &p_settings) {
  auto *bufferSvc = m_services.get<BufferService>();
  if (!bufferSvc) {
    return;
  }

  bufferSvc->openBuffer(p_nodeId, p_settings);

  emit currentExploredFolderChanged(currentExploredFolderId());
}

// --- File system watcher implementation ---

void NotebookExplorer2::setupFileWatcher() {
  m_fsWatcher = new QFileSystemWatcher(this);
  m_fsReloadTimer = new QTimer(this);
  m_fsReloadTimer->setSingleShot(true);
  m_fsReloadTimer->setInterval(500);
  connect(m_fsWatcher, &QFileSystemWatcher::directoryChanged, this,
          &NotebookExplorer2::onFileSystemChanged);
  connect(m_fsReloadTimer, &QTimer::timeout, this, &NotebookExplorer2::onFsReloadTimeout);
}

void NotebookExplorer2::teardownFileWatcher() {
  m_fsReloadTimer->stop();
  auto dirs = m_fsWatcher->directories();
  if (!dirs.isEmpty()) {
    m_fsWatcher->removePaths(dirs);
  }
  m_lastChangedDir.clear();
}

void NotebookExplorer2::expectFsChange(const QString &p_absolutePath, int p_windowMs) {
  if (p_absolutePath.isEmpty()) {
    return;
  }
  const QString key = QDir::cleanPath(QDir(p_absolutePath).absolutePath());
  const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + qMax(p_windowMs, 0);
  m_expectedFsChangesDeadline.insert(key, deadline);
}

bool NotebookExplorer2::consumeExpectedFsChange(const QString &p_absolutePath) {
  if (m_expectedFsChangesDeadline.isEmpty()) {
    return false;
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();

  // Opportunistic purge of expired entries.
  for (auto it = m_expectedFsChangesDeadline.begin(); it != m_expectedFsChangesDeadline.end();) {
    if (it.value() < now) {
      it = m_expectedFsChangesDeadline.erase(it);
    } else {
      ++it;
    }
  }

  const QString key = QDir::cleanPath(QDir(p_absolutePath).absolutePath());
  auto it = m_expectedFsChangesDeadline.find(key);
  if (it == m_expectedFsChangesDeadline.end()) {
    return false;
  }
  m_expectedFsChangesDeadline.erase(it);
  return true;
}

void NotebookExplorer2::addWatchPath(const QString &p_path) {
  if (p_path.isEmpty()) {
    return;
  }
  if (!QDir(p_path).exists()) {
    return;
  }
  if (p_path.contains(QStringLiteral("vx_recycle_bin"))) {
    return;
  }
  m_fsWatcher->addPath(p_path);
}

void NotebookExplorer2::removeWatchPath(const QString &p_path) { m_fsWatcher->removePath(p_path); }

void NotebookExplorer2::syncWatchedPaths() {
  teardownFileWatcher();
  if (m_currentNotebookId.isEmpty() || !m_nodeExplorer) {
    return;
  }

  auto *nbService = m_services.get<NotebookCoreService>();
  if (!nbService) {
    return;
  }

  QString rootPath = nbService->buildAbsolutePath(m_currentNotebookId, QString());
  addWatchPath(rootPath);

  auto state = m_nodeExplorer->captureState();
  for (const auto &folderId : state.expandedFolders) {
    QString folderPath = nbService->buildAbsolutePath(m_currentNotebookId, folderId.relativePath);
    addWatchPath(folderPath);
  }
}

void NotebookExplorer2::onFileSystemChanged(const QString &p_path) {
  // While a sync is in flight for the currently displayed notebook (or for
  // the 2 s grace window after syncFinished), drop fs events on any path
  // under that notebook's root: the explorer reload would only wipe the
  // user's selection. consumeExpectedFsChange is one-shot and cannot cover
  // sync's multi-event burst, so this is a separate code path.
  if (m_activeSyncFsSuppression.contains(m_currentNotebookId) &&
      isPathUnderNotebookRoot(m_services, m_currentNotebookId, p_path)) {
    return;
  }
  if (consumeExpectedFsChange(p_path)) {
    // App-initiated change. Synchronous reload has already been performed by
    // the originating handler; the delayed reload would only wipe selection.
    return;
  }
  m_lastChangedDir = p_path;
  m_fsReloadTimer->start();
}

void NotebookExplorer2::onFsReloadTimeout() {
  if (m_lastChangedDir.isEmpty() || !m_nodeExplorer) {
    return;
  }

  auto *nbService = m_services.get<NotebookCoreService>();
  if (!nbService) {
    m_lastChangedDir.clear();
    return;
  }

  QString rootPath = nbService->buildAbsolutePath(m_currentNotebookId, QString());
  QString relPath = QDir(rootPath).relativeFilePath(m_lastChangedDir);
  if (relPath == QStringLiteral(".") || relPath.isEmpty()) {
    relPath.clear();
  }

  NodeIdentifier nodeId;
  nodeId.notebookId = m_currentNotebookId;
  nodeId.relativePath = relPath;
  m_nodeExplorer->reloadNode(nodeId);

  // Qt may drop a watched path after it changes; re-add it.
  addWatchPath(m_lastChangedDir);

  m_lastChangedDir.clear();
}

// --- Sync UI implementation (T15) ---

void NotebookExplorer2::onSyncFailedSurface(const QString &p_notebookId, VxCoreError p_code,
                                            const QString &p_message) {
  // Always refresh the button regardless of error category so the post-failure
  // state (back to "Sync Now" from in-progress) is reflected.
  updateSyncButtonState();

  if (p_notebookId.isEmpty()) {
    return;
  }

  // Conflict failures are owned by MainWindow2 (which surfaces the conflict
  // resolution flow). Don't double-notify here.
  if (p_code == VXCORE_ERR_SYNC_CONFLICT) {
    return;
  }

  if (p_code == VXCORE_ERR_SYNC_IN_PROGRESS) {
    // Overlap with another in-flight sync (queue full, queue rejected on
    // shutdown, OR backend op_mutex_ try_lock race). NOT a real failure —
    // the original sync continues; this trigger was redundant. Log and
    // refresh the button only. NEVER mutate anti-spam state, circuit-
    // breaker, or reconcile-error tracker.
    qCInfo(lcSync) << "NotebookExplorer2::onSyncFailedSurface: SYNC_IN_PROGRESS"
                   << "treated as overlap (silent) notebookId:" << p_notebookId
                   << "message:" << p_message;
    updateSyncButtonState();
    return;
  }

  // Best-effort notebook label for the message body.
  QString notebookLabel = p_notebookId;
  if (auto *nbSvc = m_services.get<NotebookCoreService>()) {
    const QJsonObject cfg = nbSvc->getNotebookConfig(p_notebookId);
    const QString name = cfg.value(QLatin1String(vxcore::kJsonKeyName)).toString();
    if (!name.isEmpty()) {
      notebookLabel = name;
    }
  }

  if (p_code == VXCORE_ERR_SYNC_AUTH_FAILED) {
    // Suppress repeat popups within a "failure streak". Counter is cleared on
    // the next successful sync, notebook switch, or sync disable.
    if (m_authFailureNotified.contains(p_notebookId)) {
      qCDebug(lcSync) << "NotebookExplorer2::onSyncFailedSurface: auth-failure popup suppressed"
                      << "notebookId:" << p_notebookId;
      return;
    }
    // Arm credential-update auto-retry: if the user fixes the PAT via Sync
    // Info, credentialsSetFinished(OK) will trigger a sync automatically so
    // the user sees the verdict.
    m_credentialUpdateRetryArm.insert(p_notebookId);
    m_authFailureNotified.insert(p_notebookId);

    QMessageBox box(QMessageBox::Warning, tr("Sync authentication failed"),
                    tr("Sync failed for notebook \"%1\".\n\nGitHub rejected the stored Personal "
                       "Access Token (HTTP 401). The token may have been revoked, expired, or had "
                       "its SSO authorization withdrawn.\n\nUpdate the token to resume syncing.")
                        .arg(notebookLabel),
                    QMessageBox::Cancel, this);
    box.setDetailedText(p_message);
    QPushButton *openInfoBtn = box.addButton(tr("Open Sync Info..."), QMessageBox::AcceptRole);
    box.setDefaultButton(openInfoBtn);
    box.exec();
    if (box.clickedButton() == openInfoBtn) {
      auto *dlg = new NotebookSyncInfoDialog2(m_services, p_notebookId, this);
      dlg->setAttribute(Qt::WA_DeleteOnClose);
      dlg->open();
    }
    return;
  }

  if (p_code == VXCORE_ERR_SYNC_NETWORK) {
    if (m_networkFailureNotified.contains(p_notebookId)) {
      qCDebug(lcSync) << "NotebookExplorer2::onSyncFailedSurface: network-failure popup suppressed"
                      << "notebookId:" << p_notebookId;
      return;
    }
    m_networkFailureNotified.insert(p_notebookId);
    MessageBoxHelper::notify(
        MessageBoxHelper::Information, tr("Sync network error"),
        tr("Sync failed for notebook \"%1\" because of a network error. VNote will retry "
           "automatically on the next change.")
            .arg(notebookLabel),
        p_message, this);
    return;
  }

  // Other failure codes (queue full, invalid param, not enabled, unknown).
  // These are usually internal-state problems the user can't act on; show a
  // single warning and rely on the anti-spam guard reusing the auth slot's
  // bucket so we never spam during a failure streak.
  if (m_authFailureNotified.contains(p_notebookId)) {
    return;
  }
  m_authFailureNotified.insert(p_notebookId);
  MessageBoxHelper::notify(MessageBoxHelper::Warning, tr("Sync failed"),
                           tr("Sync failed for notebook \"%1\".").arg(notebookLabel), p_message,
                           this);
}

void NotebookExplorer2::onSyncButtonClicked() {
  const QString nbId = currentNotebookId();
  if (nbId.isEmpty()) {
    return;
  }
  auto *syncSvc = m_services.get<SyncService>();
  auto *classifier = m_services.get<SyncStateClassifier>();
  if (!syncSvc || !classifier) {
    return;
  }
  const SyncState state = classifier->classify(nbId);
  qCDebug(lcSync) << "NotebookExplorer2::onSyncButtonClicked: clicked notebookId:" << nbId
                  << "state:" << static_cast<int>(state);
  // W4.T2: S0 (sync disabled) MUST route to the bootstrap dialog with empty
  // fields so the user can re-enable sync. Triggering Sync Now is forbidden
  // here per plan ("DO NOT allow clicking Sync Now on S0 notebook").
  switch (state) {
  case SyncState::S0:
  case SyncState::S6: {
    qCDebug(lcSync)
        << "NotebookExplorer2::onSyncButtonClicked: disabled state, opening bootstrap dialog"
        << "notebookId:" << nbId;
    auto *dlg = new NotebookSyncInfoDialog2(m_services, nbId, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setBootstrapMode(true);
    dlg->open();
    return;
  }
  case SyncState::S5:
  case SyncState::S7:
    // Wave 12.2 / F5.9: if a sync is already in flight for this notebook,
    // the button doubles as Cancel. Route to cancelSync; the syncFinished
    // signal will repaint the button back to "Sync Now" via the existing
    // updateSyncButtonState path.
    if (syncSvc->isSyncInProgress(nbId)) {
      qCDebug(lcSync) << "NotebookExplorer2::onSyncButtonClicked: cancelling in-flight sync"
                      << "notebookId:" << nbId;
      syncSvc->cancelSync(nbId);
      return;
    }
    // Manual Sync Now overrides anti-spam suppression: the user explicitly
    // clicked, so any next failure MUST surface even if a prior failure popup
    // was already shown. Without this clear, m_authFailureNotified.contains(id)
    // in onSyncFailedSurface would short-circuit and the user would see
    // "nothing happens" again.
    m_authFailureNotified.remove(nbId);
    m_networkFailureNotified.remove(nbId);
    // Tag for success-feedback so the syncFinished(OK) handler can post a
    // brief status-bar confirmation. Manual click only — auto-sync never
    // populates this set.
    m_pendingManualSyncFeedback.insert(nbId);
    syncSvc->triggerSyncNow(nbId);
    return;
  case SyncState::S1:
  case SyncState::S2:
  case SyncState::S3:
  case SyncState::S4: {
    qCDebug(lcSync)
        << "NotebookExplorer2::onSyncButtonClicked: partial state, opening bootstrap dialog"
        << "notebookId:" << nbId;
    auto *dlg = new NotebookSyncInfoDialog2(m_services, nbId, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setBootstrapMode(true);
    dlg->open();
    return;
  }
  }
}

void NotebookExplorer2::onSyncInfoActionTriggered() {
  const QString nbId = currentNotebookId();
  if (nbId.isEmpty()) {
    return;
  }
  qCDebug(lcSync) << "NotebookExplorer2::onSyncInfoActionTriggered: triggered notebookId:" << nbId;
  auto *dlg = new NotebookSyncInfoDialog2(m_services, nbId, this);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->open();
}

void NotebookExplorer2::maybePromptSyncInfoForMissingPat(const QString &p_notebookId) {
  if (p_notebookId.isEmpty()) {
    return;
  }
  auto *classifier = m_services.get<SyncStateClassifier>();
  if (!classifier) {
    return;
  }
  // Re-confirm the notebook is genuinely in S2 (sync configured on disk, PAT
  // missing). VXCORE_ERR_SYNC_AUTH_FAILED can also be emitted for transient
  // keychain faults where a PAT actually exists; the classify() re-check guards
  // against prompting a notebook that is not actually missing its PAT.
  if (classifier->classify(p_notebookId) != SyncState::S2) {
    qCDebug(lcSync) << "NotebookExplorer2::maybePromptSyncInfoForMissingPat: skip, not S2"
                    << "notebookId:" << p_notebookId;
    return;
  }
  qCDebug(lcSync) << "NotebookExplorer2::maybePromptSyncInfoForMissingPat: S2 detected on open,"
                  << "scheduling bootstrap dialog notebookId:" << p_notebookId;
  // Defer the dialog open to the next event-loop turn. The AUTH_FAILED emit can
  // arrive while OpenNotebookDialog2's nested exec() loop is still unwinding;
  // opening a second dialog re-entrantly on top of the closing modal is fragile,
  // so we let the stack unwind first.
  const QString notebookId = p_notebookId;
  QTimer::singleShot(0, this, [this, notebookId]() {
    auto *dlg = new NotebookSyncInfoDialog2(m_services, notebookId, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setBootstrapMode(true);
    dlg->open();
  });
}

void NotebookExplorer2::updateSyncButtonState() {
  if (!m_syncButton || !m_syncInfoAction) {
    return;
  }

  const QString nbId = currentNotebookId();
  if (nbId.isEmpty()) {
    m_syncButton->setEnabled(false);
    m_syncInfoAction->setEnabled(false);
    m_syncButton->setToolTip(tr("Sync Now"));
    qCDebug(lcSync) << "NotebookExplorer2::updateSyncButtonState: shortCircuit:emptyId";
    return;
  }

  // Check bundled status via BufferService.
  bool bundled = false;
  if (auto *bufferSvc = m_services.get<BufferService>()) {
    bundled = bufferSvc->isNotebookBundled(nbId);
  }
  if (!bundled) {
    m_syncButton->setEnabled(false);
    m_syncInfoAction->setEnabled(false);
    m_syncButton->setToolTip(tr("Sync Now"));
    qCDebug(lcSync)
        << "NotebookExplorer2::updateSyncButtonState: shortCircuit:notBundled notebookId:" << nbId;
    return;
  }

  auto *syncSvc = m_services.get<SyncService>();
  auto *classifier = m_services.get<SyncStateClassifier>();
  const SyncState state = classifier ? classifier->classify(nbId) : SyncState::S0;
  const bool syncInProgress = syncSvc && syncSvc->isSyncInProgress(nbId);

  // Partial states (S1-S4): notebook claims sync is enabled but configuration
  // is incomplete (missing backend, URL, PAT, or runtime registration). The
  // UI surfaces this via the partialSyncConfig QWidget property so the user
  // knows their click will open Configure Sync (not start a sync).
  const bool partialSyncConfig = classifier && classifier->isPartial(state);
  const bool syncEnabled = (state != SyncState::S0 && state != SyncState::S6);
  const bool syncReady = (state == SyncState::S5 || state == SyncState::S7);
  qCDebug(lcSync) << "NotebookExplorer2::updateSyncButtonState: decided notebookId:" << nbId
                  << "state:" << static_cast<int>(state) << "syncEnabled:" << syncEnabled
                  << "syncReady:" << syncReady << "syncInProgress:" << syncInProgress
                  << "partialSyncConfig:" << partialSyncConfig;

  // Wave 12.2 / F5.9: keep the button enabled during in-flight sync so the
  // user can click it to cancel. Previously it was disabled-while-syncing;
  // now it doubles as a Cancel control (onSyncButtonClicked routes to
  // SyncService::cancelSync when isSyncInProgress is true).
  m_syncButton->setEnabled(bundled);

  // Surface partial state via tooltip + dynamic Qt property (no new icon
  // asset). The button click semantics are unchanged.
  m_syncButton->setProperty("partialSyncConfig", partialSyncConfig);

  // Build tooltip, appending reconcile error if present (W4.T3)
  QString tooltip;
  if (!syncEnabled) {
    // W4.T2: S0 affordance is "Enable Sync" (distinct from "Sync Now") so
    // the user understands the click will open the bootstrap dialog rather
    // than start a sync.
    tooltip = tr("Enable sync for this notebook");
  } else if (partialSyncConfig) {
    tooltip = tr("Sync configured but incomplete");
  } else if (syncInProgress) {
    tooltip = tr("Sync in progress");
  } else if (!syncReady) {
    tooltip = tr("Click to bootstrap sync for this notebook.");
  } else {
    tooltip = tr("Sync Now");
  }

  // Append reconcile error to tooltip if present
  if (m_lastReconcileError.contains(nbId)) {
    const int errorCode = m_lastReconcileError.value(nbId);
    // Cosmetic guard: a freshly-cloned no-PAT notebook (S2) legitimately fails
    // reconcile with AUTH_FAILED because it has no token yet. That is the
    // EXPECTED partial state, not an init error to surface -- keep the normal
    // partial tooltip instead of appending the failure line. All other
    // states/errors are unaffected.
    const bool s2AuthExpected =
        (state == SyncState::S2) && (errorCode == static_cast<int>(VXCORE_ERR_SYNC_AUTH_FAILED));
    if (!s2AuthExpected) {
      tooltip += tr("\n\nLast sync init failed: error code %1").arg(errorCode);
    }
  }

  m_syncButton->setToolTip(tooltip);
  // Force style refresh for any QSS rule keyed on the property.
  if (m_syncButton->style()) {
    m_syncButton->style()->unpolish(m_syncButton);
    m_syncButton->style()->polish(m_syncButton);
  }

  // W4.T2: Sync Info menu is enabled for ALL bundled notebooks regardless of
  // syncEnabled (was previously gated to syncEnabled, which trapped S0
  // notebooks - users could not view/edit sync settings to re-enable).
  m_syncInfoAction->setEnabled(bundled);
}

#ifdef VNOTE_TESTING
void NotebookExplorer2::testTriggerNewNotebookCreated(const QString &p_notebookId,
                                                      const QString &p_syncMethod) {
  if (p_syncMethod == QStringLiteral("git") && !p_notebookId.isEmpty()) {
    auto *dlg = new NotebookSyncInfoDialog2(m_services, p_notebookId, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setBootstrapMode(true);
    dlg->open();
  }
}
#endif
