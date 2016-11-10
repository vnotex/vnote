#include <QtWidgets>
#include <QList>
#include "vmainwindow.h"
#include "vdirectorytree.h"
#include "vnote.h"
#include "vfilelist.h"
#include "vconfigmanager.h"
#include "dialog/vnewnotebookdialog.h"
#include "dialog/vnotebookinfodialog.h"
#include "utils/vutils.h"
#include "veditarea.h"
#include "voutline.h"

extern VConfigManager vconfig;

VMainWindow::VMainWindow(QWidget *parent)
    : QMainWindow(parent), notebookComboMuted(false)
{
    // Must be called before those who uses VConfigManager
    vnote = new VNote();
    initPredefinedColorPixmaps();
    setupUI();
    initActions();
    initToolBar();
    initMenuBar();
    initDockWindows();

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

    newNotebookBtn = new QPushButton(QIcon(":/resources/icons/create_notebook.svg"), "");
    newNotebookBtn->setToolTip(tr("Create a new notebook"));
    deleteNotebookBtn = new QPushButton(QIcon(":/resources/icons/delete_notebook.svg"), "");
    deleteNotebookBtn->setToolTip(tr("Delete current notebook"));
    notebookInfoBtn = new QPushButton(QIcon(":/resources/icons/notebook_info.svg"), "");
    notebookInfoBtn->setToolTip(tr("View and edit current notebook's information"));

    notebookComboBox = new QComboBox();
    notebookComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    directoryTree = new VDirectoryTree(vnote);

    QHBoxLayout *nbBtnLayout = new QHBoxLayout;
    nbBtnLayout->addWidget(notebookLabel);
    nbBtnLayout->addStretch();
    nbBtnLayout->addWidget(newNotebookBtn);
    nbBtnLayout->addWidget(deleteNotebookBtn);
    nbBtnLayout->addWidget(notebookInfoBtn);
    QHBoxLayout *dirBtnLayout = new QHBoxLayout;
    dirBtnLayout->addWidget(directoryLabel);
    dirBtnLayout->addStretch();
    QVBoxLayout *nbLayout = new QVBoxLayout;
    nbLayout->addLayout(nbBtnLayout);
    nbLayout->addWidget(notebookComboBox);
    nbLayout->addLayout(dirBtnLayout);
    nbLayout->addWidget(directoryTree);
    QWidget *nbContainer = new QWidget();
    nbContainer->setLayout(nbLayout);
    nbContainer->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);

    fileList = new VFileList(vnote);
    fileList->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);

    editArea = new VEditArea(vnote);
    editArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Main Splitter
    mainSplitter = new QSplitter();
    mainSplitter->addWidget(nbContainer);
    mainSplitter->addWidget(fileList);
    mainSplitter->addWidget(editArea);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 0);
    mainSplitter->setStretchFactor(2, 1);

    // Signals
    connect(notebookComboBox, SIGNAL(currentIndexChanged(int)), this,
            SLOT(setCurNotebookIndex(int)));

    connect(vnote, &VNote::notebooksRenamed,
            fileList, &VFileList::handleNotebookRenamed);
    connect(directoryTree, &VDirectoryTree::currentDirectoryChanged,
            fileList, &VFileList::setDirectory);
    connect(directoryTree, &VDirectoryTree::directoryRenamed,
            fileList, &VFileList::handleDirectoryRenamed);
    connect(fileList, &VFileList::directoryChanged,
            this, &VMainWindow::handleFileListDirectoryChanged);

    connect(fileList, &VFileList::fileClicked,
            editArea, &VEditArea::openFile);
    connect(fileList, &VFileList::fileDeleted,
            editArea, &VEditArea::closeFile);
    connect(fileList, &VFileList::fileCreated,
            editArea, &VEditArea::openFile);
    connect(vnote, &VNote::notebooksRenamed,
            editArea, &VEditArea::handleNotebookRenamed);
    connect(editArea, &VEditArea::curTabStatusChanged,
            this, &VMainWindow::updateToolbarFromTabChage);

    connect(newNotebookBtn, &QPushButton::clicked,
            this, &VMainWindow::onNewNotebookBtnClicked);
    connect(deleteNotebookBtn, &QPushButton::clicked,
            this, &VMainWindow::onDeleteNotebookBtnClicked);
    connect(notebookInfoBtn, &QPushButton::clicked,
            this, &VMainWindow::onNotebookInfoBtnClicked);

    connect(vnote, &VNote::notebooksChanged,
            this, &VMainWindow::updateNotebookComboBox);
    connect(vnote, &VNote::notebooksDeleted,
            this, &VMainWindow::notebookComboBoxDeleted);
    connect(vnote, &VNote::notebooksRenamed,
            this, &VMainWindow::notebookComboBoxRenamed);
    connect(vnote, &VNote::notebooksAdded,
            this, &VMainWindow::notebookComboBoxAdded);

    connect(this, &VMainWindow::curNotebookChanged,
            directoryTree, &VDirectoryTree::setNotebook);
    connect(vnote, &VNote::notebooksRenamed,
            directoryTree, &VDirectoryTree::handleNotebookRenamed);

    setCentralWidget(mainSplitter);
    // Create and show the status bar
    statusBar();
}

void VMainWindow::initActions()
{
    newRootDirAct = new QAction(QIcon(":/resources/icons/create_rootdir_tb.svg"),
                                tr("&New rood directory"), this);
    newRootDirAct->setStatusTip(tr("Create a new root directory"));
    connect(newRootDirAct, &QAction::triggered,
            directoryTree, &VDirectoryTree::newRootDirectory);
    newNoteAct = new QAction(QIcon(":/resources/icons/create_note_tb.svg"),
                             tr("&New note"), this);
    newNoteAct->setStatusTip(tr("Create a new note"));
    connect(newNoteAct, &QAction::triggered,
            fileList, &VFileList::newFile);

    editNoteAct = new QAction(QIcon(":/resources/icons/edit_note.svg"),
                              tr("&Edit"), this);
    editNoteAct->setStatusTip(tr("Edit current note"));
    connect(editNoteAct, &QAction::triggered,
            editArea, &VEditArea::editFile);

    discardExitAct = new QAction(QIcon(":/resources/icons/discard_exit.svg"),
                                 tr("Discard changes and exit"), this);
    discardExitAct->setStatusTip(tr("Discard changes and exit edit mode"));
    connect(discardExitAct, &QAction::triggered,
            editArea, &VEditArea::readFile);

    saveExitAct = new QAction(QIcon(":/resources/icons/save_exit.svg"),
                              tr("Save changes and exit"), this);
    saveExitAct->setStatusTip(tr("Save changes and exit edit mode"));
    connect(saveExitAct, &QAction::triggered,
            editArea, &VEditArea::saveAndReadFile);

    saveNoteAct = new QAction(QIcon(":/resources/icons/save_note.svg"),
                              tr("&Save"), this);
    saveNoteAct->setStatusTip(tr("Save current note"));
    saveNoteAct->setShortcut(QKeySequence::Save);
    connect(saveNoteAct, &QAction::triggered,
            editArea, &VEditArea::saveFile);

    viewAct = new QActionGroup(this);
    twoPanelViewAct = new QAction(QIcon(":/resources/icons/two_panels.svg"),
                                  tr("&Two Panels"), viewAct);
    twoPanelViewAct->setStatusTip(tr("Display the directory and notes browser panel"));
    twoPanelViewAct->setCheckable(true);
    twoPanelViewAct->setData(2);
    onePanelViewAct = new QAction(QIcon(":/resources/icons/one_panel.svg"),
                                  tr("&Single panel"), viewAct);
    onePanelViewAct->setStatusTip(tr("Display only the notes browser panel"));
    onePanelViewAct->setCheckable(true);
    onePanelViewAct->setData(1);
    expandViewAct = new QAction(QIcon(":/resources/icons/expand.svg"),
                                tr("&Expand"), viewAct);
    expandViewAct->setStatusTip(tr("Expand the editing area"));
    expandViewAct->setCheckable(true);
    expandViewAct->setData(0);
    connect(viewAct, &QActionGroup::triggered,
            this, &VMainWindow::changePanelView);
    // Must be called after setting up the signal to sync the state and settings
    twoPanelViewAct->setChecked(true);

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

    expandTabAct = new QAction(tr("&Expand tab"), this);
    expandTabAct->setStatusTip(tr("Expand tab to spaces"));
    expandTabAct->setCheckable(true);
    connect(expandTabAct, &QAction::triggered,
            this, &VMainWindow::changeExpandTab);

    tabStopWidthAct = new QActionGroup(this);
    twoSpaceTabAct = new QAction(tr("2 spaces"), tabStopWidthAct);
    twoSpaceTabAct->setStatusTip(tr("Expand tab to 2 spaces"));
    twoSpaceTabAct->setCheckable(true);
    twoSpaceTabAct->setData(2);
    fourSpaceTabAct = new QAction(tr("4 spaces"), tabStopWidthAct);
    fourSpaceTabAct->setStatusTip(tr("Expand tab to 4 spaces"));
    fourSpaceTabAct->setCheckable(true);
    fourSpaceTabAct->setData(4);
    eightSpaceTabAct = new QAction(tr("8 spaces"), tabStopWidthAct);
    eightSpaceTabAct->setStatusTip(tr("Expand tab to 8 spaces"));
    eightSpaceTabAct->setCheckable(true);
    eightSpaceTabAct->setData(8);
    connect(tabStopWidthAct, &QActionGroup::triggered,
            this, &VMainWindow::setTabStopWidth);

    backgroundColorAct = new QActionGroup(this);
    connect(backgroundColorAct, &QActionGroup::triggered,
            this, &VMainWindow::setEditorBackgroundColor);

    renderBackgroundAct = new QActionGroup(this);
    connect(renderBackgroundAct, &QActionGroup::triggered,
            this, &VMainWindow::setRenderBackgroundColor);
}

void VMainWindow::initToolBar()
{
    QToolBar *fileToolBar = addToolBar(tr("Note"));
    fileToolBar->addAction(newRootDirAct);
    fileToolBar->addAction(newNoteAct);
    fileToolBar->addAction(editNoteAct);
    fileToolBar->addAction(saveExitAct);
    fileToolBar->addAction(discardExitAct);
    fileToolBar->addAction(saveNoteAct);

    newRootDirAct->setEnabled(false);
    newNoteAct->setEnabled(false);
    editNoteAct->setEnabled(false);
    saveExitAct->setVisible(false);
    discardExitAct->setVisible(false);
    saveNoteAct->setVisible(false);

    QToolBar *viewToolBar = addToolBar(tr("View"));
    viewToolBar->addAction(twoPanelViewAct);
    viewToolBar->addAction(onePanelViewAct);
    viewToolBar->addAction(expandViewAct);
}

void VMainWindow::initMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    viewMenu = menuBar()->addMenu(tr("&View"));
    QMenu *markdownMenu = menuBar()->addMenu(tr("&Markdown"));
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));

    // File Menu
    fileMenu->addAction(importNoteAct);

    // Edit Menu
    editMenu->addAction(expandTabAct);
    if (vconfig.getIsExpandTab()) {
        expandTabAct->setChecked(true);
    } else {
        expandTabAct->setChecked(false);
    }
    QMenu *tabStopWidthMenu = editMenu->addMenu(tr("Tab stop width"));
    tabStopWidthMenu->addAction(twoSpaceTabAct);
    tabStopWidthMenu->addAction(fourSpaceTabAct);
    tabStopWidthMenu->addAction(eightSpaceTabAct);
    int tabStopWidth = vconfig.getTabStopWidth();
    switch (tabStopWidth) {
    case 2:
        twoSpaceTabAct->setChecked(true);
        break;
    case 4:
        fourSpaceTabAct->setChecked(true);
        break;
    case 8:
        eightSpaceTabAct->setChecked(true);
        break;
    default:
        qWarning() << "error: unsupported tab stop width" << tabStopWidth <<  "in config";
    }
    initEditorBackgroundMenu(editMenu);

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

    initRenderBackgroundMenu(markdownMenu);

    // Help menu
    helpMenu->addAction(aboutQtAct);
    helpMenu->addAction(aboutAct);
}

void VMainWindow::initDockWindows()
{
    QDockWidget *dock = new QDockWidget(tr("Tools"), this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    toolBox = new QToolBox(this);
    outline = new VOutline(this);
    connect(editArea, &VEditArea::outlineChanged,
            outline, &VOutline::updateOutline);
    connect(outline, &VOutline::outlineItemActivated,
            editArea, &VEditArea::handleOutlineItemActivated);
    connect(editArea, &VEditArea::curHeaderChanged,
            outline, &VOutline::updateCurHeader);
    toolBox->addItem(outline, QIcon(":/resources/icons/outline.svg"), tr("Outline"));
    dock->setWidget(toolBox);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    viewMenu->addAction(dock->toggleViewAction());
}

void VMainWindow::updateNotebookComboBox(const QVector<VNotebook> &notebooks)
{
    notebookComboMuted = true;
    notebookComboBox->clear();
    for (int i = 0; i < notebooks.size(); ++i) {
        notebookComboBox->addItem(notebooks[i].getName());
    }
    notebookComboMuted = false;

    int index = vconfig.getCurNotebookIndex();
    if (notebooks.isEmpty()) {
        index = -1;
    }
    if (notebookComboBox->currentIndex() == index) {
        setCurNotebookIndex(index);
    } else {
        notebookComboBox->setCurrentIndex(index);
    }
}

void VMainWindow::notebookComboBoxAdded(const QVector<VNotebook> &notebooks, int idx)
{
    notebookComboMuted = true;
    notebookComboBox->insertItem(idx, notebooks[idx].getName());
    notebookComboMuted = false;
    if (notebookComboBox->currentIndex() == vconfig.getCurNotebookIndex()) {
        setCurNotebookIndex(vconfig.getCurNotebookIndex());
    } else {
        notebookComboBox->setCurrentIndex(vconfig.getCurNotebookIndex());
    }
}

void VMainWindow::notebookComboBoxDeleted(const QVector<VNotebook> &notebooks, const QString &deletedName)
{
    notebookComboMuted = true;
    int nrItem = notebookComboBox->count();
    for (int i = 0; i < nrItem; ++i) {
        if (notebookComboBox->itemText(i) == deletedName) {
            notebookComboBox->removeItem(i);
            break;
        }
    }
    notebookComboMuted = false;

    if (notebookComboBox->currentIndex() == vconfig.getCurNotebookIndex()) {
        setCurNotebookIndex(vconfig.getCurNotebookIndex());
    } else {
        notebookComboBox->setCurrentIndex(vconfig.getCurNotebookIndex());
    }
    Q_ASSERT(notebooks.size() == notebookComboBox->count());
}

void VMainWindow::notebookComboBoxRenamed(const QVector<VNotebook> &notebooks,
                                          const QString &oldName, const QString &newName)
{
    int nrItem = notebookComboBox->count();
    for (int i = 0; i < nrItem; ++i) {
        if (notebookComboBox->itemText(i) == oldName) {
            notebookComboBox->setItemText(i, newName);
            break;
        }
    }
    // Renaming a notebook won't change current index
}

// Maybe called multiple times
void VMainWindow::setCurNotebookIndex(int index)
{
    if (notebookComboMuted) {
        return;
    }
    Q_ASSERT(index < vnote->getNotebooks().size());
    // Update directoryTree
    QString notebook;
    if (index > -1) {
        vconfig.setCurNotebookIndex(index);
        notebook = vnote->getNotebooks()[index].getName();
        newRootDirAct->setEnabled(true);
    } else {
        newRootDirAct->setEnabled(false);
    }
    emit curNotebookChanged(notebook);
}

void VMainWindow::onNewNotebookBtnClicked()
{
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
    msgBox.setDefaultButton(QMessageBox::Cancel);
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
    static QString lastPath = QDir::homePath();
    QStringList files = QFileDialog::getOpenFileNames(this,tr("Select files(HTML or Markdown) to be imported as notes"),
                                                      lastPath);
    if (files.isEmpty()) {
        return;
    }
    // Update lastPath
    lastPath = QFileInfo(files[0]).path();

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

void VMainWindow::changeExpandTab(bool checked)
{
    vconfig.setIsExpandTab(checked);
}

void VMainWindow::setTabStopWidth(QAction *action)
{
    if (!action) {
        return;
    }
    vconfig.setTabStopWidth(action->data().toInt());
}

void VMainWindow::setEditorBackgroundColor(QAction *action)
{
    if (!action) {
        return;
    }

    vconfig.setCurBackgroundColor(action->data().toString());
}

void VMainWindow::initPredefinedColorPixmaps()
{
    const QVector<VColor> &bgColors = vconfig.getPredefinedColors();
    predefinedColorPixmaps.clear();
    int size = 256;
    for (int i = 0; i < bgColors.size(); ++i) {
        // Generate QPixmap filled in this color
        QColor color(VUtils::QRgbFromString(bgColors[i].rgb));
        QPixmap pixmap(size, size);
        pixmap.fill(color);
        predefinedColorPixmaps.append(pixmap);
    }
}

void VMainWindow::initRenderBackgroundMenu(QMenu *menu)
{
    QMenu *renderBgMenu = menu->addMenu(tr("&Rendering background"));
    const QString &curBgColor = vconfig.getCurRenderBackgroundColor();
    QAction *tmpAct = new QAction(tr("System"), renderBackgroundAct);
    tmpAct->setStatusTip(tr("Use system's background color configuration for rendering"));
    tmpAct->setCheckable(true);
    tmpAct->setData("System");
    if (curBgColor == "System") {
        tmpAct->setChecked(true);
    }
    renderBgMenu->addAction(tmpAct);

    const QVector<VColor> &bgColors = vconfig.getPredefinedColors();
    for (int i = 0; i < bgColors.size(); ++i) {
        tmpAct = new QAction(bgColors[i].name, renderBackgroundAct);
        tmpAct->setStatusTip(tr("Set background color for rendering"));
        tmpAct->setCheckable(true);
        tmpAct->setData(bgColors[i].name);
        tmpAct->setIcon(QIcon(predefinedColorPixmaps[i]));
        if (curBgColor == bgColors[i].name) {
            tmpAct->setChecked(true);
        }

        renderBgMenu->addAction(tmpAct);
    }
}

void VMainWindow::initEditorBackgroundMenu(QMenu *menu)
{
    QMenu *backgroundColorMenu = menu->addMenu(tr("&Background Color"));
    // System background color
    const QString &curBgColor = vconfig.getCurBackgroundColor();
    QAction *tmpAct = new QAction(tr("System"), backgroundColorAct);
    tmpAct->setStatusTip(tr("Use system's background color configuration for editor"));
    tmpAct->setCheckable(true);
    tmpAct->setData("System");
    if (curBgColor == "System") {
        tmpAct->setChecked(true);
    }
    backgroundColorMenu->addAction(tmpAct);
    const QVector<VColor> &bgColors = vconfig.getPredefinedColors();
    for (int i = 0; i < bgColors.size(); ++i) {
        tmpAct = new QAction(bgColors[i].name, backgroundColorAct);
        tmpAct->setStatusTip(tr("Set background color for editor"));
        tmpAct->setCheckable(true);
        tmpAct->setData(bgColors[i].name);
        tmpAct->setIcon(QIcon(predefinedColorPixmaps[i]));
        if (curBgColor == bgColors[i].name) {
            tmpAct->setChecked(true);
        }

        backgroundColorMenu->addAction(tmpAct);
    }
}

void VMainWindow::setRenderBackgroundColor(QAction *action)
{
    if (!action) {
        return;
    }
    vconfig.setCurRenderBackgroundColor(action->data().toString());
    vnote->updateTemplate();
}

void VMainWindow::updateToolbarFromTabChage(const QString &notebook, const QString &relativePath,
                                            bool editMode, bool modifiable)
{
    if (notebook.isEmpty() || !modifiable) {
        editNoteAct->setEnabled(false);
        saveExitAct->setVisible(false);
        discardExitAct->setVisible(false);
        saveNoteAct->setVisible(false);
        return;
    } else if (editMode) {
        editNoteAct->setEnabled(false);
        saveExitAct->setVisible(true);
        discardExitAct->setVisible(true);
        saveNoteAct->setVisible(true);
    } else {
        editNoteAct->setEnabled(true);
        saveExitAct->setVisible(false);
        discardExitAct->setVisible(false);
        saveNoteAct->setVisible(false);
    }
}

void VMainWindow::changePanelView(QAction *action)
{
    if (!action) {
        return;
    }
    int nrPanel = action->data().toInt();

    changeSplitterView(nrPanel);
}

void VMainWindow::changeSplitterView(int nrPanel)
{
    switch (nrPanel) {
    case 0:
        // Expand
        mainSplitter->widget(0)->hide();
        mainSplitter->widget(1)->hide();
        mainSplitter->widget(2)->show();
        break;
    case 1:
        // Single panel
        mainSplitter->widget(0)->hide();
        mainSplitter->widget(1)->show();
        mainSplitter->widget(2)->show();
        break;
    case 2:
        // Two panels
        mainSplitter->widget(0)->show();
        mainSplitter->widget(1)->show();
        mainSplitter->widget(2)->show();
        break;
    default:
        qWarning() << "error: invalid panel number" << nrPanel;
    }
}

void VMainWindow::handleFileListDirectoryChanged(const QString &notebook, const QString &relativePath)
{
    if (relativePath.isEmpty()) {
        newNoteAct->setEnabled(false);
    } else {
        newNoteAct->setEnabled(true);
    }
}
