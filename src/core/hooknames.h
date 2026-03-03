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

// Before a file is closed
inline const QLatin1String FileBeforeClose("vnote.file.before_close");

// After a file is closed
inline const QLatin1String FileAfterClose("vnote.file.after_close");

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

// Before the context menu is shown
inline const QLatin1String ContextMenuBeforeShow("vnote.ui.contextmenu.before_show");

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

} // namespace HookNames

} // namespace vnotex

#endif // HOOKNAMES_H
