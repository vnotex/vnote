#include "attachmentpopup.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QFileDialog>
#include <QInputDialog>
#include <QTimer>

#include "propertydefs.h"

#include <utils/iconutils.h>
#include <utils/widgetutils.h>
#include <utils/pathutils.h>
#include <core/vnotex.h>
#include <core/thememgr.h>
#include <buffer/buffer.h>

#include "filesystemviewer.h"
#include "messageboxhelper.h"
#include "fileopenparameters.h"
#include <core/exception.h>

using namespace vnotex;

AttachmentPopup::AttachmentPopup(QToolButton *p_btn, QWidget *p_parent)
    : QMenu(p_parent),
      m_button(p_btn)
{
    setupUI();

    connect(this, &QMenu::aboutToShow,
            this, [this]() {
                Q_ASSERT(m_buffer);
                if (m_needUpdateAttachmentFolder) {
                    setRootFolder(m_buffer->getAttachmentFolderPath());
                }

                m_viewer->setFocus();
            });
}

void AttachmentPopup::setupUI()
{
    auto mainLayout = new QVBoxLayout(this);

    const auto &themeMgr = VNoteX::getInst().getThemeMgr();

    auto buttonsLayout = new QHBoxLayout();

    {
        // Add.
        auto addBtn = createButton();

        auto act = new QAction(IconUtils::fetchIconWithDisabledState(themeMgr.getIconFile(QStringLiteral("add.svg"))),
                               tr("Add"),
                               addBtn);
        connect(act, &QAction::triggered,
                this, [this]() {
                    if (checkRootFolderAndSingleSelection()) {
                        // Get dest folder path before other dialogs.
                        const auto destFolderPath = getDestFolderPath();

                        static QString lastDirPath = QDir::homePath();
                        auto files = QFileDialog::getOpenFileNames(this, tr("Select Files As Attachments"), lastDirPath);
                        if (files.isEmpty()) {
                            return;
                        }

                        lastDirPath = QFileInfo(files[0]).path();

                        addAttachments(destFolderPath, files);
                    }
                });
        addBtn->setDefaultAction(act);
        buttonsLayout->addWidget(addBtn);
    }

    {
        // New File.
        auto newFileBtn = createButton();

        auto act = new QAction(IconUtils::fetchIconWithDisabledState(themeMgr.getIconFile(QStringLiteral("new_file.svg"))),
                               tr("New File"),
                               newFileBtn);
        connect(act, &QAction::triggered,
                this, [this]() {
                    if (checkRootFolderAndSingleSelection()) {
                        // Get dest folder path before other dialogs.
                        const auto destFolderPath = getDestFolderPath();
                        auto name = QInputDialog::getText(this,
                                                          tr("New Attachment"),
                                                          tr("File name:"));
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

        auto act = new QAction(IconUtils::fetchIconWithDisabledState(themeMgr.getIconFile(QStringLiteral("new_folder.svg"))),
                               tr("New Folder"),
                               newFolderBtn);
        connect(act, &QAction::triggered,
                this, [this]() {
                    if (checkRootFolderAndSingleSelection()) {
                        // Get dest folder path before other dialogs.
                        const auto destFolderPath = getDestFolderPath();
                        auto name = QInputDialog::getText(this,
                                                          tr("New Attachment"),
                                                          tr("Folder name:"));
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

        auto act = new QAction(IconUtils::fetchIconWithDisabledState(themeMgr.getIconFile(QStringLiteral("open_folder.svg"))),
                               tr("Open Folder"),
                               openFolderBtn);
        connect(act, &QAction::triggered,
                this, [this]() {
                    WidgetUtils::openUrlByDesktop(QUrl::fromLocalFile(m_viewer->rootPath()));
                });
        openFolderBtn->setDefaultAction(act);
        buttonsLayout->addWidget(openFolderBtn);
    }

    buttonsLayout->addStretch();

    mainLayout->addLayout(buttonsLayout);

    m_viewer = new FileSystemViewer(this);
    connect(m_viewer, &FileSystemViewer::renameFile,
            this, [this](const QString &p_path, const QString &p_name) {
                try {
                    m_buffer->renameAttachment(p_path, p_name);
                    showPopupLater(QStringList() << PathUtils::concatenateFilePath(PathUtils::parentDirPath(p_path), p_name));
                } catch (Exception &p_e) {
                    MessageBoxHelper::notify(MessageBoxHelper::Warning,
                                             tr("Failed to rename attachment (%1) to (%2).").arg(p_path, p_name),
                                             tr("Please try another name again."),
                                             p_e.what(),
                                             this);
                }
            });
    connect(m_viewer, &FileSystemViewer::removeFiles,
            this, [this](QStringList p_paths) {
                if (p_paths.isEmpty()) {
                    return;
                }

                // Filter out children paths.
                QStringList paths;
                std::sort(p_paths.begin(), p_paths.end());
                for (int i = p_paths.size() - 1; i >= 0; --i) {
                    bool skip = false;
                    for (int j = i - 1; j >= 0; --j) {
                        // Check if [j] is parent of [i].
                        if (p_paths[j].size() < p_paths[i].size()
                            && p_paths[i].startsWith(p_paths[j]) && p_paths[i].at(p_paths[j].size()) == '/') {
                            skip = true;
                            break;
                        }
                    }

                    if (!skip) {
                        paths << p_paths[i];
                    }
                }

                m_buffer->removeAttachment(paths);
            });
    connect(m_viewer, &FileSystemViewer::openFiles,
            this, [this](const QStringList &p_paths) {
                hide();
                for (const auto &file : p_paths) {
                    auto paras = QSharedPointer<FileOpenParameters>::create();
                    paras->m_nodeAttachedTo = m_buffer->getNode();
                    Q_ASSERT(paras->m_nodeAttachedTo);
                    emit VNoteX::getInst().openFileRequested(file, paras);
                }
            });
    mainLayout->addWidget(m_viewer);

    setMinimumSize(320, 384);
}

QToolButton *AttachmentPopup::createButton()
{
    auto btn = new QToolButton(this);
    btn->setProperty(PropertyDefs::s_actionToolButton, true);
    return btn;
}

bool AttachmentPopup::checkRootFolderAndSingleSelection()
{
    const auto rootPath = m_viewer->rootPath();
    bool ret = !rootPath.isEmpty() && m_viewer->selectedCount() <= 1;
    if (!ret) {
        MessageBoxHelper::notify(MessageBoxHelper::Information,
                                 tr("Please select one directory to continue."),
                                 this);
    }
    return ret;
}

void AttachmentPopup::setRootFolder(const QString &p_folderPath)
{
    m_viewer->setRootPath(p_folderPath);
    m_needUpdateAttachmentFolder = false;
}

void AttachmentPopup::setBuffer(Buffer *p_buffer)
{
    if (m_buffer == p_buffer) {
        return;
    }

    m_buffer = p_buffer;
    m_needUpdateAttachmentFolder = true;
}

QString AttachmentPopup::getDestFolderPath() const
{
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

void AttachmentPopup::addAttachments(const QString &p_destFolderPath, const QStringList &p_files)
{
    auto files = m_buffer->addAttachment(p_destFolderPath, p_files);
    showPopupLater(files);
}

void AttachmentPopup::newAttachmentFile(const QString &p_destFolderPath, const QString &p_name)
{
    auto file = m_buffer->newAttachmentFile(p_destFolderPath, p_name);
    showPopupLater(QStringList() << file);
}

void AttachmentPopup::newAttachmentFolder(const QString &p_destFolderPath, const QString &p_name)
{
    auto folder = m_buffer->newAttachmentFolder(p_destFolderPath, p_name);
    showPopupLater(QStringList() << folder);
}

void AttachmentPopup::showPopupLater(const QStringList &p_pathsToSelect)
{
    QTimer::singleShot(250, this, [this, p_pathsToSelect]() {
        m_viewer->scrollToAndSelect(p_pathsToSelect);
        m_button->showMenu();
    });
}
