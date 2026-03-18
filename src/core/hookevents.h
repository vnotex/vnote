#ifndef HOOKEVENTS_H
#define HOOKEVENTS_H

#include <QString>
#include <QVariantMap>

namespace vnotex {

// Typed event struct for NodeBeforeDelete, NodeBeforeMove, NodeAfterMove.
// Replaces raw QVariantMap construction for node delete/move hooks.
struct NodeOperationEvent {
  QString notebookId;
  QString relativePath;
  bool isFolder = false;
  QString name;
  QString operation; // "delete" or "move"

  QVariantMap toVariantMap() const;
  static NodeOperationEvent fromVariantMap(const QVariantMap &p_args);
};

// Typed event struct for NodeBeforeRename, NodeAfterRename.
struct NodeRenameEvent {
  QString notebookId;
  QString relativePath;
  bool isFolder = false;
  QString oldName;
  QString newName;

  QVariantMap toVariantMap() const;
  static NodeRenameEvent fromVariantMap(const QVariantMap &p_args);
};

// Typed event struct for FileBeforeOpen, FileAfterOpen.
struct FileOpenEvent {
  QString notebookId;
  QString filePath;
  QString bufferId;       // empty for BeforeOpen, filled for AfterOpen
  int mode = 0;           // ViewWindowMode enum as int
  bool forceMode = false;
  bool focus = true;
  bool newFile = false;
  bool readOnly = false;
  int lineNumber = -1;
  bool alwaysNewWindow = false;

  QVariantMap toVariantMap() const;
  static FileOpenEvent fromVariantMap(const QVariantMap &p_args);
};

// Typed event struct for FileBeforeSave, FileAfterSave, FileBeforeClose, FileAfterClose.
struct BufferEvent {
  QString bufferId;

  QVariantMap toVariantMap() const;
  static BufferEvent fromVariantMap(const QVariantMap &p_args);
};

// Typed event struct for ViewWindowBeforeOpen, ViewWindowAfterOpen.
struct ViewWindowOpenEvent {
  QString fileType;
  QString bufferId;
  QString nodeId; // relativePath

  QVariantMap toVariantMap() const;
  static ViewWindowOpenEvent fromVariantMap(const QVariantMap &p_args);
};

// Typed event struct for ViewWindowBeforeClose, ViewWindowAfterClose.
struct ViewWindowCloseEvent {
  quint64 windowId = 0;
  bool force = false;
  QString bufferId; // only set for AfterClose

  QVariantMap toVariantMap() const;
  static ViewWindowCloseEvent fromVariantMap(const QVariantMap &p_args);
};

// Typed event struct for ViewWindowBeforeMove, ViewWindowAfterMove.
struct ViewWindowMoveEvent {
  quint64 windowId = 0;
  QString srcWorkspaceId;
  QString dstWorkspaceId;
  int direction = 0; // Direction enum as int
  QString bufferId;

  QVariantMap toVariantMap() const;
  static ViewWindowMoveEvent fromVariantMap(const QVariantMap &p_args);
};

// Typed event struct for ViewSplitBeforeCreate, ViewSplitAfterCreate.
struct ViewSplitCreateEvent {
  QString workspaceId;
  int direction = 0;
  QString newWorkspaceId; // only set for AfterCreate

  QVariantMap toVariantMap() const;
  static ViewSplitCreateEvent fromVariantMap(const QVariantMap &p_args);
};

// Typed event struct for ViewSplitBeforeRemove, ViewSplitAfterRemove.
struct ViewSplitRemoveEvent {
  QString workspaceId;
  bool keepWorkspace = false;

  QVariantMap toVariantMap() const;
  static ViewSplitRemoveEvent fromVariantMap(const QVariantMap &p_args);
};

// Typed event struct for ViewSplitBeforeActivate, ViewSplitAfterActivate.
struct ViewSplitActivateEvent {
  QString workspaceId;

  QVariantMap toVariantMap() const;
  static ViewSplitActivateEvent fromVariantMap(const QVariantMap &p_args);
};

} // namespace vnotex

#endif // HOOKEVENTS_H
