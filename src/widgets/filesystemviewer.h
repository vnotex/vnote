#ifndef FILESYSTEMVIEWER_H
#define FILESYSTEMVIEWER_H

#include <QModelIndex>
#include <QWidget>

class QTreeView;
class QFileSystemModel;
class QMenu;

namespace vnotex {
class FileSystemViewer : public QWidget {
  Q_OBJECT
public:
  explicit FileSystemViewer(QWidget *p_parent = nullptr);

  int selectedCount() const;

  void setRootPath(const QString &p_rootPath);

  QString rootPath() const;

  QStringList getSelectedPaths() const;

  void scrollToAndSelect(const QStringList &p_paths);

signals:
  void renameFile(const QString &p_path, const QString &p_name);

  // Should detect children relationship.
  void removeFiles(const QStringList &p_paths);

  void openFiles(const QStringList &p_paths);

  void selectionChanged();

private slots:
  // Resize the first column.
  void resizeTreeToContents();

  void handleContextMenuRequested(const QPoint &p_pos);

private:
  enum Action { Open, Delete, Properties, CopyPath };

  void setupUI();

  QFileSystemModel *fileModel() const;

  // Valid only when there is only one selected index.
  QModelIndex getSelectedIndex() const;

  void createContextMenuOnItem(QMenu *p_menu);

  QAction *createAction(Action p_act, QObject *p_parent);

  // Managed by QObject.
  QTreeView *m_viewer = nullptr;

  bool m_contextMenuEnabled = false;
};
} // namespace vnotex

#endif // FILESYSTEMVIEWER_H
