#include "exampleplugin.h"

#include <QDebug>

#include "hooknames.h"
#include "services/hookmanager.h"

using namespace vnotex;

// Static member initialization
int ExamplePlugin::s_notebookOpenId = -1;
int ExamplePlugin::s_nodeActivateId = -1;
int ExamplePlugin::s_mainWindowShowId = -1;
int ExamplePlugin::s_nodeDisplayNameFilterId = -1;

void ExamplePlugin::registerHooks(HookManager *p_hookMgr) {
  if (!p_hookMgr) {
    return;
  }

  // Example 1: Log notebook opening events
  // Priority 10 (default) - runs in normal order
  s_notebookOpenId = p_hookMgr->addAction(
      HookNames::NotebookBeforeOpen,
      [](HookContext &p_ctx, const QVariantMap &p_args) {
        QString notebookId = p_args.value(QStringLiteral("notebookId")).toString();
        qDebug() << "[ExamplePlugin] Notebook opening:" << notebookId;

        // Example: Cancel opening for specific notebooks (commented out)
        // if (notebookId == "blocked-notebook-id") {
        //   p_ctx.setCancelled(true);
        //   qDebug() << "[ExamplePlugin] Blocked notebook opening";
        // }
      },
      10);

  // Example 2: Log node activation events
  // Priority 5 - runs early to log before other handlers
  s_nodeActivateId = p_hookMgr->addAction(
      HookNames::NodeBeforeActivate,
      [](HookContext &p_ctx, const QVariantMap &p_args) {
        Q_UNUSED(p_ctx);
        QString notebookId = p_args.value(QStringLiteral("notebookId")).toString();
        QString relativePath = p_args.value(QStringLiteral("relativePath")).toString();
        qDebug() << "[ExamplePlugin] Node activating:" << notebookId << "/" << relativePath;
      },
      5);

  // Example 3: Log main window show events
  s_mainWindowShowId = p_hookMgr->addAction(
      HookNames::MainWindowAfterShow,
      [](HookContext &p_ctx, const QVariantMap &p_args) {
        Q_UNUSED(p_ctx);
        Q_UNUSED(p_args);
        qDebug() << "[ExamplePlugin] Main window shown";
      },
      10);

  // Example 4: Filter to transform node display names
  // This demonstrates how filters can modify data
  s_nodeDisplayNameFilterId = p_hookMgr->addFilter(
      HookNames::FilterNodeDisplayName,
      [](const QVariant &p_value, const QVariantMap &p_context) -> QVariant {
        Q_UNUSED(p_context);
        QString name = p_value.toString();

        // Example: Add emoji prefix to markdown files
        if (name.endsWith(QStringLiteral(".md"))) {
          // Commented out to avoid modifying default behavior
          // return QStringLiteral("\xF0\x9F\x93\x84 ") + name;  // document emoji
        }

        return p_value; // Return unchanged
      },
      10);

  qDebug() << "[ExamplePlugin] Hooks registered";
}

void ExamplePlugin::unregisterHooks(HookManager *p_hookMgr) {
  if (!p_hookMgr) {
    return;
  }

  if (s_notebookOpenId >= 0) {
    p_hookMgr->removeAction(s_notebookOpenId);
    s_notebookOpenId = -1;
  }

  if (s_nodeActivateId >= 0) {
    p_hookMgr->removeAction(s_nodeActivateId);
    s_nodeActivateId = -1;
  }

  if (s_mainWindowShowId >= 0) {
    p_hookMgr->removeAction(s_mainWindowShowId);
    s_mainWindowShowId = -1;
  }

  if (s_nodeDisplayNameFilterId >= 0) {
    p_hookMgr->removeFilter(s_nodeDisplayNameFilterId);
    s_nodeDisplayNameFilterId = -1;
  }

  qDebug() << "[ExamplePlugin] Hooks unregistered";
}
