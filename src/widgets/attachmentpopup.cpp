#include "attachmentpopup.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "propertydefs.h"

#include <buffer/buffer.h>
#include <core/thememgr.h>
#include <core/vnotex.h>
#include <utils/clipboardutils.h>
#include <utils/iconutils.h>
#include <utils/pathutils.h>
#include <utils/widgetutils.h>

#include "dialogs/filepropertiesdialog.h"
#include "fileopenparameters.h"
#include "filesystemviewer.h"
#include "messageboxhelper.h"
#include <core/configmgr.h>
#include <core/exception.h>
#include <core/sessionconfig.h>

using namespace vnotex;

AttachmentPopup::AttachmentPopup(QToolButton *p_btn, QWidget *p_parent)
    : ButtonPopup(p_btn, p_parent) {
  setupUI();

  connect(this, &QMenu::aboutToShow, this, [this]() {
    Q_ASSERT(m_buffer);
    if (m_needUpdateAttachmentFolder) {
      setRootFolder(m_buffer->getAttachmentFolderPath());
    }

    m_viewer->setFocus();
  });
}

void AttachmentPopup::setupUI() {
  QWidget *widget = new QWidget{};
  auto mainLayout = new QVBoxLayout(widget);

  const auto &themeMgr = VNoteX::getInst().getThemeMgr();

  auto buttonsLayout = new QHBoxLayout();

  {
    // Add.
    auto addBtn = createButton();

    auto act = new QAction(
        IconUtils::fetchIconWithDisabledState(themeMgr.getIconFile(QStringLiteral("add.svg"))),
        tr("Add"), addBtn);
    connect(act, &QAction::triggered, this, [this]() {
      if (checkRootFolderAndSingleSelection()) {
        // Get dest folder path before other dialogs.
        const auto destFolderPath = getDestFolderPath();

        auto &sessionConfig = ConfigMgr::getInst().getSessionConfig();
        auto files = QFileDialog::getOpenFileNames(nullptr, tr("Select Files As Attachments"),
                                                   sessionConfig.getExternalMediaDefaultPath());
        if (files.isEmpty()) {
          return;
        }

        sessionConfig.setExternalMediaDefaultPath(QFileInfo(files[0]).path());

        addAttachments(destFolderPath, files);
      }
    });
    addBtn->setDefaultAction(act);
    buttonsLayout->addWidget(addBtn);
  }

  {
    // New File.
    auto newFileBtn = createButton();

    auto act = new QAction(
        IconUtils::fetchIconWithDisabledState(themeMgr.getIconFile(QStringLiteral("new_file.svg"))),
        tr("New File"), newFileBtn);
    connect(act, &QAction::triggered, this, [this]() {
      if (checkRootFolderAndSingleSelection()) {
        // Get dest folder path before other dialogs.
        const auto destFolderPath = getDestFolderPath();
        auto name = QInputDialog::getText(this, tr("New Attachment"), tr("File name:"));
        if (!name.isEmpty()) {
          newAttachmentFile(destFolderPath, name);
        }
      }
    });
    newFileBtn->setDefaultAction(act);
    buttonsLayout->addWidget(newFileBtn);
  }

  {
    // New Folder.
    auto newFolderBtn = createButton();

    auto act = new QAction(IconUtils::fetchIconWithDisabledState(
                               themeMgr.getIconFile(QStringLiteral("new_folder.svg"))),
                           tr("New Folder"), newFolderBtn);
    connect(act, &QAction::triggered, this, [this]() {
      if (checkRootFolderAndSingleSelection()) {
        // Get dest folder path before other dialogs.
        const auto destFolderPath = getDestFolderPath();
        auto name = QInputDialog::getText(this, tr("New Attachment"), tr("Folder name:"));
        if (!name.isEmpty()) {
          newAttachmentFolder(destFolderPath, name);
        }
      }
    });
    newFolderBtn->setDefaultAction(act);
    buttonsLayout->addWidget(newFolderBtn);
  }

  {
    // Open folder.
    auto openFolderBtn = createButton();

    auto act = new QAction(IconUtils::fetchIconWithDisabledState(
                               themeMgr.getIconFile(QStringLiteral("open_folder.svg"))),
                           tr("Open Folder"), openFolderBtn);
    connect(act, &QAction::triggered, this,
            [this]() { WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(m_viewer->rootPath())); });
    openFolderBtn->setDefaultAction(act);
    buttonsLayout->addWidget(openFolderBtn);
  }

  buttonsLayout->addStretch();

  {
    // Open files.
    m_openBtn = createButton();
    auto act = new QAction(IconUtils::fetchIconWithDisabledState(
                               themeMgr.getIconFile(QStringLiteral("open_file.svg"))),
                           tr("Open"), m_openBtn);
    connect(act, &QAction::triggered, this, [this]() {
      hide();
      const auto paths = m_viewer->getSelectedPaths();
      for (const auto &file : paths) {
        auto paras = QSharedPointer<FileOpenParameters>::create();
        paras->m_nodeAttachedTo = m_buffer->getNode();
        Q_ASSERT(paras->m_nodeAttachedTo);
        emit VNoteX::getInst().openFileRequested(file, paras);
      }
    });
    m_openBtn->setDefaultAction(act);
    buttonsLayout->addWidget(m_openBtn);
  }

  {
    // Delete files.
    m_deleteBtn = createButton();
    auto act = new QAction(
        IconUtils::fetchIconWithDisabledState(themeMgr.getIconFile(QStringLiteral("delete.svg"))),
        tr("Delete"), m_deleteBtn);
    connect(act, &QAction::triggered, this, [this]() {
      auto selectedPaths = m_viewer->getSelectedPaths();
      if (selectedPaths.isEmpty()) {
        return;
      }
      // Filter out children paths.
      QStringList paths;
      std::sort(selectedPaths.begin(), selectedPaths.end());
      for (int i = selectedPaths.size() - 1; i >= 0; --i) {
        bool skip = false;
        for (int j = i - 1; j >= 0; --j) {
          // Check if [j] is parent of [i].
          if (selectedPaths[j].size() < selectedPaths[i].size() &&
              selectedPaths[i].startsWith(selectedPaths[j]) &&
              selectedPaths[i].at(selectedPaths[j].size()) == '/') {
            skip = true;
            break;
          }
        }

        if (!skip) {
          paths << selectedPaths[i];
        }
      }

      m_buffer->removeAttachment(paths);
    });
    m_deleteBtn->setDefaultAction(act);
    buttonsLayout->addWidget(m_deleteBtn);
  }

  {
    // Copy path.
    m_copyPathBtn = createButton();
    auto act = new QAction(IconUtils::fetchIconWithDisabledState(
                               themeMgr.getIconFile(QStringLiteral("copy_path.svg"))),
                           tr("Copy Path"), m_copyPathBtn);
    connect(act, &QAction::triggered, this, [this]() {
      hide();
      const auto paths = m_viewer->getSelectedPaths();
      ClipboardUtils::setTextToClipboard(paths.join('\n'));
    });
    m_copyPathBtn->setDefaultAction(act);
    buttonsLayout->addWidget(m_copyPathBtn);
  }

  {
    // Properties.
    m_propertiesBtn = createButton();
    auto act = new QAction(IconUtils::fetchIconWithDisabledState(
                               themeMgr.getIconFile(QStringLiteral("properties.svg"))),
                           tr("Properties"), m_propertiesBtn);
    connect(act, &QAction::triggered, this, [this]() {
      hide();
      const auto paths = m_viewer->getSelectedPaths();
      Q_ASSERT(paths.size() == 1);
      const auto path = paths[0];
      FilePropertiesDialog dialog(path, this);
      int ret = dialog.exec();
      if (ret) {
        auto newName = dialog.getFileName();
        if (newName != PathUtils::fileName(path)) {
          // Rename.
          try {
            m_buffer->renameAttachment(path, newName);
            showPopupLater(QStringList() << PathUtils::concatenateFilePath(
                               PathUtils::parentDirPath(path), newName));
          } catch (Exception &p_e) {
            MessageBoxHelper::notify(
                MessageBoxHelper::Warning,
                tr("Failed to rename attachment (%1) to (%2).").arg(path, newName),
                tr("Please try another name again."), p_e.what(), this);
          }
        }
      }
    });
    m_propertiesBtn->setDefaultAction(act);
    buttonsLayout->addWidget(m_propertiesBtn);
  }

  mainLayout->addLayout(buttonsLayout);

  m_viewer = new FileSystemViewer(this);
  connect(m_viewer, &FileSystemViewer::selectionChanged, this,
          &AttachmentPopup::updateButtonsState);

  updateButtonsState();

  mainLayout->addWidget(m_viewer);

  widget->setMinimumSize(320, 384);

  addWidget(widget);
}

QToolButton *AttachmentPopup::createButton() {
  auto btn = new QToolButton(this);
  btn->setProperty(PropertyDefs::c_actionToolButton, true);
  return btn;
}

bool AttachmentPopup::checkRootFolderAndSingleSelection() {
  const auto rootPath = m_viewer->rootPath();
  bool ret = !rootPath.isEmpty() && m_viewer->selectedCount() <= 1;
  if (!ret) {
    MessageBoxHelper::notify(MessageBoxHelper::Information,
                             tr("Please select one directory to continue."), this);
  }
  return ret;
}

void AttachmentPopup::setRootFolder(const QString &p_folderPath) {
  m_viewer->setRootPath(p_folderPath);
  m_needUpdateAttachmentFolder = false;
}

void AttachmentPopup::setBuffer(Buffer *p_buffer) {
  if (m_buffer == p_buffer) {
    return;
  }

  m_buffer = p_buffer;
  m_needUpdateAttachmentFolder = true;
}

QString AttachmentPopup::getDestFolderPath() const {
  const auto selectedPaths = m_viewer->getSelectedPaths();
  Q_ASSERT(selectedPaths.size() <= 1);
  QString destFolderPath;
  if (selectedPaths.isEmpty()) {
    destFolderPath = m_viewer->rootPath();
  } else {
    destFolderPath = PathUtils::dirOrParentDirPath(selectedPaths[0]);
  }
  return destFolderPath;
}

void AttachmentPopup::addAttachments(const QString &p_destFolderPath, const QStringList &p_files) {
  auto files = m_buffer->addAttachment(p_destFolderPath, p_files);
  showPopupLater(files);
}

void AttachmentPopup::newAttachmentFile(const QString &p_destFolderPath, const QString &p_name) {
  auto file = m_buffer->newAttachmentFile(p_destFolderPath, p_name);
  showPopupLater(QStringList() << file);
}

void AttachmentPopup::newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name) {
  auto folder = m_buffer->newAttachmentFolder(p_destFolderPath, p_name);
  showPopupLater(QStringList() << folder);
}

void AttachmentPopup::showPopupLater(const QStringList &p_pathsToSelect) {
  QTimer::singleShot(250, this, [this, p_pathsToSelect]() {
    m_viewer->scrollToAndSelect(p_pathsToSelect);
    m_button->showMenu();
  });
}

void AttachmentPopup::updateButtonsState() {
  const int selectedCount = m_viewer->selectedCount();
  m_openBtn->setEnabled(selectedCount > 0);
  m_deleteBtn->setEnabled(selectedCount > 0);
  m_copyPathBtn->setEnabled(selectedCount > 0);
  m_propertiesBtn->setEnabled(selectedCount == 1);
}
