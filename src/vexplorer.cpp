#include "vexplorer.h"

#include <QtWidgets>
#include <QFileSystemModel>
#include <QDesktopServices>

#include "utils/viconutils.h"
#include "utils/vutils.h"
#include "vconfigmanager.h"
#include "vmainwindow.h"
#include "vcart.h"
#include "vlineedit.h"
#include "vhistorylist.h"
#include "vorphanfile.h"

extern VMainWindow *g_mainWin;

extern VConfigManager *g_config;

const QString VExplorer::c_infoShortcutSequence = "F2";

VExplorer::VExplorer(QWidget *p_parent)
    : QWidget(p_parent),
      m_initialized(false),
      m_uiInitialized(false),
      m_index(-1)
{
}

void VExplorer::setupUI()
{
    if (m_uiInitialized) {
        return;
    }

    m_uiInitialized = true;

    QLabel *dirLabel = new QLabel(tr("Directory"), this);

    m_openBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/dir_item.svg"),
                                "",
                                this);
    m_openBtn->setToolTip(tr("Open"));
    m_openBtn->setProperty("FlatBtn", true);
    connect(m_openBtn, &QPushButton::clicked,
            this, [this]() {
                static QString defaultPath = g_config->getDocumentPathOrHomePath();
                QString dirPath = QFileDialog::getExistingDirectory(this,
                                                                    tr("Select Root Directory To Explore"),
                                                                    defaultPath,
                                                                    QFileDialog::ShowDirsOnly
                                                                    | QFileDialog::DontResolveSymlinks);
                if (!dirPath.isEmpty()) {
                    defaultPath = VUtils::basePathFromPath(dirPath);

                    int idx = addEntry(dirPath);
                    updateDirectoryComboBox();
                    if (idx != -1) {
                        setCurrentEntry(idx);
                    }
                }
            });

    m_openLocationBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/open_location.svg"),
                                        "",
                                        this);
    m_openLocationBtn->setToolTip(tr("Open Directory Location"));
    m_openLocationBtn->setProperty("FlatBtn", true);
    connect(m_openLocationBtn, &QPushButton::clicked,
            this, [this]() {
                if (checkIndex()) {
                    QUrl url = QUrl::fromLocalFile(m_entries[m_index].m_directory);
                    QDesktopServices::openUrl(url);
                }
            });

    m_starBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/star.svg"),
                                "",
                                this);
    m_starBtn->setToolTip(tr("Star"));
    m_starBtn->setProperty("FlatBtn", true);
    connect(m_starBtn, &QPushButton::clicked,
            this, [this]() {
                if (checkIndex()) {
                    m_entries[m_index].m_isStarred = !m_entries[m_index].m_isStarred;

                    g_config->setExplorerEntries(m_entries);

                    updateExplorerEntryIndexInConfig();

                    updateStarButton();
                }
            });

    QHBoxLayout *dirLayout = new QHBoxLayout();
    dirLayout->addWidget(dirLabel);
    dirLayout->addStretch();
    dirLayout->addWidget(m_openBtn);
    dirLayout->addWidget(m_openLocationBtn);
    dirLayout->addWidget(m_starBtn);
    dirLayout->setContentsMargins(0, 0, 0, 0);

    m_dirCB = VUtils::getComboBox(this);
    m_dirCB->setEditable(true);
    m_dirCB->setLineEdit(new VLineEdit(this));
    m_dirCB->setToolTip(tr("Path of the root directory to explore"));
    m_dirCB->lineEdit()->setPlaceholderText(tr("Root path to explore"));
    m_dirCB->lineEdit()->setProperty("EmbeddedEdit", true);
    connect(m_dirCB->lineEdit(), &QLineEdit::editingFinished,
            this, [this]() {
                QString text = m_dirCB->currentText();
                int idx = addEntry(text);
                updateDirectoryComboBox();
                if (idx != -1) {
                    setCurrentEntry(idx);
                }
            });
    QCompleter *completer = new QCompleter(this);
    QFileSystemModel *fsModel = new QFileSystemModel(completer);
    fsModel->setRootPath("");
    completer->setModel(fsModel);
    // Enable styling the popup list via QListView::item.
    completer->popup()->setItemDelegate(new QStyledItemDelegate(this));
    completer->setCompletionMode(QCompleter::PopupCompletion);
#if defined(Q_OS_WIN)
    completer->setCaseSensitivity(Qt::CaseInsensitive);
#else
    completer->setCaseSensitivity(Qt::CaseSensitive);
#endif
    m_dirCB->lineEdit()->setCompleter(completer);
    connect(m_dirCB, SIGNAL(activated(int)),
            this, SLOT(handleEntryActivated(int)));
    m_dirCB->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

    QLabel *imgLabel = new QLabel(tr("Image Folder"), this);

    m_imgFolderEdit = new VLineEdit(this);
    m_imgFolderEdit->setPlaceholderText(tr("Use global configuration (%1)")
                                          .arg(g_config->getImageFolderExt()));
    QString imgFolderTip = tr("Set the path of the image folder to store images "
                              "of files within the root directory.\nIf absolute path is used, "
                              "VNote will not manage those images."
                              "(empty to use global configuration)");
    m_imgFolderEdit->setToolTip(imgFolderTip);
    connect(m_imgFolderEdit, &QLineEdit::editingFinished,
            this, [this]() {
                if (checkIndex()) {
                    QString folder = m_imgFolderEdit->text();
                    if (folder.isEmpty() || VUtils::checkPathLegal(folder)) {
                        m_entries[m_index].m_imageFolder = folder;
                        if (m_entries[m_index].m_isStarred) {
                            g_config->setExplorerEntries(m_entries);
                        }
                    } else {
                        m_imgFolderEdit->setText(m_entries[m_index].m_imageFolder);
                    }
                }
            });

    // New file/dir.
    m_newFileBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/create_note_tb.svg"),
                                   "",
                                   this);
    m_newFileBtn->setToolTip(tr("New File"));
    m_newFileBtn->setProperty("FlatBtn", true);
    connect(m_newFileBtn, &QPushButton::clicked,
            this, &VExplorer::newFile);

    m_newDirBtn = new QPushButton(VIconUtils::buttonIcon(":/resources/icons/create_rootdir_tb.svg"),
                                  "",
                                  this);
    m_newDirBtn->setToolTip(tr("New Folder"));
    m_newDirBtn->setProperty("FlatBtn", true);
    connect(m_newDirBtn, &QPushButton::clicked,
            this, &VExplorer::newFolder);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(m_newFileBtn);
    btnLayout->addWidget(m_newDirBtn);
    btnLayout->setContentsMargins(0, 0, 0, 0);

    QFileSystemModel *dirModel = new QFileSystemModel(this);
    dirModel->setRootPath("");
    m_tree = new QTreeView(this);
    m_tree->setModel(dirModel);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tree, &QTreeView::customContextMenuRequested,
            this, &VExplorer::handleContextMenuRequested);
    connect(m_tree, &QTreeView::activated,
            this, [this](const QModelIndex &p_index) {
                QFileSystemModel *model = static_cast<QFileSystemModel *>(m_tree->model());
                if (!model->isDir(p_index)) {
                    QStringList files;
                    files << model->filePath(p_index);
                    openFiles(files, g_config->getNoteOpenMode());
                }
            });
    connect(m_tree, &QTreeView::expanded,
            this, &VExplorer::resizeTreeToContents);
    connect(m_tree, &QTreeView::collapsed,
            this, &VExplorer::resizeTreeToContents);
    connect(dirModel, &QFileSystemModel::directoryLoaded,
            this, &VExplorer::resizeTreeToContents);

    m_tree->setHeaderHidden(true);
    // Show only the Name column.
    for (int i = 1; i < dirModel->columnCount(); ++i) {
        m_tree->hideColumn(i);
    }

    QVBoxLayout *mainLayout = new QVBoxLayout();
    mainLayout->addLayout(dirLayout);
    mainLayout->addWidget(m_dirCB);
    mainLayout->addWidget(imgLabel);
    mainLayout->addWidget(m_imgFolderEdit);
    mainLayout->addLayout(btnLayout);
    mainLayout->addWidget(m_tree);
    mainLayout->setContentsMargins(3, 0, 3, 0);

    setLayout(mainLayout);
}

void VExplorer::init()
{
    if (m_initialized) {
        return;
    }

    m_initialized = true;

    setupUI();

    QShortcut *infoShortcut = new QShortcut(QKeySequence(c_infoShortcutSequence), this);
    infoShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(infoShortcut, &QShortcut::activated,
            this, [this]() {
                QModelIndexList selectedIdx = m_tree->selectionModel()->selectedRows();
                if (selectedIdx.size() == 1) {
                    QFileSystemModel *model = static_cast<QFileSystemModel *>(m_tree->model());
                    QString filePath = model->filePath(selectedIdx[0]);
                    renameFile(filePath);
                }
            });

    g_config->getExplorerEntries(m_entries);
    m_index = g_config->getExplorerCurrentIndex();
    if (m_entries.isEmpty()) {
        m_index = -1;
        g_config->setExplorerCurrentIndex(m_index);
    } else if (m_index < 0 || m_index >= m_entries.size()) {
        m_index = 0;
        g_config->setExplorerCurrentIndex(m_index);
    }

    updateDirectoryComboBox();

    setCurrentEntry(m_index);
}

void VExplorer::showEvent(QShowEvent *p_event)
{
    init();

    QWidget::showEvent(p_event);
}

void VExplorer::focusInEvent(QFocusEvent *p_event)
{
    init();

    QWidget::focusInEvent(p_event);

    if (m_index < 0) {
        m_openBtn->setFocus();
    } else {
        m_tree->setFocus();
    }
}

void VExplorer::updateUI()
{

}

void VExplorer::updateDirectoryComboBox()
{
    m_dirCB->clear();

    for (int i = 0; i < m_entries.size(); ++i) {
        m_dirCB->addItem(QDir::toNativeSeparators(m_entries[i].m_directory));
        m_dirCB->setItemData(i, m_entries[i].m_directory, Qt::ToolTipRole);
    }

    m_dirCB->setCurrentIndex(m_index);
}

int VExplorer::addEntry(const QString &p_dir)
{
    if (p_dir.isEmpty() || !QFileInfo::exists(p_dir) || !QDir::isAbsolutePath(p_dir)) {
        return -1;
    }

    QString dir(QDir::cleanPath(p_dir));
    int idx = -1;
    for (idx = 0; idx < m_entries.size(); ++idx) {
        if (VUtils::equalPath(dir, m_entries[idx].m_directory)) {
            break;
        }
    }

    if (idx < m_entries.size()) {
        return idx;
    }

    m_entries.append(VExplorerEntry(QFileInfo(dir).absoluteFilePath(), ""));
    return m_entries.size() - 1;
}

void VExplorer::handleEntryActivated(int p_idx)
{
    bool valid = false;
    QString imgFolder;

    if (p_idx >= 0 &&  p_idx < m_entries.size()) {
        valid = true;
        imgFolder = m_entries[p_idx].m_imageFolder;
    } else {
        p_idx = -1;
    }

    m_index = p_idx;

    updateExplorerEntryIndexInConfig();

    m_imgFolderEdit->setText(imgFolder);
    m_imgFolderEdit->setEnabled(valid);

    m_openLocationBtn->setEnabled(valid);
    m_newFileBtn->setEnabled(valid);
    m_newDirBtn->setEnabled(valid);

    updateStarButton();

    // Check if exists.
    if (checkIndex() && !QFileInfo::exists(m_entries[m_index].m_directory)) {
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Fail to open directory <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle)
                              .arg(m_entries[m_index].m_directory),
                            tr("Please check if the directory exists."),
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            g_mainWin);
    }

    updateTree();
}

void VExplorer::updateStarButton()
{
    // -1 for disabled, 0 for star, 1 for unstar.
    int star = -1;

    if (checkIndex()) {
        star = m_entries[m_index].m_isStarred ? 1 : 0;
    }

    bool enabled = true;
    switch (star) {
    default:
        enabled = false;
        V_FALLTHROUGH;
    case 0:
        m_starBtn->setEnabled(enabled);
        m_starBtn->setIcon(VIconUtils::buttonIcon(":/resources/icons/star.svg"));
        m_starBtn->setToolTip(tr("Star"));
        break;

    case 1:
        m_starBtn->setEnabled(enabled);
        m_starBtn->setIcon(VIconUtils::buttonIcon(":/resources/icons/unstar.svg"));
        m_starBtn->setToolTip(tr("Unstar"));
        break;
    }
}

void VExplorer::setCurrentEntry(int p_index)
{
    m_dirCB->setCurrentIndex(p_index);

    handleEntryActivated(p_index);
}

void VExplorer::updateExplorerEntryIndexInConfig()
{
    int idx = -1;
    if (m_index >= 0 && m_index < m_entries.size()) {
        int starIdx = -1;
        for (int j = 0; j <= m_index; ++j) {
            if (m_entries[j].m_isStarred) {
                ++starIdx;
            }
        }

        if (starIdx > -1) {
            idx = starIdx;
        }
    }

    g_config->setExplorerCurrentIndex(idx);
}

void VExplorer::updateTree()
{
    if (checkIndex()) {
        QString pa = QDir::cleanPath(m_entries[m_index].m_directory);
        QFileSystemModel *model = static_cast<QFileSystemModel *>(m_tree->model());
        model->setRootPath(pa);
        const QModelIndex rootIndex = model->index(pa);
        if (rootIndex.isValid()) {
            m_tree->setRootIndex(rootIndex);
            resizeTreeToContents();
            return;
        } else {
            model->setRootPath("");
        }
    }

    m_tree->reset();
    resizeTreeToContents();
}

void VExplorer::handleContextMenuRequested(QPoint p_pos)
{
    QModelIndex index = m_tree->indexAt(p_pos);
    if (!index.isValid()) {
        return;
    }

    QMenu menu(this);
    menu.setToolTipsVisible(true);

    QFileSystemModel *model = static_cast<QFileSystemModel *>(m_tree->model());
    QModelIndexList selectedIdx = m_tree->selectionModel()->selectedRows();
    if (selectedIdx.size() == 1 && model->isDir(selectedIdx[0])) {
        QString filePath = model->filePath(selectedIdx[0]);

        QAction *setRootAct = new QAction(VIconUtils::menuIcon(":/resources/icons/explore_root.svg"),
                                          tr("Set As Root"),
                                          &menu);
        setRootAct->setToolTip(tr("Set current folder as the root directory to explore"));
        connect(setRootAct, &QAction::triggered,
                this, [this, filePath]() {
                    int idx = addEntry(filePath);
                    updateDirectoryComboBox();
                    if (idx != -1) {
                        setCurrentEntry(idx);
                    }
                });
        menu.addAction(setRootAct);

        QAction *openLocationAct = new QAction(VIconUtils::menuIcon(":/resources/icons/open_location.svg"),
                                               tr("Open Folder Location"),
                                               &menu);
        openLocationAct->setToolTip(tr("Explore this folder in operating system"));
        connect(openLocationAct, &QAction::triggered,
                this, [this, filePath]() {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
                });
        menu.addAction(openLocationAct);

        menu.addSeparator();

        QAction *fileInfoAct = new QAction(VIconUtils::menuIcon(":/resources/icons/note_info.svg"),
                                           tr("&Info (Rename)\t%1").arg(VUtils::getShortcutText(c_infoShortcutSequence)),
                                           &menu);
        fileInfoAct->setToolTip(tr("View and edit current folder's information"));
        connect(fileInfoAct, &QAction::triggered,
                this, [this, filePath]() {
                    renameFile(filePath);
                });
        menu.addAction(fileInfoAct);

        menu.exec(m_tree->mapToGlobal(p_pos));
        return;
    }

    bool allFiles = true;
    QStringList selectedFiles;
    for (auto const & it : selectedIdx) {
        if (model->isDir(it)) {
            allFiles = false;
            break;
        }

        selectedFiles << model->filePath(it);
    }

    if (!allFiles) {
        return;
    }

    // Only files are selected.
    // We do not support copy/cut/deletion of files.
    QAction *openInReadAct = new QAction(VIconUtils::menuIcon(":/resources/icons/reading.svg"),
                                         tr("Open In Read Mode"),
                                         &menu);
    openInReadAct->setToolTip(tr("Open selected files in read mode"));
    connect(openInReadAct, &QAction::triggered,
            this, [this, selectedFiles]() {
                openFiles(selectedFiles, OpenFileMode::Read, true);
            });
    menu.addAction(openInReadAct);

    QAction *openInEditAct = new QAction(VIconUtils::menuIcon(":/resources/icons/editing.svg"),
                                         tr("Open In Edit Mode"),
                                         &menu);
    openInEditAct->setToolTip(tr("Open selected files in edit mode"));
    connect(openInEditAct, &QAction::triggered,
            this, [this, selectedFiles]() {
                openFiles(selectedFiles, OpenFileMode::Edit, true);
            });
    menu.addAction(openInEditAct);

    menu.addSeparator();

    if (selectedFiles.size() == 1) {
        QAction *openLocationAct = new QAction(VIconUtils::menuIcon(":/resources/icons/open_location.svg"),
                                               tr("Open File Location"),
                                               &menu);
        openLocationAct->setToolTip(tr("Explore the folder containing this file in operating system"));
        connect(openLocationAct, &QAction::triggered,
                this, [this, filePath = selectedFiles[0]] () {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(VUtils::basePathFromPath(filePath)));
                });
        menu.addAction(openLocationAct);

        menu.addSeparator();
    }

    QAction *addToCartAct = new QAction(VIconUtils::menuIcon(":/resources/icons/cart.svg"),
                                        tr("Add To Cart"),
                                        &menu);
    addToCartAct->setToolTip(tr("Add selected files to Cart for further processing"));
    connect(addToCartAct, &QAction::triggered,
            this, [this, selectedFiles]() {
                VCart *cart = g_mainWin->getCart();
                for (int i = 0; i < selectedFiles.size(); ++i) {
                    cart->addFile(selectedFiles[i]);
                }

                g_mainWin->showStatusMessage(tr("%1 %2 added to Cart")
                                               .arg(selectedFiles.size())
                                               .arg(selectedFiles.size() > 1 ? tr("files") : tr("file")));
            });
    menu.addAction(addToCartAct);

    QAction *pinToHistoryAct = new QAction(VIconUtils::menuIcon(":/resources/icons/pin.svg"),
                                           tr("Pin To History"),
                                           &menu);
    pinToHistoryAct->setToolTip(tr("Pin selected files to History"));
    connect(pinToHistoryAct, &QAction::triggered,
            this, [this, selectedFiles]() {
                g_mainWin->getHistoryList()->pinFiles(selectedFiles);
                g_mainWin->showStatusMessage(tr("%1 %2 pinned to History")
                                               .arg(selectedFiles.size())
                                               .arg(selectedFiles.size() > 1 ? tr("files") : tr("file")));
            });
    menu.addAction(pinToHistoryAct);

    if (selectedFiles.size() == 1) {
        QAction *fileInfoAct = new QAction(VIconUtils::menuIcon(":/resources/icons/note_info.svg"),
                                           tr("&Info (Rename)\t%1").arg(VUtils::getShortcutText(c_infoShortcutSequence)),
                                           &menu);
        fileInfoAct->setToolTip(tr("View and edit current file's information"));
        connect(fileInfoAct, &QAction::triggered,
                this, [this, filePath = selectedFiles[0]]() {
                    renameFile(filePath);
                });
        menu.addAction(fileInfoAct);
    }

    menu.exec(m_tree->mapToGlobal(p_pos));
}

void VExplorer::openFiles(const QStringList &p_files,
                          OpenFileMode p_mode,
                          bool p_forceMode)
{
    if (!p_files.isEmpty()) {
        Q_ASSERT(checkIndex());
        QString imgFolder = m_entries[m_index].m_imageFolder;

        QVector<VFile *> vfiles = g_mainWin->openFiles(p_files, false, p_mode, p_forceMode, false);

        // Set image folder.
        for (auto it : vfiles) {
            if (it->getType() == FileType::Orphan) {
                static_cast<VOrphanFile *>(it)->setImageFolder(imgFolder);
            }
        }
    }
}

void VExplorer::resizeTreeToContents()
{
    m_tree->resizeColumnToContents(0);
}

// 1. When only one folder is selected, create a file in that folder.
// 2. Otherwise, create a file in the root directory.
void VExplorer::newFile()
{
    Q_ASSERT(checkIndex());

    QString parentDir;

    QFileSystemModel *model = static_cast<QFileSystemModel *>(m_tree->model());
    QModelIndexList selectedIdx = m_tree->selectionModel()->selectedRows();
    if (selectedIdx.size() == 1 && model->isDir(selectedIdx[0])) {
        parentDir = model->filePath(selectedIdx[0]);
    } else {
        parentDir = m_entries[m_index].m_directory;
    }

    qDebug() << "new file in" << parentDir;

    QString name = VUtils::promptForFileName(tr("New File"),
                                             tr("File name (%1):").arg(parentDir),
                                             "",
                                             parentDir,
                                             g_mainWin);

    if (name.isEmpty()) {
        return;
    }

    QDir paDir(parentDir);
    QString filePath = paDir.filePath(name);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Fail to create file" << filePath;
        VUtils::showMessage(QMessageBox::Warning,
                            tr("New File"),
                            tr("Fail to create file <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle)
                              .arg(filePath),
                            "",
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            g_mainWin);
        return;
    }

    file.close();

    m_tree->setFocus();

    // Select the file.
    const QModelIndex fileIndex = model->index(filePath);
    Q_ASSERT(fileIndex.isValid());
    m_tree->scrollTo(fileIndex);
    m_tree->clearSelection();
    m_tree->setCurrentIndex(fileIndex);
}

// 1. When only one folder is selected, create a folder in that folder.
// 2. Otherwise, create a folder in the root directory.
void VExplorer::newFolder()
{
    Q_ASSERT(checkIndex());

    QString parentDir;

    QFileSystemModel *model = static_cast<QFileSystemModel *>(m_tree->model());
    QModelIndexList selectedIdx = m_tree->selectionModel()->selectedRows();
    if (selectedIdx.size() == 1 && model->isDir(selectedIdx[0])) {
        parentDir = model->filePath(selectedIdx[0]);
    } else {
        parentDir = m_entries[m_index].m_directory;
    }

    qDebug() << "new folder in" << parentDir;

    QString name = VUtils::promptForFileName(tr("New Folder"),
                                             tr("Folder name (%1):").arg(parentDir),
                                             "",
                                             parentDir,
                                             g_mainWin);

    if (name.isEmpty()) {
        return;
    }

    QDir paDir(parentDir);
    QString folderPath = paDir.filePath(name);
    if (!paDir.mkdir(name)) {
        qWarning() << "Fail to create folder" << folderPath;
        VUtils::showMessage(QMessageBox::Warning,
                            tr("New Folder"),
                            tr("Fail to create folder <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle)
                              .arg(folderPath),
                            "",
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            g_mainWin);
        return;
    }

    m_tree->setFocus();

    // Select the folder.
    const QModelIndex folderIndex = model->index(folderPath);
    Q_ASSERT(folderIndex.isValid());
    m_tree->scrollTo(folderIndex);
    m_tree->clearSelection();
    m_tree->setCurrentIndex(folderIndex);
}

void VExplorer::renameFile(const QString &p_filePath)
{
    Q_ASSERT(checkIndex());

    QFileInfo fi(p_filePath);
    QString parentDir = fi.path();
    QString oldName = fi.fileName();

    QString name = VUtils::promptForFileName(tr("File Information"),
                                             tr("Rename file (%1):").arg(p_filePath),
                                             oldName,
                                             parentDir,
                                             g_mainWin);

    if (name.isEmpty()) {
        return;
    }

    QDir paDir(parentDir);
    if (!paDir.rename(oldName, name)) {
        qWarning() << "Fail to rename" << p_filePath;
        VUtils::showMessage(QMessageBox::Warning,
                            tr("File Information"),
                            tr("Fail to rename file <span style=\"%1\">%2</span>.")
                              .arg(g_config->c_dataTextStyle)
                              .arg(p_filePath),
                            "",
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            g_mainWin);
        return;
    }
}
