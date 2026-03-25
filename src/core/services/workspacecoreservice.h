#ifndef WORKSPACECORESERVICE_H
#define WORKSPACECORESERVICE_H

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <core/hookevents.h>
#include <core/noncopyable.h>

#include <vxcore/vxcore.h>
#include <vxcore/vxcore_types.h>

namespace vnotex {

class HookManager;

// Service layer for workspace operations. Wraps VxCore workspace C API and provides Qt-friendly interface.
// WorkspaceCoreService manages workspaces - creating, deleting, retrieving, and managing buffer associations.
// Also owns the hook contract for ViewArea/ViewWindow/ViewSplit events.
class WorkspaceCoreService : public QObject, private Noncopyable {
  Q_OBJECT

public:
  // Constructor receives VxCore context handle via dependency injection.
  explicit WorkspaceCoreService(VxCoreContextHandle p_context, QObject *p_parent = nullptr);
  ~WorkspaceCoreService();

  // Set HookManager for firing workspace/view operation hooks.
  // Called from main() after both services are constructed.
  void setHookManager(HookManager *p_hookMgr);

  // ============ Workspace Lifecycle ============

  // Create a new workspace with the given name.
  // Returns workspace ID, or empty string on failure.
  QString createWorkspace(const QString &p_name);

  // Delete a workspace by ID.
  bool deleteWorkspace(const QString &p_id);

  // Get workspace configuration as JSON.
  QJsonObject getWorkspace(const QString &p_id) const;

  // List all workspaces as JSON array.
  QJsonArray listWorkspaces() const;

  // Rename a workspace.
  bool renameWorkspace(const QString &p_id, const QString &p_name);

  // ============ Current Workspace ============

  // Get the ID of the current workspace.
  QString getCurrentWorkspace() const;

  // Set the current workspace.
  bool setCurrentWorkspace(const QString &p_id);

  // ============ Workspace Buffer Management ============

  // Add a buffer to a workspace.
  bool addBuffer(const QString &p_workspaceId, const QString &p_bufferId);

  // Remove a buffer from a workspace.
  bool removeBuffer(const QString &p_workspaceId, const QString &p_bufferId);

  // Set the current buffer in a workspace.
  bool setCurrentBuffer(const QString &p_workspaceId, const QString &p_bufferId);

  // Set the buffer order in a workspace.
  // @p_bufferIds: ordered list of buffer IDs. Only IDs already in the workspace are kept.
  bool setBufferOrder(const QString &p_workspaceId, const QStringList &p_bufferIds);

  // Set workspace metadata (arbitrary JSON object).
  bool setWorkspaceMetadata(const QString &p_workspaceId, const QJsonObject &p_metadata);

  // Get per-buffer metadata within a workspace.
  // Returns JSON object with buffer-specific metadata, or empty object if not found.
  QJsonObject getBufferMetadata(const QString &p_workspaceId, const QString &p_bufferId) const;

  // Set per-buffer metadata within a workspace.
  bool setBufferMetadata(const QString &p_workspaceId, const QString &p_bufferId,
                         const QJsonObject &p_metadata);

  // ============ Hook Firing (ViewArea events) ============
  // These methods own the hook contract for view area operations.
  // Controllers call these instead of firing hooks directly.

  // Fire ViewWindowBeforeOpen. Returns true if cancelled.
  bool fireViewWindowBeforeOpen(const ViewWindowOpenEvent &p_event);

  // Fire ViewWindowAfterOpen.
  void fireViewWindowAfterOpen(const ViewWindowOpenEvent &p_event);

  // Fire ViewWindowBeforeClose. Returns true if cancelled.
  bool fireViewWindowBeforeClose(const ViewWindowCloseEvent &p_event);

  // Fire ViewWindowAfterClose.
  void fireViewWindowAfterClose(const ViewWindowCloseEvent &p_event);

  // Fire ViewWindowBeforeMove. Returns true if cancelled.
  bool fireViewWindowBeforeMove(const ViewWindowMoveEvent &p_event);

  // Fire ViewWindowAfterMove.
  void fireViewWindowAfterMove(const ViewWindowMoveEvent &p_event);

  // Fire ViewSplitBeforeCreate. Returns true if cancelled.
  bool fireViewSplitBeforeCreate(const ViewSplitCreateEvent &p_event);

  // Fire ViewSplitAfterCreate.
  void fireViewSplitAfterCreate(const ViewSplitCreateEvent &p_event);

  // Fire ViewSplitBeforeRemove. Returns true if cancelled.
  bool fireViewSplitBeforeRemove(const ViewSplitRemoveEvent &p_event);

  // Fire ViewSplitAfterRemove.
  void fireViewSplitAfterRemove(const ViewSplitRemoveEvent &p_event);

  // Fire ViewSplitBeforeActivate. Returns true if cancelled.
  bool fireViewSplitBeforeActivate(const ViewSplitActivateEvent &p_event);

  // Fire ViewSplitAfterActivate.
  void fireViewSplitAfterActivate(const ViewSplitActivateEvent &p_event);

private:
  // Check context validity before operations.
  bool checkContext() const;

  // Convert C string from vxcore and free it.
  static QString cstrToQString(char *p_str);

  // Parse JSON string to QJsonObject from C string (takes ownership, frees p_str).
  static QJsonObject parseJsonObjectFromCStr(char *p_str);

  // Parse JSON string to QJsonArray from C string (takes ownership, frees p_str).
  static QJsonArray parseJsonArrayFromCStr(char *p_str);

  VxCoreContextHandle m_context = nullptr;
  HookManager *m_hookMgr = nullptr;
};

} // namespace vnotex

#endif // WORKSPACECORESERVICE_H
