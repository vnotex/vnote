#include "attachmentpopup2.h"

#include <QAction>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <core/servicelocator.h>
#include <core/services/buffer2.h>
#include <gui/services/themeservice.h>
#include <models/attachmentlistmodel.h>
#include <controllers/attachmentcontroller.h>
#include <utils/iconutils.h>

#include "propertydefs.h"

using namespace vnotex;

AttachmentPopup2::AttachmentPopup2(ServiceLocator &p_services, QToolButton *p_btn,
                                   QWidget *p_parent)
    : ButtonPopup(p_btn, p_parent), m_services(p_services) {
  setupUI();

  connect(this, &QMenu::aboutToShow, this, [this]() {
    if (!m_buffer || !m_buffer->isValid()) {
      m_listView->setVisible(false);
      m_unsupportedLabel->setVisible(true);
      m_countLabel->setVisible(false);
      m_addBtn->setEnabled(false);
      m_openBtn->setEnabled(false);
      m_deleteBtn->setEnabled(false);
      m_renameBtn->setEnabled(false);
      m_openFolderBtn->setEnabled(false);
      m_copyPathBtn->setEnabled(false);
      return;
    }

    bool supported = m_buffer->isAttachmentSupported();
    m_listView->setVisible(supported);
    m_unsupportedLabel->setVisible(!supported);
    m_countLabel->setVisible(supported);

    if (supported) {
      m_model->setBuffer(m_buffer);
      m_model->refresh();
      updateButtonsState();
      m_listView->setFocus();
    } else {
      m_addBtn->setEnabled(false);
      m_openBtn->setEnabled(false);
      m_deleteBtn->setEnabled(false);
      m_renameBtn->setEnabled(false);
      m_openFolderBtn->setEnabled(false);
      m_copyPathBtn->setEnabled(false);
    }
  });
}

void AttachmentPopup2::setupUI() {
  auto *widget = new QWidget(this);
  auto *mainLayout = new QVBoxLayout(widget);

  auto *themeService = m_services.get<ThemeService>();

  // -- Buttons row --
  auto *buttonsLayout = new QHBoxLayout();

  // Helper to create a tool button with the action style property.
  auto createButton = [this]() -> QToolButton * {
    auto *btn = new QToolButton(this);
    btn->setProperty(PropertyDefs::c_actionToolButton, true);
    return btn;
  };

  // Add.
  {
    m_addBtn = createButton();
    auto *act = new QAction(
        IconUtils::fetchIconWithDisabledState(
            themeService->getIconFile(QStringLiteral("add.svg"))),
        tr("Add"), m_addBtn);
    connect(act, &QAction::triggered, this, [this]() {
      m_controller->addAttachments();
    });
    m_addBtn->setDefaultAction(act);
    buttonsLayout->addWidget(m_addBtn);
  }

  // Open Folder.
  {
    m_openFolderBtn = createButton();
    auto *act = new QAction(
        IconUtils::fetchIconWithDisabledState(
            themeService->getIconFile(QStringLiteral("open_folder.svg"))),
        tr("Open Folder"), m_openFolderBtn);
    connect(act, &QAction::triggered, this, [this]() {
      m_controller->openAttachmentsFolder();
    });
    m_openFolderBtn->setDefaultAction(act);
    buttonsLayout->addWidget(m_openFolderBtn);
  }

  buttonsLayout->addStretch();

  // Open.
  {
    m_openBtn = createButton();
    auto *act = new QAction(
        IconUtils::fetchIconWithDisabledState(
            themeService->getIconFile(QStringLiteral("open_file.svg"))),
        tr("Open"), m_openBtn);
    connect(act, &QAction::triggered, this, [this]() {
      m_controller->openAttachments(getSelectedFilenames());
    });
    m_openBtn->setDefaultAction(act);
    buttonsLayout->addWidget(m_openBtn);
  }

  // Delete.
  {
    m_deleteBtn = createButton();
    auto *act = new QAction(
        IconUtils::fetchIconWithDisabledState(
            themeService->getIconFile(QStringLiteral("delete.svg"))),
        tr("Delete"), m_deleteBtn);
    connect(act, &QAction::triggered, this, [this]() {
      m_controller->deleteAttachments(getSelectedFilenames());
    });
    m_deleteBtn->setDefaultAction(act);
    buttonsLayout->addWidget(m_deleteBtn);
  }

  // Rename.
  {
    m_renameBtn = createButton();
    auto *act = new QAction(
        IconUtils::fetchIconWithDisabledState(
            themeService->getIconFile(QStringLiteral("edit_editor.svg"))),
        tr("Rename"), m_renameBtn);
    connect(act, &QAction::triggered, this, [this]() {
      auto idx = m_listView->currentIndex();
      if (idx.isValid()) {
        m_controller->startRename(idx);
      }
    });
    m_renameBtn->setDefaultAction(act);
    buttonsLayout->addWidget(m_renameBtn);
  }

  // Copy Path.
  {
    m_copyPathBtn = createButton();
    auto *act = new QAction(
        IconUtils::fetchIconWithDisabledState(
            themeService->getIconFile(QStringLiteral("copy_path.svg"))),
        tr("Copy Path"), m_copyPathBtn);
    connect(act, &QAction::triggered, this, [this]() {
      m_controller->copyAttachmentPaths(getSelectedFilenames());
    });
    m_copyPathBtn->setDefaultAction(act);
    buttonsLayout->addWidget(m_copyPathBtn);
  }

  mainLayout->addLayout(buttonsLayout);

  // -- List view --
  m_model = new AttachmentListModel(this);
  m_controller = new AttachmentController(m_services, this);

  m_listView = new QListView(this);
  m_listView->setModel(m_model);
  m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_listView->installEventFilter(this);
  mainLayout->addWidget(m_listView);

  // Connect selection changes to button enablement.
  connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, [this]() { updateButtonsState(); });

  // Connect model reset to button enablement and count label.
  connect(m_model, &QAbstractItemModel::modelReset, this, [this]() {
    updateButtonsState();
    m_countLabel->setText(tr("%n attachment(s)", "", m_model->rowCount()));
  });

  // Connect controller signals to refresh model.
  connect(m_controller, &AttachmentController::attachmentAdded, this, [this]() {
    m_model->refresh();
    updateButtonsState();
  });

  connect(m_controller, &AttachmentController::attachmentDeleted, this, [this]() {
    m_model->refresh();
    updateButtonsState();
  });

  // Connect rename request to inline editing.
  connect(m_controller, &AttachmentController::renameRequested, this, [this](const QModelIndex &p_index) {
    m_listView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_listView->edit(p_index);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  });

  // -- Unsupported label --
  m_unsupportedLabel = new QLabel(tr("Attachments not supported for this notebook type"), this);
  m_unsupportedLabel->setAlignment(Qt::AlignCenter);
  m_unsupportedLabel->setVisible(false);
  mainLayout->addWidget(m_unsupportedLabel);

  // -- Count label --
  m_countLabel = new QLabel(this);
  m_countLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  m_countLabel->setVisible(false);
  mainLayout->addWidget(m_countLabel);

  widget->setMinimumSize(320, 384);
  addWidget(widget);
}

void AttachmentPopup2::setBuffer(Buffer2 *p_buffer) {
  m_buffer = p_buffer;
  m_controller->setBuffer(p_buffer);
}

void AttachmentPopup2::showEvent(QShowEvent *p_event) {
  ButtonPopup::showEvent(p_event);

  // Move it to be right-aligned.
  if (m_button->isVisible()) {
    const auto p = pos();
    const auto btnRect = m_button->geometry();
    move(p.x() + btnRect.width() - geometry().width(), p.y());
  }
}

bool AttachmentPopup2::eventFilter(QObject *p_obj, QEvent *p_event) {
  if (p_obj == m_listView && p_event->type() == QEvent::KeyPress) {
    auto *ke = static_cast<QKeyEvent *>(p_event);
    if (ke->key() == Qt::Key_F2) {
      auto idx = m_listView->currentIndex();
      if (idx.isValid()) {
        m_controller->startRename(idx);
      }
      return true;
    }
  }
  return ButtonPopup::eventFilter(p_obj, p_event);
}

void AttachmentPopup2::updateButtonsState() {
  int count = m_listView->selectionModel()->selectedIndexes().size();
  m_addBtn->setEnabled(true);
  m_openFolderBtn->setEnabled(true);
  m_openBtn->setEnabled(count > 0);
  m_deleteBtn->setEnabled(count > 0);
  m_copyPathBtn->setEnabled(count > 0);
  m_renameBtn->setEnabled(count == 1);
}

QStringList AttachmentPopup2::getSelectedFilenames() const {
  QStringList names;
  const auto indexes = m_listView->selectionModel()->selectedIndexes();
  for (const auto &idx : indexes) {
    names.append(idx.data(Qt::DisplayRole).toString());
  }
  return names;
}
