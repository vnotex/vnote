// T11 (notebook-explorer-drag-reorder): NotebookExplorer2's dialog-orchestration
// logic for onSortRequested. Lives in its own translation unit so widget tests
// can link this code without dragging in the rest of NotebookExplorer2 (which
// transitively pulls in ConfigMgr2, SyncService, ThemeService, BufferService,
// ~20 dialog deps, and the MainWindow shell).
//
// Free function in the `vnotex` namespace — NOT a member of NotebookExplorer2
// — so AUTOMOC in test builds does not generate qt_metacall references to
// every slot in NotebookExplorer2. tr-equivalent calls use
// QCoreApplication::translate with the explicit "NotebookExplorer2" context so
// existing translations under that key are preserved.

#include "notebookexplorer2_sortseam.h"

#include <QCoreApplication>
#include <QDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>
#include <QWidget>

#include <core/nodeidentifier.h>
#include <widgets/dialogs/sortdialog2.h>

namespace vnotex {

namespace {

inline QString trCtx(const char *p_text) {
  return QCoreApplication::translate("NotebookExplorer2", p_text);
}

} // namespace

SortDialogResult runSortDialogsForChildren(const NodeIdentifier &p_parentId,
                                           const QJsonObject &p_childrenJson, QWidget *p_parent) {
  SortDialogResult result;

  const QJsonArray foldersArr = p_childrenJson.value(QStringLiteral("folders")).toArray();
  const QJsonArray filesArr = p_childrenJson.value(QStringLiteral("files")).toArray();
  QStringList currentFolders;
  QStringList currentFiles;
  currentFolders.reserve(foldersArr.size());
  for (const auto &v : foldersArr) {
    currentFolders << v.toObject().value(QStringLiteral("name")).toString();
  }
  currentFiles.reserve(filesArr.size());
  for (const auto &v : filesArr) {
    currentFiles << v.toObject().value(QStringLiteral("name")).toString();
  }

  const QString parentName = p_parentId.relativePath.isEmpty()
                                 ? trCtx("(notebook root)")
                                 : QFileInfo(p_parentId.relativePath).fileName();
  const QString subtitle =
      trCtx("Reorder children of %1. Order is saved to the configuration file.").arg(parentName);

  if (!currentFolders.isEmpty()) {
    SortDialog2 dlg(trCtx("Sort Folders"), subtitle, currentFolders, p_parent);
    dlg.setWindowModality(Qt::WindowModal);
    if (dlg.exec() == QDialog::Accepted) {
      const QStringList chosen = dlg.getSortedOrder();
      if (chosen != currentFolders) {
        result.newFolderOrder = chosen;
      }
    }
  }

  if (!currentFiles.isEmpty()) {
    SortDialog2 dlg(trCtx("Sort Notes"), subtitle, currentFiles, p_parent);
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
