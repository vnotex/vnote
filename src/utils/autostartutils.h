#ifndef AUTOSTARTUTILS_H
#define AUTOSTARTUTILS_H

#include <QString>

namespace vnotex {

// What reconciling a given intent against a given registry state requires.
enum class AutoStartAction { None, Write, Remove };

// Launch-on-login registration. Effected on Windows only, via the per-user Run key.
class AutoStartUtils {
public:
  AutoStartUtils() = delete;

  // True on Windows only.
  static bool isSupported();

  // The exact value data VNote would write: the quoted native application file path.
  // Computed on every platform, so decide() stays platform-agnostic.
  static QString expectedCommand();

  // Pure decision helper. No registry access, no platform ifdef.
  // p_present distinguishes "absent" from "present but empty".
  static AutoStartAction decide(bool p_intent, bool p_present, const QString &p_current,
                                const QString &p_expected);

  // Existence only. Do NOT use this to decide whether a removal is needed; reconcile()
  // compares against registeredCommand() instead.
  static bool hasRegisteredCommand();

  static QString registeredCommand();

  // Write or remove the value. Returns false on failure.
  static bool setEnabled(bool p_enabled);

  // Config-is-intent reconcile, both directions. Returns false only when an action was
  // required and the registry write/remove failed.
  static bool reconcile(bool p_intent);
};

} // namespace vnotex

#endif // AUTOSTARTUTILS_H
