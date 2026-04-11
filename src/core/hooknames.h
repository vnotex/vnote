#ifndef HOOKNAMES_H
#define HOOKNAMES_H

#include <QLatin1String>

namespace vnotex {

// Hook name constants for the WordPress-style plugin architecture.
// Hook naming convention: vnote.{module}.{event}
// Use these constants instead of string literals for compile-time checking.

namespace HookNames {

// ===== Notebook Events =====
// Triggered when notebook operations occur

// Before a notebook is opened
inline const QLatin1String NotebookBeforeOpen("vnote.notebook.before_open");

// After a notebook is opened
inline const QLatin1String NotebookAfterOpen("vnote.notebook.after_open");

// Before a notebook is closed
inline const QLatin1String NotebookBeforeClose("vnote.notebook.before_close");

// After a notebook is closed
inline const QLatin1String NotebookAfterClose("vnote.notebook.after_close");

// ===== Node Events =====
// Triggered when node (file/folder) operations occur

// Before a node is activated (selected for viewing)
inline const QLatin1String NodeBeforeActivate("vnote.node.before_activate");

// After a node is activated
inline const QLatin1String NodeAfterActivate("vnote.node.after_activate");

// Before a node is created
inline const QLatin1String NodeBeforeCreate("vnote.node.before_create");

// After a node is created
inline const QLatin1String NodeAfterCreate("vnote.node.after_create");

// Before a node is renamed
inline const QLatin1String NodeBeforeRename("vnote.node.before_rename");

// After a node is renamed
inline const QLatin1String NodeAfterRename("vnote.node.after_rename");

// Before a node is deleted
inline const QLatin1String NodeBeforeDelete("vnote.node.before_delete");

// After a node is deleted
inline const QLatin1String NodeAfterDelete("vnote.node.after_delete");

// Before a node is moved
inline const QLatin1String NodeBeforeMove("vnote.node.before_move");

// After a node is moved
inline const QLatin1String NodeAfterMove("vnote.node.after_move");

// Before a node is copied
inline const QLatin1String NodeBeforeCopy("vnote.node.before_copy");

// After a node is copied
inline const QLatin1String NodeAfterCopy("vnote.node.after_copy");

// ===== File Events =====
// Triggered for file-specific operations

// Before a file is opened in the editor
inline const QLatin1String FileBeforeOpen("vnote.file.before_open");

// After a file is opened in the editor
inline const QLatin1String FileAfterOpen("vnote.file.after_open");

// Before a file is saved
inline const QLatin1String FileBeforeSave("vnote.file.before_save");

// After a file is saved
inline const QLatin1String FileAfterSave("vnote.file.after_save");

// Before a tag is applied to a file
inline const QLatin1String FileBeforeTag("vnote.file.before_tag");

// After a tag is applied to a file
inline const QLatin1String FileAfterTag("vnote.file.after_tag");

// Before a tag is removed from a file
inline const QLatin1String FileBeforeUntag("vnote.file.before_untag");

// After a tag is removed from a file
inline const QLatin1String FileAfterUntag("vnote.file.after_untag");

// ===== Tag Events =====
// Triggered when tag operations occur

// Before a tag is created
inline const QLatin1String TagBeforeCreate("vnote.tag.before_create");

// After a tag is created
inline const QLatin1String TagAfterCreate("vnote.tag.after_create");

// Before a tag is deleted
inline const QLatin1String TagBeforeDelete("vnote.tag.before_delete");

// After a tag is deleted
inline const QLatin1String TagAfterDelete("vnote.tag.after_delete");

// Before a tag is moved (reparented)
inline const QLatin1String TagBeforeMove("vnote.tag.before_move");

// After a tag is moved (reparented)
inline const QLatin1String TagAfterMove("vnote.tag.after_move");

// ===== Folder Events =====
// Triggered for folder-specific operations

// Before a folder is expanded
inline const QLatin1String FolderBeforeExpand("vnote.folder.before_expand");

// After a folder is expanded
inline const QLatin1String FolderAfterExpand("vnote.folder.after_expand");

// Before a folder is collapsed
inline const QLatin1String FolderBeforeCollapse("vnote.folder.before_collapse");

// After a folder is collapsed
inline const QLatin1String FolderAfterCollapse("vnote.folder.after_collapse");

// ===== UI Events =====
// Triggered for UI-related operations

// Before the main window is shown
inline const QLatin1String MainWindowBeforeShow("vnote.ui.mainwindow.before_show");

// After the main window is shown
inline const QLatin1String MainWindowAfterShow("vnote.ui.mainwindow.after_show");

// After the main window has started (post-init complete, ready for heavy work).
// Subscribers should restore session state, load data, etc.
inline const QLatin1String MainWindowAfterStart("vnote.ui.mainwindow.after_start");

// Before the main window is closed (cancellable).
// Subscribers should save session state, persist data, etc.
inline const QLatin1String MainWindowBeforeClose("vnote.ui.mainwindow.before_close");

// Fired when the close operation is cancelled after MainWindowBeforeClose.
// Subscribers should undo any shutdown preparation (e.g., cancel vxcore shutdown).
inline const QLatin1String MainWindowShutdownCancelled("vnote.ui.mainwindow.shutdown_cancelled");

// Before the context menu is shown
inline const QLatin1String ContextMenuBeforeShow("vnote.ui.contextmenu.before_show");

// ===== ViewArea Events =====
// Triggered for view area split/layout operations

// Before a view split is created
inline const QLatin1String ViewSplitBeforeCreate("vnote.viewarea.split.before_create");

// After a view split is created
inline const QLatin1String ViewSplitAfterCreate("vnote.viewarea.split.after_create");

// Before a view split is removed
inline const QLatin1String ViewSplitBeforeRemove("vnote.viewarea.split.before_remove");

// After a view split is removed
inline const QLatin1String ViewSplitAfterRemove("vnote.viewarea.split.after_remove");

// Before the active view split changes
inline const QLatin1String ViewSplitBeforeActivate("vnote.viewarea.split.before_activate");

// After the active view split changes
inline const QLatin1String ViewSplitAfterActivate("vnote.viewarea.split.after_activate");

// ===== ViewWindow Events =====
// Triggered for view window (tab) operations

// Before a view window is opened in a split
inline const QLatin1String ViewWindowBeforeOpen("vnote.viewarea.window.before_open");

// After a view window is opened in a split
inline const QLatin1String ViewWindowAfterOpen("vnote.viewarea.window.after_open");

// Before a view window is closed
inline const QLatin1String ViewWindowBeforeClose("vnote.viewarea.window.before_close");

// After a view window is closed
inline const QLatin1String ViewWindowAfterClose("vnote.viewarea.window.after_close");

// Before a view window is moved between splits
inline const QLatin1String ViewWindowBeforeMove("vnote.viewarea.window.before_move");

// After a view window is moved between splits
inline const QLatin1String ViewWindowAfterMove("vnote.viewarea.window.after_move");

// Before the active view window changes within a split
inline const QLatin1String ViewWindowBeforeActivate("vnote.viewarea.window.before_activate");

// After the active view window changes within a split
inline const QLatin1String ViewWindowAfterActivate("vnote.viewarea.window.after_activate");

// Before view window mode changes (read/edit)
inline const QLatin1String ViewWindowBeforeModeChange("vnote.viewarea.window.before_mode_change");

// After view window mode changes (read/edit)
inline const QLatin1String ViewWindowAfterModeChange("vnote.viewarea.window.after_mode_change");

// ===== Attachment Events =====
// Triggered when attachment operations occur

// Before an attachment is added
inline const QLatin1String AttachmentBeforeAdd("vnote.attachment.before_add");

// After an attachment is added
inline const QLatin1String AttachmentAfterAdd("vnote.attachment.after_add");

// Before an attachment is deleted
inline const QLatin1String AttachmentBeforeDelete("vnote.attachment.before_delete");

// After an attachment is deleted
inline const QLatin1String AttachmentAfterDelete("vnote.attachment.after_delete");

// Before an attachment is renamed
inline const QLatin1String AttachmentBeforeRename("vnote.attachment.before_rename");

// After an attachment is renamed
inline const QLatin1String AttachmentAfterRename("vnote.attachment.after_rename");

// ===== Filter Hooks =====
// Filters transform data and return modified values

// Filter for node display name
inline const QLatin1String FilterNodeDisplayName("vnote.filter.node_display_name");

// Filter for file content before save
inline const QLatin1String FilterFileContentBeforeSave("vnote.filter.file_content_before_save");

// Filter for file content after load
inline const QLatin1String FilterFileContentAfterLoad("vnote.filter.file_content_after_load");

// Filter for context menu items
inline const QLatin1String FilterContextMenuItems("vnote.filter.context_menu_items");

// ===== Config Events =====
// Triggered when configuration changes occur

// After editor configuration is changed and saved.
// Consumers should re-read EditorConfig (and sub-configs) from ConfigMgr2.
// No typed event — simple notification with empty args.
inline const QLatin1String ConfigEditorChanged("vnote.config.editor_changed");

} // namespace HookNames

} // namespace vnotex

#endif // HOOKNAMES_H
