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
#include <QDateTime>
#include <QDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <core/nodeidentifier.h>
#include <widgets/dialogs/sortdialog2.h>

namespace vnotex {

namespace {

inline QString trCtx(const char *p_text) {
  return QCoreApplication::translate("NotebookExplorer2", p_text);
}

// vxcore's FileRecord/FolderRecord default both timestamps to 0 and emit them
// unconditionally, so a "missing" timestamp arrives as the NUMBER zero, not as
// an absent key. Treat a non-numeric value or a value <= 0 as unknown —
// otherwise legacy entries would render as 1970-01-01.
QDateTime readUtcTimestamp(const QJsonObject &p_obj, const QString &p_key) {
  const QJsonValue v = p_obj.value(p_key);
  if (!v.isDouble()) {
    return QDateTime();
  }
  // Range-check BEFORE narrowing: a JSON number is an unbounded double, and a
  // float->integer conversion whose result does not fit the target type is
  // undefined behavior. Anything beyond ~year 286000 is corruption anyway.
  const double raw = v.toDouble();
  if (!(raw > 0.0) || raw > 9.0e15) {
    return QDateTime();
  }
  return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(raw), Qt::UTC);
}

QVector<SortDialog2::Entry> buildEntries(const QJsonArray &p_arr) {
  QVector<SortDialog2::Entry> entries;
  entries.reserve(p_arr.size());
  for (const auto &v : p_arr) {
    const QJsonObject o = v.toObject();
    SortDialog2::Entry entry;
    entry.m_name = o.value(QStringLiteral("name")).toString();
    entry.m_createdUtc = readUtcTimestamp(o, QStringLiteral("createdUtc"));
    entry.m_modifiedUtc = readUtcTimestamp(o, QStringLiteral("modifiedUtc"));
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
                                           const QJsonObject &p_childrenJson, QWidget *p_parent) {
  SortDialogResult result;

  const QJsonArray foldersArr = p_childrenJson.value(QStringLiteral("folders")).toArray();
  const QJsonArray filesArr = p_childrenJson.value(QStringLiteral("files")).toArray();
  const QVector<SortDialog2::Entry> folderEntries = buildEntries(foldersArr);
  const QVector<SortDialog2::Entry> fileEntries = buildEntries(filesArr);
  const QStringList currentFolders = namesOf(folderEntries);
  const QStringList currentFiles = namesOf(fileEntries);

  const QString parentName = p_parentId.relativePath.isEmpty()
                                 ? trCtx("(notebook root)")
                                 : QFileInfo(p_parentId.relativePath).fileName();
  const QString subtitle =
      trCtx("Reorder children of %1. Order is saved to the configuration file.").arg(parentName);

  if (!folderEntries.isEmpty()) {
    SortDialog2 dlg(trCtx("Sort Folders"), subtitle, folderEntries, p_parent);
    dlg.setWindowModality(Qt::WindowModal);
    if (dlg.exec() == QDialog::Accepted) {
      const QStringList chosen = dlg.getSortedOrder();
      if (chosen != currentFolders) {
        result.newFolderOrder = chosen;
      }
    }
  }

  if (!fileEntries.isEmpty()) {
    SortDialog2 dlg(trCtx("Sort Notes"), subtitle, fileEntries, p_parent);
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
