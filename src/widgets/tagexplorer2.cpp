#include "tagexplorer2.h"

#include <QApplication>
#include <QClipboard>
#include <QDataStream>
#include <QDesktopServices>
#include <QFileInfo>
#include <QIODevice>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QSplitter>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <controllers/tagcontroller.h>
#include <core/services/notebookcoreservice.h>
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <models/inodelistmodel.h>
#include <models/tagfilemodel.h>
#include <models/tagmodel.h>
#include <views/filelistview.h>
#include <views/filenodedelegate.h>
#include <views/tagview.h>
#include <widgets/titlebar.h>

using namespace vnotex;

TagExplorer2::TagExplorer2(ServiceLocator &p_services, QWidget *p_parent)
    : QFrame(p_parent), m_services(p_services) {
  setupUI();
}

void TagExplorer2::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(0);

  // Title bar
  setupTitleBar();
  mainLayout->addWidget(m_titleBar);

  // MVC components
  m_tagModel = new TagModel(m_services, this);
  m_tagView = new TagView(this);
  m_tagView->setModel(m_tagModel);
  m_tagController = new TagController(m_services, this);

  // File list panel
  m_fileModel = new TagFileModel(this);
  m_fileView = new FileListView(this);
  m_fileView->setModel(m_fileModel);
  // Disable drag & drop for tag file list
  m_fileView->setDragEnabled(false);
  m_fileView->setAcceptDrops(false);
  m_fileView->setDragDropMode(QAbstractItemView::NoDragDrop);

  m_fileDelegate = new FileNodeDelegate(m_services, this);
  m_fileView->setItemDelegate(m_fileDelegate);

  // Splitter: tag tree on top, node list on bottom
  m_splitter = new QSplitter(Qt::Vertical, this);
  m_splitter->addWidget(m_tagView);
  m_splitter->addWidget(m_fileView);
  mainLayout->addWidget(m_splitter, 1);

  // Wire signals

  // 1. TagView selection → TagController
  connect(m_tagView, &TagView::tagsSelectionChanged,
          m_tagController, &TagController::onTagsSelected);

  // 2. TagController matching nodes → file model
  connect(m_tagController, &TagController::matchingNodesChanged,
          this, &TagExplorer2::onMatchingNodesChanged);

  // 3. TagController GUI requests → TagExplorer2 dialog handlers
  connect(m_tagController, &TagController::newTagRequested,
          this, &TagExplorer2::onNewTagRequested);
  connect(m_tagController, &TagController::deleteTagRequested,
          this, &TagExplorer2::onDeleteTagRequested);
  connect(m_tagController, &TagController::errorOccurred,
          this, &TagExplorer2::onErrorOccurred);

  // 4. FileListView activation
  connect(m_fileView, &QListView::activated, this, [this](const QModelIndex &idx) {
    QVariant v = idx.data(INodeListModel::NodeIdentifierRole);
    if (v.isValid()) {
      NodeIdentifier nodeId = v.value<NodeIdentifier>();
      if (nodeId.isValid()) {
        emit openNodeRequested(nodeId);
      }
    }
  });

  // Context menu on file list
  connect(m_fileView, &FileListView::contextMenuRequested, this,
          [this](const NodeIdentifier &nodeId, const QPoint &globalPos) {
            if (!nodeId.isValid()) {
              return;
            }

            QMenu menu(this);

            menu.addAction(tr("Open"), this, [this, nodeId]() {
              emit openNodeRequested(nodeId);
            });

            menu.addAction(tr("Open Location"), this, [this, nodeId]() {
              auto *notebookSvc = m_services.get<NotebookCoreService>();
              if (!notebookSvc) {
                return;
              }
              const QString absPath =
                  notebookSvc->buildAbsolutePath(nodeId.notebookId, nodeId.relativePath);
              if (!absPath.isEmpty()) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(absPath).absolutePath()));
              }
            });

            menu.addAction(tr("Copy Path"), this, [this, nodeId]() {
              auto *notebookSvc = m_services.get<NotebookCoreService>();
              if (!notebookSvc) {
                return;
              }
              const QString absPath =
                  notebookSvc->buildAbsolutePath(nodeId.notebookId, nodeId.relativePath);
              if (!absPath.isEmpty()) {
                QApplication::clipboard()->setText(absPath);
              }
            });

            menu.addAction(tr("Properties"), this, [this, nodeId]() {
              auto *notebookSvc = m_services.get<NotebookCoreService>();
              if (!notebookSvc) {
                return;
              }
              const QString absPath =
                  notebookSvc->buildAbsolutePath(nodeId.notebookId, nodeId.relativePath);
              QMessageBox::information(window(), tr("Properties"),
                                       tr("File: %1\nPath: %2\nNotebook: %3")
                                           .arg(nodeId.relativePath.section('/', -1))
                                           .arg(absPath)
                                           .arg(nodeId.notebookId));
            });

            menu.exec(globalPos);
          });

  connect(m_tagController, &TagController::openNodeRequested,
          this, &TagExplorer2::openNodeRequested);

  // 5. Tag operation completed → reload model
  connect(m_tagController, &TagController::tagOperationCompleted,
          m_tagModel, &TagModel::reload);

  // 6. After tag creation, locate the new tag in the tree.
  connect(m_tagController, &TagController::tagCreated,
          m_tagView, &TagView::selectAndScrollToTag);

  // 7. Tag compatibility changed -> model state update (MVC)
  connect(m_tagController, &TagController::tagCompatibilityChanged,
          m_tagModel, &TagModel::setIncompatibleTags);

  // 8. Context menu on tag tree
  connect(m_tagView, &TagView::contextMenuRequested,
          this, &TagExplorer2::onContextMenuRequested);

  setFocusProxy(m_tagView);
}

void TagExplorer2::setupTitleBar() {
  m_titleBar =
      new TitleBar(m_services.get<ThemeService>(), QString(), false, TitleBar::Action::Menu, this);
  m_titleBar->setActionButtonsAlwaysShown(true);

  // + button for New Tag
  auto *newTagBtn =
      m_titleBar->addActionButton(QStringLiteral("add.svg"), tr("New Tag"));
  connect(newTagBtn, &QToolButton::clicked, this, [this]() {
    m_tagController->onNewTagAction(m_notebookId);
  });

  setupTitleBarMenu();
}

void TagExplorer2::setupTitleBarMenu() {
  // Two Columns toggle
  auto *twoColumnsAct = m_titleBar->addMenuAction(tr("Two Columns"), this, [this](bool p_checked) {
    m_splitter->setOrientation(p_checked ? Qt::Horizontal : Qt::Vertical);
  });
  twoColumnsAct->setCheckable(true);
  twoColumnsAct->setChecked(false);
}

void TagExplorer2::setNotebookId(const QString &p_notebookId) {
  m_notebookId = p_notebookId;
  m_tagModel->setNotebookId(p_notebookId);
  m_tagController->setNotebookId(p_notebookId);
}

QByteArray TagExplorer2::saveState() const {
  QByteArray state;
  QDataStream stream(&state, QIODevice::WriteOnly);
  stream << static_cast<int>(m_splitter->orientation());
  stream << m_splitter->saveState();
  return state;
}

void TagExplorer2::restoreState(const QByteArray &p_data) {
  if (p_data.isEmpty()) {
    return;
  }

  QDataStream stream(p_data);

  int orientation;
  stream >> orientation;
  m_splitter->setOrientation(static_cast<Qt::Orientation>(orientation));

  QByteArray splitterState;
  stream >> splitterState;
  if (!splitterState.isEmpty()) {
    m_splitter->restoreState(splitterState);
  }
}

void TagExplorer2::onMatchingNodesChanged(const QJsonArray &p_nodes) {
  m_fileModel->setNodes(p_nodes, m_notebookId);
}

void TagExplorer2::onNewTagRequested(const QString &p_notebookId) {
  // Use selected tag as parent if available.
  const QStringList selected = m_tagView->selectedTagNames();
  const QString parentTag = selected.isEmpty() ? QString() : selected.first();
  const QString parentPath = parentTag.isEmpty() ? QString() : m_tagModel->fullTagPath(parentTag);

  const QString label = parentPath.isEmpty()
      ? tr("Tag path (e.g. tag1/tag2/tag3):")
      : tr("Tag path under \"%1\" (e.g. child1/child2):").arg(parentPath);

  bool ok = false;
  QString tagPath = QInputDialog::getText(window(), tr("New Tag"), label,
                                          QLineEdit::Normal, QString(), &ok);
  if (ok && !tagPath.isEmpty()) {
    QString fullPath = tagPath.trimmed();
    if (!parentPath.isEmpty()) {
      fullPath = parentPath + QLatin1Char('/') + fullPath;
    }
    m_tagController->handleNewTagResult(p_notebookId, fullPath);
  }
}

void TagExplorer2::onDeleteTagRequested(const QString &p_notebookId, const QString &p_tagName) {
  int ret = QMessageBox::question(
      window(), tr("Delete"),
      tr("Are you sure you want to delete tag \"%1\"?").arg(p_tagName),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (ret == QMessageBox::Yes) {
    m_tagController->handleDeleteTagConfirmed(p_notebookId, p_tagName);
  }
}

void TagExplorer2::onErrorOccurred(const QString &p_title, const QString &p_message) {
  QMessageBox::critical(window(), p_title, p_message);
}

void TagExplorer2::onContextMenuRequested(const QString &p_tagName, const QPoint &p_globalPos) {
  QMenu menu(this);

  menu.addAction(tr("New Tag"), this, [this]() {
    m_tagController->onNewTagAction(m_notebookId);
  });

  if (!p_tagName.isEmpty()) {
    menu.addAction(tr("Delete"), this, [this, p_tagName]() {
      m_tagController->onDeleteTagAction(m_notebookId, p_tagName);
    });

    menu.addAction(tr("Move"), this, [this, p_tagName]() {
      bool ok = false;
      QString newParent = QInputDialog::getText(
          window(), tr("Move Tag"),
          tr("Enter new parent tag (empty for root):"),
          QLineEdit::Normal, QString(), &ok);
      if (ok) {
        m_tagController->onMoveTagAction(m_notebookId, p_tagName, newParent.trimmed());
      }
    });
  }

  menu.exec(p_globalPos);
}
