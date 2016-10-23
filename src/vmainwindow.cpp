#include <QtWidgets>
#include "vmainwindow.h"
#include "vdirectorytree.h"
#include "vnote.h"
#include "vfilelist.h"
#include "vtabwidget.h"
#include "vconfigmanager.h"
#include "dialog/vnewnotebookdialog.h"
#include "dialog/vnotebookinfodialog.h"

extern VConfigManager vconfig;

VMainWindow::VMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Must be called before those who uses VConfigManager
    vnote = new VNote();
    setupUI();
    initActions();
    initToolBar();
    initMenuBar();

    updateNotebookComboBox(vnote->getNotebooks());
}

VMainWindow::~VMainWindow()
{
    delete vnote;
}

void VMainWindow::setupUI()
{
    // Notebook directory browser tree
    notebookLabel = new QLabel(tr("Notebook"));
    directoryLabel = new QLabel(tr("Directory"));

    newNotebookBtn = new QPushButton(QIcon(":/resources/icons/create_notebook.png"), "");
    newNotebookBtn->setToolTip(tr("Create a new notebook"));
    deleteNotebookBtn = new QPushButton(QIcon(":/resources/icons/delete_notebook.png"), "");
    deleteNotebookBtn->setToolTip(tr("Delete current notebook"));
    notebookInfoBtn = new QPushButton(QIcon(":/resources/icons/notebook_info.png"), "");
    notebookInfoBtn->setToolTip(tr("View and edit current notebook's information"));

    newRootDirBtn = new QPushButton(QIcon(":/resources/icons/create_rootdir.png"), "");
    newRootDirBtn->setToolTip(tr("Create a new root directory"));
    deleteDirBtn = new QPushButton(QIcon(":/resources/icons/delete_dir.png"), "");
    deleteDirBtn->setToolTip(tr("Delete current directory"));
    dirInfoBtn = new QPushButton(QIcon(":/resources/icons/dir_info.png"), "");
    dirInfoBtn->setToolTip(tr("View and edit current directory's information"));

    notebookComboBox = new QComboBox();
    notebookComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    directoryTree = new VDirectoryTree();

    QHBoxLayout *nbBtnLayout = new QHBoxLayout;
    nbBtnLayout->addWidget(notebookLabel);
    nbBtnLayout->addStretch();
    nbBtnLayout->addWidget(newNotebookBtn);
    nbBtnLayout->addWidget(deleteNotebookBtn);
    nbBtnLayout->addWidget(notebookInfoBtn);
    QHBoxLayout *dirBtnLayout = new QHBoxLayout;
    dirBtnLayout->addWidget(directoryLabel);
    dirBtnLayout->addStretch();
    dirBtnLayout->addWidget(newRootDirBtn);
    dirBtnLayout->addWidget(deleteDirBtn);
    dirBtnLayout->addWidget(dirInfoBtn);
    QVBoxLayout *nbLayout = new QVBoxLayout;
    nbLayout->addLayout(nbBtnLayout);
    nbLayout->addWidget(notebookComboBox);
    nbLayout->addLayout(dirBtnLayout);
    nbLayout->addWidget(directoryTree);
    QWidget *nbContainer = new QWidget();
    nbContainer->setLayout(nbLayout);
    nbContainer->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);

    // File list widget
    fileList = new VFileList();
    fileList->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);

    // Editor tab widget
    tabs = new VTabWidget();
    tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    tabs->setTabBarAutoHide(true);

    // Main Splitter
    mainSplitter = new QSplitter();
    mainSplitter->addWidget(nbContainer);
    mainSplitter->addWidget(fileList);
    mainSplitter->addWidget(tabs);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setStretchFactor(2, 10);

    // Signals
    connect(notebookComboBox, SIGNAL(currentIndexChanged(int)), this,
            SLOT(setCurNotebookIndex(int)));
    connect(this, SIGNAL(curNotebookIndexChanged(const QString&)), directoryTree,
            SLOT(setTreePath(const QString&)));
    connect(directoryTree, &VDirectoryTree::currentDirectoryChanged,
            fileList, &VFileList::setDirectory);
    connect(fileList, &VFileList::fileClicked,
            tabs, &VTabWidget::openFile);
    connect(fileList, &VFileList::fileDeleted,
            tabs, &VTabWidget::closeFile);
    connect(fileList, &VFileList::fileCreated,
            tabs, &VTabWidget::openFile);
    connect(newNotebookBtn, &QPushButton::clicked,
            this, &VMainWindow::onNewNotebookBtnClicked);
    connect(deleteNotebookBtn, &QPushButton::clicked,
            this, &VMainWindow::onDeleteNotebookBtnClicked);
    connect(notebookInfoBtn, &QPushButton::clicked,
            this, &VMainWindow::onNotebookInfoBtnClicked);
    connect(vnote, &VNote::notebooksChanged,
            this, &VMainWindow::updateNotebookComboBox);

    setCentralWidget(mainSplitter);
    // Create and show the status bar
    statusBar();
}

void VMainWindow::initActions()
{
    editNoteAct = new QAction(tr("&Edit"), this);
    editNoteAct->setStatusTip(tr("Edit current note"));
    connect(editNoteAct, &QAction::triggered,
            tabs, &VTabWidget::editFile);

    readNoteAct = new QAction(tr("&Read"), this);
    readNoteAct->setStatusTip(tr("Open current note in read mode"));
    connect(readNoteAct, &QAction::triggered,
            tabs, &VTabWidget::readFile);

    saveNoteAct = new QAction(tr("&Save"), this);
    saveNoteAct->setStatusTip(tr("Save current note"));
    saveNoteAct->setShortcut(QKeySequence::Save);
    connect(saveNoteAct, &QAction::triggered,
            tabs, &VTabWidget::saveFile);

    importNoteAct = new QAction(tr("&Import note from file"), this);
    importNoteAct->setStatusTip(tr("Import notes into current directory from files"));
    connect(importNoteAct, &QAction::triggered,
            this, &VMainWindow::importNoteFromFile);

    converterAct = new QActionGroup(this);
    markedAct = new QAction(tr("Marked"), converterAct);
    markedAct->setStatusTip(tr("Use Marked to convert Markdown to HTML (Re-open current tabs to make it work)"));
    markedAct->setCheckable(true);
    markedAct->setData(int(MarkdownConverterType::Marked));
    hoedownAct = new QAction(tr("Hoedown"), converterAct);
    hoedownAct->setStatusTip(tr("Use Hoedown to convert Markdown to HTML (Re-open current tabs to make it work)"));
    hoedownAct->setCheckable(true);
    hoedownAct->setData(int(MarkdownConverterType::Hoedown));
    connect(converterAct, &QActionGroup::triggered,
            this, &VMainWindow::changeMarkdownConverter);

    aboutAct = new QAction(tr("&About"), this);
    aboutAct->setStatusTip(tr("Show information about VNote"));
    connect(aboutAct, &QAction::triggered,
            this, &VMainWindow::aboutMessage);
    aboutQtAct = new QAction(tr("About &Qt"), this);
    aboutQtAct->setStatusTip(tr("Show information about Qt"));
    connect(aboutQtAct, &QAction::triggered,
            qApp, &QApplication::aboutQt);
}

void VMainWindow::initToolBar()
{
    QToolBar *fileToolBar = addToolBar(tr("Note"));
    fileToolBar->setMovable(false);
    fileToolBar->addAction(editNoteAct);
    fileToolBar->addAction(readNoteAct);
    fileToolBar->addAction(saveNoteAct);
}

void VMainWindow::initMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QMenu *markdownMenu = menuBar()->addMenu(tr("&Markdown"));
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));

    // File Menu
    fileMenu->addAction(importNoteAct);

    // Markdown Menu
    QMenu *converterMenu = markdownMenu->addMenu(tr("&Converter"));
    converterMenu->addAction(hoedownAct);
    converterMenu->addAction(markedAct);
    MarkdownConverterType converterType = vconfig.getMdConverterType();
    if (converterType == MarkdownConverterType::Marked) {
        markedAct->setChecked(true);
    } else if (converterType == MarkdownConverterType::Hoedown) {
        hoedownAct->setChecked(true);
    }

    // Help menu
    helpMenu->addAction(aboutQtAct);
    helpMenu->addAction(aboutAct);
}

void VMainWindow::updateNotebookComboBox(const QVector<VNotebook> &notebooks)
{
    // Clearing and inserting items will emit the signal which corrupt the vconfig's
    // current index. We save it first and then set the combobox index to the
    // right one to resrote it.
    int curIndex = vconfig.getCurNotebookIndex();
    notebookComboBox->clear();
    if (notebooks.isEmpty()) {
        return;
    }
    for (int i = 0; i <notebooks.size(); ++i) {
        notebookComboBox->addItem(notebooks[i].getName());
    }

    qDebug() << "update notebook combobox with" << notebookComboBox->count()
             << "items, current notebook" << curIndex;
    notebookComboBox->setCurrentIndex(curIndex);
}

void VMainWindow::setCurNotebookIndex(int index)
{
    Q_ASSERT(index < vnote->getNotebooks().size());
    // Update directoryTree
    QString treePath;
    if (index > -1) {
        vconfig.setCurNotebookIndex(index);
        treePath = vnote->getNotebooks()[index].getPath();
    }
    emit curNotebookIndexChanged(treePath);
}

void VMainWindow::onNewNotebookBtnClicked()
{
    qDebug() << "request to create a notebook";
    QString info;
    QString defaultName("new_notebook");
    QString defaultPath;
    do {
        VNewNotebookDialog dialog(tr("Create a new notebook"), info, defaultName,
                                  defaultPath, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            QString path = dialog.getPathInput();
            if (isConflictWithExistingNotebooks(name)) {
                info = "Name already exists.";
                defaultName = name;
                defaultPath = path;
                continue;
            }
            vnote->createNotebook(name, path);
        }
        break;
    } while (true);
}

bool VMainWindow::isConflictWithExistingNotebooks(const QString &name)
{
    const QVector<VNotebook> &notebooks = vnote->getNotebooks();
    for (int i = 0; i < notebooks.size(); ++i) {
        if (notebooks[i].getName() == name) {
            return true;
        }
    }
    return false;
}

void VMainWindow::onDeleteNotebookBtnClicked()
{
    int curIndex = notebookComboBox->currentIndex();
    QString curName = vnote->getNotebooks()[curIndex].getName();
    QString curPath = vnote->getNotebooks()[curIndex].getPath();

    QMessageBox msgBox(QMessageBox::Warning, tr("Warning"), QString("Are you sure you want to delete notebook \"%1\"?")
                       .arg(curName), QMessageBox::Ok | QMessageBox::Cancel, this);
    msgBox.setInformativeText(QString("This will delete any files in this notebook (\"%1\").").arg(curPath));
    msgBox.setDefaultButton(QMessageBox::Ok);
    if (msgBox.exec() == QMessageBox::Ok) {
        vnote->removeNotebook(curName);
    }
}

void VMainWindow::onNotebookInfoBtnClicked()
{
    int curIndex = notebookComboBox->currentIndex();
    if (curIndex < 0) {
        return;
    }
    QString info;
    QString curName = vnote->getNotebooks()[curIndex].getName();
    QString defaultPath = vnote->getNotebooks()[curIndex].getPath();
    QString defaultName(curName);
    do {
        VNotebookInfoDialog dialog(tr("Notebook information"), info, defaultName,
                                  defaultPath, this);
        if (dialog.exec() == QDialog::Accepted) {
            QString name = dialog.getNameInput();
            if (name == curName) {
                return;
            }
            if (isConflictWithExistingNotebooks(name)) {
                info = "Name already exists.";
                defaultName = name;
                continue;
            }
            vnote->renameNotebook(curName, name);
        }
        break;
    } while (true);
}

void VMainWindow::importNoteFromFile()
{
    QStringList files = QFileDialog::getOpenFileNames(this,tr("Select files(HTML or Markdown) to be imported as notes"),
                                                      QDir::homePath());
    if (files.isEmpty()) {
        return;
    }
    QStringList failedFiles;
    for (int i = 0; i < files.size(); ++i) {
        bool ret = fileList->importFile(files[i]);
        if (!ret) {
            failedFiles.append(files[i]);
        }
    }
    QMessageBox msgBox(QMessageBox::Information, tr("Import note from file"),
                       QString("Imported notes: %1 succeed, %2 failed.")
                       .arg(files.size() - failedFiles.size()).arg(failedFiles.size()),
                       QMessageBox::Ok, this);
    if (!failedFiles.isEmpty()) {
        msgBox.setInformativeText(tr("Failed to import files may be due to name conflicts."));
    }
    msgBox.exec();
}

void VMainWindow::changeMarkdownConverter(QAction *action)
{
    if (!action) {
        return;
    }
    MarkdownConverterType type = (MarkdownConverterType)action->data().toInt();
    qDebug() << "switch to converter" << type;
    vconfig.setMarkdownConverterType(type);
}

void VMainWindow::aboutMessage()
{
    QMessageBox::about(this, tr("About VNote"),
                       tr("VNote is a Vim-inspired note taking application for Markdown.\n"
                          "Visit https://github.com/tamlok/vnote.git for more information."));
}
