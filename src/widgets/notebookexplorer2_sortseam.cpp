// T11 (notebook-explorer-drag-reorder): NotebookExplorer2's dialog-orchestration
// logic for onSortRequested. Lives in its own translation unit so widget tests
// can link this code without dragging in the rest of NotebookExplorer2 (which
// transitively pulls in ConfigMgr2, SyncService, ThemeService, BufferService,
// ~20 dialog deps, and the MainWindow shell).
//
// Free function in the `vnotex` namespace — NOT a member of NotebookExplorer2
// — so AUTOMOC in test builds does not generate qt_metacall references to
// every slot in NotebookExplorer2. Literal translation calls use its namespaced
// context so Qt Linguist can extract the dialog text.

#include "notebookexplorer2_sortseam.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QFileInfo>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <core/nodeidentifier.h>
#include <widgets/dialogs/sortdialog2.h>

namespace vnotex {

namespace {

// Legacy records use zero for unknown timestamps, not the Unix epoch.
QDateTime readUtcTimestamp(qint64 p_timestamp) {
  if (p_timestamp <= 0 || p_timestamp > Q_INT64_C(9000000000000000)) {
    return QDateTime();
  }
  return QDateTime::fromMSecsSinceEpoch(p_timestamp, Qt::UTC);
}

QVector<SortDialog2::Entry> buildEntries(const QVector<FolderEntry> &p_children) {
  QVector<SortDialog2::Entry> entries;
  entries.reserve(p_children.size());
  for (const auto &child : p_children) {
    SortDialog2::Entry entry;
    entry.m_name = child.m_name;
    entry.m_createdUtc = readUtcTimestamp(child.m_createdUtc);
    entry.m_modifiedUtc = readUtcTimestamp(child.m_modifiedUtc);
    entries.append(entry);
  }
  return entries;
}

QStringList namesOf(const QVector<SortDialog2::Entry> &p_entries) {
  QStringList names;
  names.reserve(p_entries.size());
  for (const auto &entry : p_entries) {
    names << entry.m_name;
  }
  return names;
}

} // namespace

SortDialogResult runSortDialogsForChildren(const NodeIdentifier &p_parentId,
                                           const NodeIdentifier &p_currentNodeId,
                                           const FolderListing &p_children, QWidget *p_parent) {
  SortDialogResult result;

  const QVector<SortDialog2::Entry> folderEntries = buildEntries(p_children.m_folders);
  const QVector<SortDialog2::Entry> fileEntries = buildEntries(p_children.m_files);
  const QStringList currentFolders = namesOf(folderEntries);
  const QStringList currentFiles = namesOf(fileEntries);

  QString currentName;
  if (p_currentNodeId.isValid() && !p_currentNodeId.isRoot() &&
      p_currentNodeId.notebookId == p_parentId.notebookId &&
      p_currentNodeId.parentPath() == p_parentId.relativePath) {
    currentName = QFileInfo(p_currentNodeId.relativePath).fileName();
  }

  const QString parentName =
      p_parentId.relativePath.isEmpty()
          ? QCoreApplication::translate("vnotex::NotebookExplorer2", "(notebook root)")
          : QStringLiteral("(%1)").arg(QFileInfo(p_parentId.relativePath).fileName());
  const QString subtitle = QCoreApplication::translate(
                               "vnotex::NotebookExplorer2",
                               "Reorder children of %1. Order is saved to the configuration file.")
                               .arg(parentName);

  if (!folderEntries.isEmpty()) {
    SortDialog2 dlg(QCoreApplication::translate("vnotex::NotebookExplorer2", "Sort Folders"),
                    subtitle, folderEntries, currentName, p_parent);
    dlg.setWindowModality(Qt::WindowModal);
    if (dlg.exec() == QDialog::Accepted) {
      const QStringList chosen = dlg.getSortedOrder();
      if (chosen != currentFolders) {
        result.newFolderOrder = chosen;
      }
    }
  }

  if (!fileEntries.isEmpty()) {
    SortDialog2 dlg(QCoreApplication::translate("vnotex::NotebookExplorer2", "Sort Notes"),
                    subtitle, fileEntries, currentName, p_parent);
    dlg.setWindowModality(Qt::WindowModal);
    if (dlg.exec() == QDialog::Accepted) {
      const QStringList chosen = dlg.getSortedOrder();
      if (chosen != currentFiles) {
        result.newFileOrder = chosen;
      }
    }
  }

  return result;
}

} // namespace vnotex
