#include "tagexplorer2.h"

#include <QDataStream>
#include <QIODevice>
#include <QInputDialog>
#include <QMessageBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <controllers/tagcontroller.h>
#include <core/servicelocator.h>
#include <gui/services/themeservice.h>
#include <models/tagmodel.h>
#include <views/tagnodelistview.h>
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
  m_tagNodeListView = new TagNodeListView(this);
  m_tagController = new TagController(m_services, this);

  // Splitter: tag tree on top, node list on bottom
  m_splitter = new QSplitter(Qt::Vertical, this);
  m_splitter->addWidget(m_tagView);
  m_splitter->addWidget(m_tagNodeListView);
  mainLayout->addWidget(m_splitter, 1);

  // Wire signals

  // 1. TagView selection → TagController
  connect(m_tagView, &TagView::tagsSelectionChanged,
          m_tagController, &TagController::onTagsSelected);

  // 2. TagController matching nodes → TagNodeListView
  connect(m_tagController, &TagController::matchingNodesChanged,
          m_tagNodeListView, &TagNodeListView::setNodes);

  // 3. TagController GUI requests → TagExplorer2 dialog handlers
  connect(m_tagController, &TagController::newTagRequested,
          this, &TagExplorer2::onNewTagRequested);
  connect(m_tagController, &TagController::deleteTagRequested,
          this, &TagExplorer2::onDeleteTagRequested);
  connect(m_tagController, &TagController::errorOccurred,
          this, &TagExplorer2::onErrorOccurred);

  // 4. TagNodeListView activation → TagController → propagate upward
  connect(m_tagNodeListView, &TagNodeListView::nodeActivated,
          m_tagController, &TagController::onNodeActivated);
  connect(m_tagController, &TagController::openNodeRequested,
          this, &TagExplorer2::openNodeRequested);

  // 5. Tag operation completed → reload model
  connect(m_tagController, &TagController::tagOperationCompleted,
          m_tagModel, &TagModel::reload);

  // 6. Tag compatibility changed (store for future use)
  connect(m_tagController, &TagController::tagCompatibilityChanged,
          this, [](const QStringList &p_incompatibleTags) {
            Q_UNUSED(p_incompatibleTags);
            // TODO: Grey out incompatible tags in TagView
          });

  setFocusProxy(m_tagView);
}

void TagExplorer2::setupTitleBar() {
  m_titleBar =
      new TitleBar(m_services.get<ThemeService>(), QString(), false, TitleBar::Action::Menu, this);
  m_titleBar->setActionButtonsAlwaysShown(true);

  setupTitleBarMenu();
}

void TagExplorer2::setupTitleBarMenu() {
  // New Tag action
  m_titleBar->addMenuAction(tr("New Tag"), this, [this]() {
    m_tagController->onNewTagAction(m_notebookId);
  });

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

void TagExplorer2::onNewTagRequested(const QString &p_notebookId) {
  bool ok = false;
  QString tagName = QInputDialog::getText(window(), tr("New Tag"), tr("Tag name:"),
                                          QLineEdit::Normal, QString(), &ok);
  if (ok && !tagName.isEmpty()) {
    m_tagController->handleNewTagResult(p_notebookId, tagName.trimmed());
  }
}

void TagExplorer2::onDeleteTagRequested(const QString &p_notebookId, const QString &p_tagName) {
  int ret = QMessageBox::question(
      window(), tr("Delete Tag"),
      tr("Are you sure you want to delete tag \"%1\"?").arg(p_tagName),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
  if (ret == QMessageBox::Yes) {
    m_tagController->handleDeleteTagConfirmed(p_notebookId, p_tagName);
  }
}

void TagExplorer2::onErrorOccurred(const QString &p_title, const QString &p_message) {
  QMessageBox::critical(window(), p_title, p_message);
}
