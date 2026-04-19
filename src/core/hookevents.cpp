#include "hookevents.h"

using namespace vnotex;

// ===== NodeOperationEvent =====

QVariantMap NodeOperationEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("notebookId")] = notebookId;
  m[QStringLiteral("relativePath")] = relativePath;
  m[QStringLiteral("isFolder")] = isFolder;
  m[QStringLiteral("name")] = name;
  m[QStringLiteral("operation")] = operation;
  return m;
}

NodeOperationEvent NodeOperationEvent::fromVariantMap(const QVariantMap &p_args) {
  NodeOperationEvent e;
  e.notebookId = p_args.value(QStringLiteral("notebookId")).toString();
  e.relativePath = p_args.value(QStringLiteral("relativePath")).toString();
  e.isFolder = p_args.value(QStringLiteral("isFolder")).toBool();
  e.name = p_args.value(QStringLiteral("name")).toString();
  e.operation = p_args.value(QStringLiteral("operation")).toString();
  return e;
}

// ===== NodeRenameEvent =====

QVariantMap NodeRenameEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("notebookId")] = notebookId;
  m[QStringLiteral("relativePath")] = relativePath;
  m[QStringLiteral("isFolder")] = isFolder;
  m[QStringLiteral("oldName")] = oldName;
  m[QStringLiteral("newName")] = newName;
  return m;
}

NodeRenameEvent NodeRenameEvent::fromVariantMap(const QVariantMap &p_args) {
  NodeRenameEvent e;
  e.notebookId = p_args.value(QStringLiteral("notebookId")).toString();
  e.relativePath = p_args.value(QStringLiteral("relativePath")).toString();
  e.isFolder = p_args.value(QStringLiteral("isFolder")).toBool();
  e.oldName = p_args.value(QStringLiteral("oldName")).toString();
  e.newName = p_args.value(QStringLiteral("newName")).toString();
  return e;
}

// ===== FileOpenEvent =====

QVariantMap FileOpenEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("notebookId")] = notebookId;
  m[QStringLiteral("filePath")] = filePath;
  m[QStringLiteral("bufferId")] = bufferId;
  m[QStringLiteral("mode")] = mode;
  m[QStringLiteral("forceMode")] = forceMode;
  m[QStringLiteral("focus")] = focus;
  m[QStringLiteral("newFile")] = newFile;
  m[QStringLiteral("readOnly")] = readOnly;
  m[QStringLiteral("lineNumber")] = lineNumber;
  m[QStringLiteral("alwaysNewWindow")] = alwaysNewWindow;
  if (!searchPatterns.isEmpty()) {
    m[QStringLiteral("searchPatterns")] = QVariant::fromValue(searchPatterns);
    m[QStringLiteral("searchOptions")] = searchOptions;
    m[QStringLiteral("searchCurrentMatchLine")] = searchCurrentMatchLine;
  }
  return m;
}

FileOpenEvent FileOpenEvent::fromVariantMap(const QVariantMap &p_args) {
  FileOpenEvent e;
  e.notebookId = p_args.value(QStringLiteral("notebookId")).toString();
  e.filePath = p_args.value(QStringLiteral("filePath")).toString();
  e.bufferId = p_args.value(QStringLiteral("bufferId")).toString();
  e.mode = p_args.value(QStringLiteral("mode")).toInt();
  e.forceMode = p_args.value(QStringLiteral("forceMode")).toBool();
  e.focus = p_args.value(QStringLiteral("focus"), true).toBool();
  e.newFile = p_args.value(QStringLiteral("newFile")).toBool();
  e.readOnly = p_args.value(QStringLiteral("readOnly")).toBool();
  e.lineNumber = p_args.value(QStringLiteral("lineNumber"), -1).toInt();
  e.alwaysNewWindow = p_args.value(QStringLiteral("alwaysNewWindow")).toBool();
  e.searchPatterns = p_args.value(QStringLiteral("searchPatterns")).toStringList();
  e.searchOptions = p_args.value(QStringLiteral("searchOptions")).toInt();
  e.searchCurrentMatchLine = p_args.value(QStringLiteral("searchCurrentMatchLine"), -1).toInt();
  return e;
}

// ===== BufferEvent =====

QVariantMap BufferEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("bufferId")] = bufferId;
  return m;
}

BufferEvent BufferEvent::fromVariantMap(const QVariantMap &p_args) {
  BufferEvent e;
  e.bufferId = p_args.value(QStringLiteral("bufferId")).toString();
  return e;
}

// ===== ViewWindowOpenEvent =====

QVariantMap ViewWindowOpenEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("fileType")] = fileType;
  m[QStringLiteral("bufferId")] = bufferId;
  m[QStringLiteral("nodeId")] = nodeId;
  return m;
}

ViewWindowOpenEvent ViewWindowOpenEvent::fromVariantMap(const QVariantMap &p_args) {
  ViewWindowOpenEvent e;
  e.fileType = p_args.value(QStringLiteral("fileType")).toString();
  e.bufferId = p_args.value(QStringLiteral("bufferId")).toString();
  e.nodeId = p_args.value(QStringLiteral("nodeId")).toString();
  return e;
}

// ===== ViewWindowCloseEvent =====

QVariantMap ViewWindowCloseEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("windowId")] = windowId;
  m[QStringLiteral("force")] = force;
  m[QStringLiteral("bufferId")] = bufferId;
  return m;
}

ViewWindowCloseEvent ViewWindowCloseEvent::fromVariantMap(const QVariantMap &p_args) {
  ViewWindowCloseEvent e;
  e.windowId = p_args.value(QStringLiteral("windowId")).toULongLong();
  e.force = p_args.value(QStringLiteral("force")).toBool();
  e.bufferId = p_args.value(QStringLiteral("bufferId")).toString();
  return e;
}

// ===== ViewWindowMoveEvent =====

QVariantMap ViewWindowMoveEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("windowId")] = windowId;
  m[QStringLiteral("srcWorkspaceId")] = srcWorkspaceId;
  m[QStringLiteral("dstWorkspaceId")] = dstWorkspaceId;
  m[QStringLiteral("direction")] = direction;
  m[QStringLiteral("bufferId")] = bufferId;
  return m;
}

ViewWindowMoveEvent ViewWindowMoveEvent::fromVariantMap(const QVariantMap &p_args) {
  ViewWindowMoveEvent e;
  e.windowId = p_args.value(QStringLiteral("windowId")).toULongLong();
  e.srcWorkspaceId = p_args.value(QStringLiteral("srcWorkspaceId")).toString();
  e.dstWorkspaceId = p_args.value(QStringLiteral("dstWorkspaceId")).toString();
  e.direction = p_args.value(QStringLiteral("direction")).toInt();
  e.bufferId = p_args.value(QStringLiteral("bufferId")).toString();
  return e;
}

// ===== ViewSplitCreateEvent =====

QVariantMap ViewSplitCreateEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("workspaceId")] = workspaceId;
  m[QStringLiteral("direction")] = direction;
  m[QStringLiteral("newWorkspaceId")] = newWorkspaceId;
  return m;
}

ViewSplitCreateEvent ViewSplitCreateEvent::fromVariantMap(const QVariantMap &p_args) {
  ViewSplitCreateEvent e;
  e.workspaceId = p_args.value(QStringLiteral("workspaceId")).toString();
  e.direction = p_args.value(QStringLiteral("direction")).toInt();
  e.newWorkspaceId = p_args.value(QStringLiteral("newWorkspaceId")).toString();
  return e;
}

// ===== ViewSplitRemoveEvent =====

QVariantMap ViewSplitRemoveEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("workspaceId")] = workspaceId;
  m[QStringLiteral("keepWorkspace")] = keepWorkspace;
  return m;
}

ViewSplitRemoveEvent ViewSplitRemoveEvent::fromVariantMap(const QVariantMap &p_args) {
  ViewSplitRemoveEvent e;
  e.workspaceId = p_args.value(QStringLiteral("workspaceId")).toString();
  e.keepWorkspace = p_args.value(QStringLiteral("keepWorkspace")).toBool();
  return e;
}

// ===== ViewSplitActivateEvent =====

QVariantMap ViewSplitActivateEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("workspaceId")] = workspaceId;
  return m;
}

ViewSplitActivateEvent ViewSplitActivateEvent::fromVariantMap(const QVariantMap &p_args) {
  ViewSplitActivateEvent e;
  e.workspaceId = p_args.value(QStringLiteral("workspaceId")).toString();
  return e;
}

// ===== TagOperationEvent =====

QVariantMap TagOperationEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("notebookId")] = notebookId;
  m[QStringLiteral("tagName")] = tagName;
  m[QStringLiteral("parentTag")] = parentTag;
  m[QStringLiteral("operation")] = operation;
  return m;
}

TagOperationEvent TagOperationEvent::fromVariantMap(const QVariantMap &p_args) {
  TagOperationEvent e;
  e.notebookId = p_args.value(QStringLiteral("notebookId")).toString();
  e.tagName = p_args.value(QStringLiteral("tagName")).toString();
  e.parentTag = p_args.value(QStringLiteral("parentTag")).toString();
  e.operation = p_args.value(QStringLiteral("operation")).toString();
  return e;
}

// ===== FileTagEvent =====

QVariantMap FileTagEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("notebookId")] = notebookId;
  m[QStringLiteral("filePath")] = filePath;
  m[QStringLiteral("tagName")] = tagName;
  m[QStringLiteral("tagsJson")] = tagsJson;
  m[QStringLiteral("operation")] = operation;
  return m;
}

FileTagEvent FileTagEvent::fromVariantMap(const QVariantMap &p_args) {
  FileTagEvent e;
  e.notebookId = p_args.value(QStringLiteral("notebookId")).toString();
  e.filePath = p_args.value(QStringLiteral("filePath")).toString();
  e.tagName = p_args.value(QStringLiteral("tagName")).toString();
  e.tagsJson = p_args.value(QStringLiteral("tagsJson")).toString();
  e.operation = p_args.value(QStringLiteral("operation")).toString();
  return e;
}

// ===== AttachmentAddEvent =====

QVariantMap AttachmentAddEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("bufferId")] = bufferId;
  m[QStringLiteral("sourcePath")] = sourcePath;
  m[QStringLiteral("filename")] = filename;
  return m;
}

AttachmentAddEvent AttachmentAddEvent::fromVariantMap(const QVariantMap &p_args) {
  AttachmentAddEvent e;
  e.bufferId = p_args.value(QStringLiteral("bufferId")).toString();
  e.sourcePath = p_args.value(QStringLiteral("sourcePath")).toString();
  e.filename = p_args.value(QStringLiteral("filename")).toString();
  return e;
}

// ===== AttachmentDeleteEvent =====

QVariantMap AttachmentDeleteEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("bufferId")] = bufferId;
  m[QStringLiteral("filename")] = filename;
  return m;
}

AttachmentDeleteEvent AttachmentDeleteEvent::fromVariantMap(const QVariantMap &p_args) {
  AttachmentDeleteEvent e;
  e.bufferId = p_args.value(QStringLiteral("bufferId")).toString();
  e.filename = p_args.value(QStringLiteral("filename")).toString();
  return e;
}

// ===== AttachmentRenameEvent =====

QVariantMap AttachmentRenameEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("bufferId")] = bufferId;
  m[QStringLiteral("oldFilename")] = oldFilename;
  m[QStringLiteral("newFilename")] = newFilename;
  return m;
}

AttachmentRenameEvent AttachmentRenameEvent::fromVariantMap(const QVariantMap &p_args) {
  AttachmentRenameEvent e;
  e.bufferId = p_args.value(QStringLiteral("bufferId")).toString();
  e.oldFilename = p_args.value(QStringLiteral("oldFilename")).toString();
  e.newFilename = p_args.value(QStringLiteral("newFilename")).toString();
  return e;
}

// ===== NotebookCloseEvent =====

QVariantMap NotebookCloseEvent::toVariantMap() const {
  QVariantMap m;
  m[QStringLiteral("notebookId")] = notebookId;
  return m;
}

NotebookCloseEvent NotebookCloseEvent::fromVariantMap(const QVariantMap &p_args) {
  NotebookCloseEvent e;
  e.notebookId = p_args.value(QStringLiteral("notebookId")).toString();
  return e;
}

