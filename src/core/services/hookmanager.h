#ifndef HOOKMANAGER_H
#define HOOKMANAGER_H

#include <QHash>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <functional>

#include "core/hookcontext.h"
#include "core/hookevents.h"
#include "core/noncopyable.h"

namespace vnotex {

// WordPress-style hook manager for plugin architecture.
// Provides Actions (cancellable notifications) and Filters (data transformation).
// Thread safety: Single-threaded only (main Qt thread).
class HookManager : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Callback type for actions: receives context (for cancellation) and arguments.
  using ActionCallback = std::function<void(HookContext &, const QVariantMap &)>;

  // Callback type for filters: transforms value with optional context.
  using FilterCallback = std::function<QVariant(const QVariant &, const QVariantMap &)>;

  // Maximum recursion depth to prevent infinite loops.
  static constexpr int c_maxRecursionDepth = 10;

  explicit HookManager(QObject *p_parent = nullptr);
  ~HookManager() override;

  // ===== Actions =====

  // Register an action callback for a hook.
  // Returns unique ID for removal. Lower priority = earlier execution.
  int addAction(const QString &p_hook, ActionCallback p_callback, int p_priority = 10);

  // Remove an action by its unique ID.
  // Returns true if action was found and removed.
  bool removeAction(int p_id);

  // Execute all actions registered for a hook (no arguments).
  // Returns true if any callback cancelled the action (ctx.cancel() was called).
  // If cancelled, downstream processing should be skipped.
  bool doAction(const QString &p_hook);

  // Execute all actions registered for a hook with raw QVariantMap arguments.
  // Returns true if any callback cancelled the action (ctx.cancel() was called).
  // If cancelled, downstream processing should be skipped.
  bool doAction(const QString &p_hook, const QVariantMap &p_args);

  // Same as above, but also hands the caller the HookContext the callbacks ran
  // against, so metadata a handler set (e.g. a cancellation reason) can reach
  // production code in-band instead of being discarded.
  //
  // The context is constructed INTERNALLY with the correct hook name (a
  // caller-built HookContext would carry an empty hookName(), and there is no
  // setter). *p_outCtx is overwritten on EVERY return path - including the
  // recursion-guard bail-out and the no-callbacks-registered path - so stale
  // metadata from a previous call can never leak through.
  //
  // Metadata precedence: all callbacks for a hook share ONE context, invoked in
  // priority order, so a later handler CAN overwrite an earlier handler's key
  // (last writer wins). Handlers that want first-non-empty-wins semantics must
  // check getMetadata(key) before setting - see SyncService's
  // NotebookBeforeClose handler for the reference implementation.
  bool doAction(const QString &p_hook, const QVariantMap &p_args, HookContext *p_outCtx);

  // ===== Typed Actions (emission) =====
  // Overloads that accept typed event structs. Each calls toVariantMap() then
  // delegates to the raw QVariantMap doAction.

  bool doAction(const QString &p_hook, const NodeOperationEvent &p_event);
  bool doAction(const QString &p_hook, const NodeRenameEvent &p_event);
  bool doAction(const QString &p_hook, const NodeMoveEvent &p_event);
  bool doAction(const QString &p_hook, const NodeTransferEvent &p_event);
  bool doAction(const QString &p_hook, const NodeReorderEvent &p_event);
  bool doAction(const QString &p_hook, const FileOpenEvent &p_event);
  bool doAction(const QString &p_hook, const BufferEvent &p_event);
  bool doAction(const QString &p_hook, const ViewWindowOpenEvent &p_event);
  bool doAction(const QString &p_hook, const ViewWindowCloseEvent &p_event);
  bool doAction(const QString &p_hook, const ViewWindowMoveEvent &p_event);
  bool doAction(const QString &p_hook, const ViewSplitCreateEvent &p_event);
  bool doAction(const QString &p_hook, const ViewSplitRemoveEvent &p_event);
  bool doAction(const QString &p_hook, const ViewSplitActivateEvent &p_event);
  bool doAction(const QString &p_hook, const TagOperationEvent &p_event);
  bool doAction(const QString &p_hook, const FileTagEvent &p_event);
  bool doAction(const QString &p_hook, const AttachmentAddEvent &p_event);
  bool doAction(const QString &p_hook, const AttachmentDeleteEvent &p_event);
  bool doAction(const QString &p_hook, const AttachmentRenameEvent &p_event);
  bool doAction(const QString &p_hook, const NotebookCloseEvent &p_event);
  bool doAction(const QString &p_hook, const NotebookOpenEvent &p_event);
  bool doAction(const QString &p_hook, const NotebookCloneEvent &p_event);
  bool doAction(const QString &p_hook, const ThemeSwitchEvent &p_event);
  bool doAction(const QString &p_hook, const FileExternalChangeEvent &p_event);
  bool doAction(const QString &p_hook, const ImageHostUploadEvent &p_event);
  bool doAction(const QString &p_hook, const ImageHostRemoveEvent &p_event);
  bool doAction(const QString &p_hook, const SyncCancelledEvent &p_event);

  // Typed forwarding overload with a HookContext out-param. Keeps
  // NotebookCoreService::closeNotebook on the typed API instead of forcing a
  // manual QVariantMap conversion just to observe the cancellation reason.
  bool doAction(const QString &p_hook, const NotebookCloseEvent &p_event, HookContext *p_outCtx);

  // ===== Typed Actions (subscription) =====
  // Template adapter: wraps a typed callback into the raw QVariantMap callback.
  // The adapter deserializes QVariantMap -> EventT before calling the user's callback.
  template <typename EventT>
  int addAction(const QString &p_hookName,
                std::function<void(HookContext &, const EventT &)> p_callback,
                int p_priority = 10) {
    return addAction(
        p_hookName,
        [cb = std::move(p_callback)](HookContext &p_ctx, const QVariantMap &p_args) {
          cb(p_ctx, EventT::fromVariantMap(p_args));
        },
        p_priority);
  }

  // ===== Filters =====

  // Register a filter callback for a hook.
  // Returns unique ID for removal. Lower priority = earlier execution.
  int addFilter(const QString &p_hook, FilterCallback p_callback, int p_priority = 10);

  // Remove a filter by its unique ID.
  // Returns true if filter was found and removed.
  bool removeFilter(int p_id);

  // Apply all filters to a value.
  // Filters are executed in priority order, each receiving the previous result.
  QVariant applyFilters(const QString &p_hook, const QVariant &p_value,
                        const QVariantMap &p_context = QVariantMap());

  // ===== Introspection =====

  // Get list of all registered hook names (both actions and filters).
  QStringList registeredHooks() const;

  // Get count of callbacks registered for a hook (actions + filters).
  int callbackCount(const QString &p_hook) const;

  // Check if a hook has any registered callbacks.
  bool hasCallbacks(const QString &p_hook) const;

  // Get count of actions registered for a hook.
  int actionCount(const QString &p_hook) const;

  // Get count of filters registered for a hook.
  int filterCount(const QString &p_hook) const;

signals:
  // Emitted when an action callback throws an exception.
  // Hook system catches and isolates errors to prevent app crash.
  void actionError(const QString &p_hook, const QString &p_error);

  // Emitted when a filter callback throws an exception.
  void filterError(const QString &p_hook, const QString &p_error);

private:
  struct ActionEntry {
    int id;
    int priority;
    ActionCallback callback;
  };

  struct FilterEntry {
    int id;
    int priority;
    FilterCallback callback;
  };

  // Insert entry into sorted list (by priority, stable).
  template <typename T> void insertSorted(QList<T> &p_list, const T &p_entry);

  // Next unique ID for callbacks.
  int m_nextId = 1;

  // Current recursion depth (to detect circular hooks).
  int m_recursionDepth = 0;

  // Registered actions: hook name -> sorted list of callbacks.
  QHash<QString, QList<ActionEntry>> m_actions;

  // Registered filters: hook name -> sorted list of callbacks.
  QHash<QString, QList<FilterEntry>> m_filters;
};

} // namespace vnotex

#endif // HOOKMANAGER_H
