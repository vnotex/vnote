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

  // Execute all actions registered for a hook.
  // Returns true if any callback cancelled the action (ctx.cancel() was called).
  // If cancelled, downstream processing should be skipped.
  bool doAction(const QString &p_hook, const QVariantMap &p_args = QVariantMap());

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
  template <typename T>
  void insertSorted(QList<T> &p_list, const T &p_entry);

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
