#include "filesystemviewer.h"

#include <QDebug>
#include <QDir>
#include <QFileSystemModel>
#include <QMenu>
#include <QSharedPointer>
#include <QVBoxLayout>

#include "dialogs/filepropertiesdialog.h"
#include "treeview.h"
#include "widgetsfactory.h"
#include <core/thememgr.h>
#include <core/vnotex.h>
#include <utils/clipboardutils.h>
#include <utils/iconutils.h>
#include <utils/pathutils.h>

using namespace vnotex;

FileSystemViewer::FileSystemViewer(QWidget *p_parent) : QWidget(p_parent) { setupUI(); }

void FileSystemViewer::setupUI() {
  auto mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  auto fileModel = new QFileSystemModel(this);

  m_viewer = new TreeView(this);
  m_viewer->setModel(fileModel);
  m_viewer->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_viewer->setHeaderHidden(true);
  // Show only the Name column.
  for (int i = 1; i < fileModel->columnCount(); ++i) {
    m_viewer->hideColumn(i);
  }

  if (m_contextMenuEnabled) {
    m_viewer->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_viewer, &QTreeView::customContextMenuRequested, this,
            &FileSystemViewer::handleContextMenuRequested);
  }

  connect(m_viewer, &QTreeView::activated, this, [this](const QModelIndex &p_index) {
    if (!this->fileModel()->isDir(p_index)) {
      QStringList files;
      files << this->fileModel()->filePath(p_index);
      emit openFiles(files);
    }
  });
  connect(m_viewer->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &FileSystemViewer::selectionChanged);

  auto index = fileModel->setRootPath(QDir::homePath());
  m_viewer->setRootIndex(index);

  mainLayout->addWidget(m_viewer);

  setFocusProxy(m_viewer);

  connect(m_viewer, &QTreeView::expanded, this, &FileSystemViewer::resizeTreeToContents);
  connect(m_viewer, &QTreeView::collapsed, this, &FileSystemViewer::resizeTreeToContents);
  connect(fileModel, &QFileSystemModel::directoryLoaded, this,
          &FileSystemViewer::resizeTreeToContents);
}

void FileSystemViewer::resizeTreeToContents() { m_viewer->resizeColumnToContents(0); }

QFileSystemModel *FileSystemViewer::fileModel() const {
  return static_cast<QFileSystemModel *>(m_viewer->model());
}

int FileSystemViewer::selectedCount() const {
  return m_viewer->selectionModel()->selectedRows().size();
}

QModelIndex FileSystemViewer::getSelectedIndex() const {
  const auto modelIndexList = m_viewer->selectionModel()->selectedRows();
  return modelIndexList.size() == 1 ? modelIndexList[0] : QModelIndex();
}

void FileSystemViewer::setRootPath(const QString &p_rootPath) {
  auto model = fileModel();
  auto index = model->setRootPath(p_rootPath);
  if (!index.isValid()) {
    qWarning() << "failed to set root path to" << p_rootPath;
    index = model->setRootPath("");
  }

  m_viewer->setRootIndex(index);
  resizeTreeToContents();
}

QString FileSystemViewer::rootPath() const { return fileModel()->rootPath(); }

QStringList FileSystemViewer::getSelectedPaths() const {
  const auto modelIndexList = m_viewer->selectionModel()->selectedRows();
  auto model = fileModel();
  QStringList filePaths;
  for (const auto &index : modelIndexList) {
    filePaths << model->filePath(index);
  }

  return filePaths;
}

void FileSystemViewer::createContextMenuOnItem(QMenu *p_menu) {
  auto act = createAction(Action::Open, p_menu);
  p_menu->addAction(act);

  act = createAction(Action::Delete, p_menu);
  p_menu->addAction(act);

  if (selectedCount() == 1) {
    act = createAction(Action::CopyPath, p_menu);
    p_menu->addAction(act);

    act = createAction(Action::Properties, p_menu);
    p_menu->addAction(act);
  }
}

QAction *FileSystemViewer::createAction(Action p_act, QObject *p_parent) {
  QAction *act = nullptr;
  const auto &themeMgr = VNoteX::getInst().getThemeMgr();
  switch (p_act) {
  case Action::Open:
    act = new QAction(tr("&Open"), p_parent);
    connect(act, &QAction::triggered, this, [this]() {
      auto paths = getSelectedPaths();
      Q_ASSERT(paths.size() > 0);
      emit openFiles(paths);
    });
    break;

  case Action::Delete:
    act = new QAction(tr("&Delete"), p_parent);
    connect(act, &QAction::triggered, this, [this]() {
      auto paths = getSelectedPaths();
      emit removeFiles(paths);
    });
    break;

  case Action::Properties:
    act = new QAction(IconUtils::fetchIconWithDisabledState(themeMgr.getIconFile("properties.svg")),
                      tr("&Properties"), p_parent);
    connect(act, &QAction::triggered, this, [this]() {
      auto paths = getSelectedPaths();
      Q_ASSERT(paths.size() == 1);
      FilePropertiesDialog dialog(paths[0], this);
      int ret = dialog.exec();
      if (ret) {
        auto newName = dialog.getFileName();
        if (newName != PathUtils::fileName(paths[0])) {
          // Rename.
          emit renameFile(paths[0], newName);
        }
      }
    });
    break;

  case Action::CopyPath:
    act = new QAction(tr("Cop&y Path"), p_parent);
    connect(act, &QAction::triggered, this, [this]() {
      auto paths = getSelectedPaths();
      Q_ASSERT(paths.size() == 1);
      ClipboardUtils::setTextToClipboard(paths[0]);
      VNoteX::getInst().showStatusMessageShort(tr("Copied path: %1").arg(paths[0]));
    });
    break;
  }
  return act;
}

void FileSystemViewer::scrollToAndSelect(const QStringList &p_paths) {
  auto selectionModel = m_viewer->selectionModel();
  selectionModel->clear();

  bool isFirst = true;
  auto model = fileModel();
  for (const auto &pa : p_paths) {
    auto index = model->index(pa);
    if (index.isValid()) {
      if (isFirst) {
        m_viewer->scrollTo(index);
        isFirst = false;
      }
      selectionModel->select(index, QItemSelectionModel::SelectCurrent | QItemSelectionModel::Rows);
    }
  }
}

void FileSystemViewer::handleContextMenuRequested(const QPoint &p_pos) {
  QScopedPointer<QMenu> menu(WidgetsFactory::createMenu());

  auto index = m_viewer->indexAt(p_pos);
  if (index.isValid()) {
    auto selectionModel = m_viewer->selectionModel();
    if (!selectionModel->isSelected(index)) {
      // Must select entire row since we use selectedRows() to count.
      selectionModel->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect |
                                                 QItemSelectionModel::Rows);
    }

    m_viewer->update();

    createContextMenuOnItem(menu.data());
  }

  m_viewer->update();

  if (!menu->isEmpty()) {
    menu->exec(m_viewer->mapToGlobal(p_pos));
  }
}
