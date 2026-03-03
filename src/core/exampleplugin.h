#ifndef EXAMPLEPLUGIN_H
#define EXAMPLEPLUGIN_H

#include <QObject>

namespace vnotex {

class HookManager;

// ExamplePlugin demonstrates how to use the WordPress-style hook system.
// This serves as a reference implementation for future plugin developers.
//
// Usage:
//   auto *hookMgr = services.get<HookManager>();
//   ExamplePlugin::registerHooks(hookMgr);
//
// The plugin registers handlers for common events:
// - Notebook opening (with optional cancellation)
// - Node activation logging
// - Main window show events
class ExamplePlugin {
public:
  // Register all example hooks with the HookManager.
  // @p_hookMgr: The HookManager instance to register with.
  static void registerHooks(HookManager *p_hookMgr);

  // Unregister all example hooks.
  // Call this when unloading the plugin.
  static void unregisterHooks(HookManager *p_hookMgr);

private:
  // Stored callback IDs for unregistration.
  static int s_notebookOpenId;
  static int s_nodeActivateId;
  static int s_mainWindowShowId;
  static int s_nodeDisplayNameFilterId;
};

} // namespace vnotex

#endif // EXAMPLEPLUGIN_H
