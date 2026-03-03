#ifndef HOOKCONTEXT_H
#define HOOKCONTEXT_H

#include <QMetaType>
#include <QString>
#include <QVariantMap>

namespace vnotex {

// Context object passed to hook callbacks in the WordPress-style hook system.
// Allows callbacks to cancel actions or stop propagation, and share metadata.
// This is a value type (copyable) for passing through filter chains.
struct HookContext {
  Q_GADGET

public:
  // Create a new HookContext with a given hook name
  explicit HookContext(const QString &p_hookName = QString())
      : m_hookName(p_hookName), m_cancelled(false), m_stopPropagation(false) {}

  // Cancel this hook, preventing downstream processing
  void cancel() { m_cancelled = true; }

  // Check if this hook was cancelled
  bool isCancelled() const { return m_cancelled; }

  // Stop propagation to other callbacks
  void stopPropagation() { m_stopPropagation = true; }

  // Check if propagation was stopped
  bool isPropagationStopped() const { return m_stopPropagation; }

  // Set metadata value
  void setMetadata(const QString &p_key, const QVariant &p_value) {
    m_metadata[p_key] = p_value;
  }

  // Get metadata value
  QVariant getMetadata(const QString &p_key, const QVariant &p_defaultValue = QVariant()) const {
    return m_metadata.value(p_key, p_defaultValue);
  }

  // Get all metadata
  const QVariantMap &metadata() const { return m_metadata; }

  // Get hook name
  const QString &hookName() const { return m_hookName; }

private:
  QString m_hookName;
  bool m_cancelled;
  bool m_stopPropagation;
  QVariantMap m_metadata;
};

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::HookContext)

#endif // HOOKCONTEXT_H
