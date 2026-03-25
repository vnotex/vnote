#include "hookmanager.h"

#include <QDebug>

#include <algorithm>

using namespace vnotex;

HookManager::HookManager(QObject *p_parent) : QObject(p_parent) {}

HookManager::~HookManager() = default;

// ===== Actions =====

int HookManager::addAction(const QString &p_hook, ActionCallback p_callback, int p_priority) {
  if (!p_callback) {
    qWarning() << "HookManager::addAction: null callback for hook" << p_hook;
    return -1;
  }

  ActionEntry entry;
  entry.id = m_nextId++;
  entry.priority = p_priority;
  entry.callback = std::move(p_callback);

  insertSorted(m_actions[p_hook], entry);
  return entry.id;
}

bool HookManager::removeAction(int p_id) {
  for (auto it = m_actions.begin(); it != m_actions.end(); ++it) {
    QList<ActionEntry> &list = it.value();
    for (int i = 0; i < list.size(); ++i) {
      if (list[i].id == p_id) {
        list.removeAt(i);
        // Clean up empty lists.
        if (list.isEmpty()) {
          m_actions.erase(it);
        }
        return true;
      }
    }
  }
  return false;
}

bool HookManager::doAction(const QString &p_hook) {
  return doAction(p_hook, QVariantMap());
}

bool HookManager::doAction(const QString &p_hook, const QVariantMap &p_args) {
  // Recursion guard.
  if (m_recursionDepth >= c_maxRecursionDepth) {
    qWarning() << "HookManager::doAction: max recursion depth reached for hook" << p_hook;
    emit actionError(p_hook, "Max recursion depth exceeded");
    return false;
  }

  auto it = m_actions.constFind(p_hook);
  if (it == m_actions.constEnd()) {
    return false; // No actions registered, not cancelled.
  }

  // Take a copy of the list in case callbacks modify registration.
  const QList<ActionEntry> callbacks = it.value();

  HookContext ctx(p_hook);
  ++m_recursionDepth;

  for (const ActionEntry &entry : callbacks) {
    try {
      entry.callback(ctx, p_args);
    } catch (const std::exception &e) {
      qWarning() << "HookManager::doAction: exception in hook" << p_hook << ":" << e.what();
      emit actionError(p_hook, QString::fromUtf8(e.what()));
    } catch (...) {
      qWarning() << "HookManager::doAction: unknown exception in hook" << p_hook;
      emit actionError(p_hook, "Unknown exception");
    }

    // Stop propagation if requested.
    if (ctx.isPropagationStopped()) {
      break;
    }
  }

  --m_recursionDepth;
  return ctx.isCancelled();
}

// ===== Typed Actions (emission) =====

bool HookManager::doAction(const QString &p_hook, const NodeOperationEvent &p_event) {
  return doAction(p_hook, p_event.toVariantMap());
}

bool HookManager::doAction(const QString &p_hook, const NodeRenameEvent &p_event) {
  return doAction(p_hook, p_event.toVariantMap());
}

bool HookManager::doAction(const QString &p_hook, const FileOpenEvent &p_event) {
  return doAction(p_hook, p_event.toVariantMap());
}

bool HookManager::doAction(const QString &p_hook, const BufferEvent &p_event) {
  return doAction(p_hook, p_event.toVariantMap());
}

bool HookManager::doAction(const QString &p_hook, const ViewWindowOpenEvent &p_event) {
  return doAction(p_hook, p_event.toVariantMap());
}

bool HookManager::doAction(const QString &p_hook, const ViewWindowCloseEvent &p_event) {
  return doAction(p_hook, p_event.toVariantMap());
}

bool HookManager::doAction(const QString &p_hook, const ViewWindowMoveEvent &p_event) {
  return doAction(p_hook, p_event.toVariantMap());
}

bool HookManager::doAction(const QString &p_hook, const ViewSplitCreateEvent &p_event) {
  return doAction(p_hook, p_event.toVariantMap());
}

bool HookManager::doAction(const QString &p_hook, const ViewSplitRemoveEvent &p_event) {
  return doAction(p_hook, p_event.toVariantMap());
}

bool HookManager::doAction(const QString &p_hook, const ViewSplitActivateEvent &p_event) {
  return doAction(p_hook, p_event.toVariantMap());
}

bool HookManager::doAction(const QString &p_hook, const TagOperationEvent &p_event) {
  return doAction(p_hook, p_event.toVariantMap());
}

bool HookManager::doAction(const QString &p_hook, const FileTagEvent &p_event) {
  return doAction(p_hook, p_event.toVariantMap());
}

// ===== Filters =====

int HookManager::addFilter(const QString &p_hook, FilterCallback p_callback, int p_priority) {
  if (!p_callback) {
    qWarning() << "HookManager::addFilter: null callback for hook" << p_hook;
    return -1;
  }

  FilterEntry entry;
  entry.id = m_nextId++;
  entry.priority = p_priority;
  entry.callback = std::move(p_callback);

  insertSorted(m_filters[p_hook], entry);
  return entry.id;
}

bool HookManager::removeFilter(int p_id) {
  for (auto it = m_filters.begin(); it != m_filters.end(); ++it) {
    QList<FilterEntry> &list = it.value();
    for (int i = 0; i < list.size(); ++i) {
      if (list[i].id == p_id) {
        list.removeAt(i);
        // Clean up empty lists.
        if (list.isEmpty()) {
          m_filters.erase(it);
        }
        return true;
      }
    }
  }
  return false;
}

QVariant HookManager::applyFilters(const QString &p_hook, const QVariant &p_value,
                                   const QVariantMap &p_context) {
  // Recursion guard.
  if (m_recursionDepth >= c_maxRecursionDepth) {
    qWarning() << "HookManager::applyFilters: max recursion depth reached for hook" << p_hook;
    emit filterError(p_hook, "Max recursion depth exceeded");
    return p_value;
  }

  auto it = m_filters.constFind(p_hook);
  if (it == m_filters.constEnd()) {
    return p_value; // No filters registered, return original value.
  }

  // Take a copy of the list in case callbacks modify registration.
  const QList<FilterEntry> callbacks = it.value();

  QVariant result = p_value;
  ++m_recursionDepth;

  for (const FilterEntry &entry : callbacks) {
    try {
      result = entry.callback(result, p_context);
    } catch (const std::exception &e) {
      qWarning() << "HookManager::applyFilters: exception in hook" << p_hook << ":" << e.what();
      emit filterError(p_hook, QString::fromUtf8(e.what()));
      // Continue with previous result on error.
    } catch (...) {
      qWarning() << "HookManager::applyFilters: unknown exception in hook" << p_hook;
      emit filterError(p_hook, "Unknown exception");
    }
  }

  --m_recursionDepth;
  return result;
}

// ===== Introspection =====

QStringList HookManager::registeredHooks() const {
  QSet<QString> hooks;

  for (auto it = m_actions.constBegin(); it != m_actions.constEnd(); ++it) {
    hooks.insert(it.key());
  }

  for (auto it = m_filters.constBegin(); it != m_filters.constEnd(); ++it) {
    hooks.insert(it.key());
  }

  return hooks.values();
}

int HookManager::callbackCount(const QString &p_hook) const {
  return actionCount(p_hook) + filterCount(p_hook);
}

bool HookManager::hasCallbacks(const QString &p_hook) const {
  return m_actions.contains(p_hook) || m_filters.contains(p_hook);
}

int HookManager::actionCount(const QString &p_hook) const {
  auto it = m_actions.constFind(p_hook);
  return (it != m_actions.constEnd()) ? it.value().size() : 0;
}

int HookManager::filterCount(const QString &p_hook) const {
  auto it = m_filters.constFind(p_hook);
  return (it != m_filters.constEnd()) ? it.value().size() : 0;
}

// ===== Private =====

template <typename T>
void HookManager::insertSorted(QList<T> &p_list, const T &p_entry) {
  // Find insertion point to maintain stable sort by priority.
  // Lower priority = earlier execution, so insert before first entry with higher priority.
  auto insertPos =
      std::upper_bound(p_list.begin(), p_list.end(), p_entry,
                       [](const T &a, const T &b) { return a.priority < b.priority; });
  p_list.insert(insertPos, p_entry);
}

// Explicit template instantiations.
template void HookManager::insertSorted<HookManager::ActionEntry>(QList<ActionEntry> &,
                                                                  const ActionEntry &);
template void HookManager::insertSorted<HookManager::FilterEntry>(QList<FilterEntry> &,
                                                                  const FilterEntry &);
