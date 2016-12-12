#include <QtWidgets>
#include <QList>
#include "vmainwindow.h"
#include "vdirectorytree.h"
#include "vnote.h"
#include "vfilelist.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "veditarea.h"
#include "voutline.h"
#include "vnotebookselector.h"
#include "vavatar.h"

extern VConfigManager vconfig;

VMainWindow::VMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowIcon(QIcon(":/resources/icons/vnote.ico"));
    // Must be called before those who uses VConfigManager
    vnote = new VNote(this);
    vnote->initPalette(palette());
    initPredefinedColorPixmaps();
    setupUI();
    initActions();
    initToolBar();
    initMenuBar();
    initDockWindows();
    initAvatar();

    restoreStateAndGeometry();
    setContextMenuPolicy(Qt::NoContextMenu);

    notebookSelector->update();
}

void VMainWindow::setupUI()
{
    QWidget *directoryPanel = setupDirectoryPanel();

    fileList = new VFileList();
    fileList->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);

    editArea = new VEditArea(vnote);
    editArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    fileList->setEditArea(editArea);
    directoryTree->setEditArea(editArea);
    notebookSelector->setEditArea(editArea);

    // Main Splitter
    mainSplitter = new QSplitter();
    mainSplitter->setObjectName("MainSplitter");
    mainSplitter->addWidget(directoryPanel);
    mainSplitter->addWidget(fileList);
    mainSplitter->addWidget(editArea);
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 0);
    mainSplitter->setStretchFactor(2, 1);

    // Signals
    connect(directoryTree, &VDirectoryTree::currentDirectoryChanged,
            fileList, &VFileList::setDirectory);
    connect(directoryTree, &VDirectoryTree::directoryUpdated,
            editArea, &VEditArea::handleDirectoryUpdated);

    connect(notebookSelector, &VNotebookSelector::notebookUpdated,
            editArea, &VEditArea::handleNotebookUpdated);

    connect(fileList, &VFileList::fileClicked,
            editArea, &VEditArea::openFile);
    connect(fileList, &VFileList::fileCreated,
            editArea, &VEditArea::openFile);
    connect(fileList, &VFileList::fileUpdated,
            editArea, &VEditArea::handleFileUpdated);
    connect(editArea, &VEditArea::curTabStatusChanged,
            this, &VMainWindow::handleCurTabStatusChanged);

    setCentralWidget(mainSplitter);
    // Create and show the status bar
    statusBar();
}

QWidget *VMainWindow::setupDirectoryPanel()
{
    notebookLabel = new QLabel(tr("Notebook"));
    notebookLabel->setProperty("TitleLabel", true);
    notebookLabel->setProperty("NotebookPanel", true);
    directoryLabel = new QLabel(tr("Directory"));
    directoryLabel->setProperty("TitleLabel", true);
    directoryLabel->setProperty("NotebookPanel", true);

    notebookSelector = new VNotebookSelector(vnote);
    notebookSelector->setObjectName("NotebookSelector");
    notebookSelector->setProperty("NotebookPanel", true);
    notebookSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

    directoryTree = new VDirectoryTree(vnote);
    directoryTree->setProperty("NotebookPanel", true);

    QVBoxLayout *nbLayout = new QVBoxLayout;
    nbLayout->addWidget(notebookLabel);
    nbLayout->addWidget(notebookSelector);
    nbLayout->addWidget(directoryLabel);
    nbLayout->addWidget(directoryTree);
    nbLayout->setContentsMargins(0, 0, 0, 0);
    nbLayout->setSpacing(0);
    QWidget *nbContainer = new QWidget();
    nbContainer->setObjectName("NotebookPanel");
    nbContainer->setLayout(nbLayout);
    nbContainer->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);

    connect(notebookSelector, &VNotebookSelector::curNotebookChanged,
            directoryTree, &VDirectoryTree::setNotebook);
    connect(notebookSelector, &VNotebookSelector::curNotebookChanged,
            this, &VMainWindow::handleCurrentNotebookChanged);

    connect(directoryTree, &VDirectoryTree::currentDirectoryChanged,
            this, &VMainWindow::handleCurrentDirectoryChanged);
    return nbContainer;
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

    noteInfoAct = new QAction(QIcon(":/resources/icons/note_info_tb.svg"),
                              tr("&Note info"), this);
    noteInfoAct->setStatusTip(tr("Current note information"));
    connect(noteInfoAct, &QAction::triggered,
            this, &VMainWindow::curEditFileInfo);

    deleteNoteAct = new QAction(QIcon(":/resources/icons/delete_note_tb.svg"),
                                tr("&Delete note"), this);
    deleteNoteAct->setStatusTip(tr("Delete current note"));
    connect(deleteNoteAct, &QAction::triggered,
            this, &VMainWindow::deleteCurNote);

    initEditActions();

    initViewActions();

    importNoteAct = new QAction(QIcon(":/resources/icons/import_note.svg"),
                                tr("&Import Notes From Files"), this);
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

    m_insertImageAct = new QAction(QIcon(":/resources/icons/insert_image.svg"),
                                   tr("Insert &Image"), this);
    m_insertImageAct->setStatusTip(tr("Insert an image from file"));
    connect(m_insertImageAct, &QAction::triggered,
            this, &VMainWindow::insertImage);

    expandTabAct = new QAction(tr("&Expand Tab"), this);
    expandTabAct->setStatusTip(tr("Expand tab to spaces"));
    expandTabAct->setCheckable(true);
    connect(expandTabAct, &QAction::triggered,
            this, &VMainWindow::changeExpandTab);

    tabStopWidthAct = new QActionGroup(this);
    twoSpaceTabAct = new QAction(tr("2 Spaces"), tabStopWidthAct);
    twoSpaceTabAct->setStatusTip(tr("Expand tab to 2 spaces"));
    twoSpaceTabAct->setCheckable(true);
    twoSpaceTabAct->setData(2);
    fourSpaceTabAct = new QAction(tr("4 Spaces"), tabStopWidthAct);
    fourSpaceTabAct->setStatusTip(tr("Expand tab to 4 spaces"));
    fourSpaceTabAct->setCheckable(true);
    fourSpaceTabAct->setData(4);
    eightSpaceTabAct = new QAction(tr("8 Spaces"), tabStopWidthAct);
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

void VMainWindow::initEditActions()
{
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

    QMenu *exitEditMenu = new QMenu(this);
    exitEditMenu->addAction(discardExitAct);

    saveExitAct = new QAction(QIcon(":/resources/icons/save_exit.svg"),
                              tr("Save changes and exit"), this);
    saveExitAct->setStatusTip(tr("Save changes and exit edit mode"));
    saveExitAct->setMenu(exitEditMenu);
    connect(saveExitAct, &QAction::triggered,
            editArea, &VEditArea::saveAndReadFile);

    saveNoteAct = new QAction(QIcon(":/resources/icons/save_note.svg"),
                              tr("&Save"), this);
    saveNoteAct->setStatusTip(tr("Save current note"));
    saveNoteAct->setShortcut(QKeySequence::Save);
    connect(saveNoteAct, &QAction::triggered,
            editArea, &VEditArea::saveFile);

}

void VMainWindow::initViewActions()
{
    onePanelViewAct = new QAction(QIcon(":/resources/icons/one_panel.svg"),
                                  tr("&Single Panel"), this);
    onePanelViewAct->setStatusTip(tr("Display only the notes browser panel"));
    connect(onePanelViewAct, &QAction::triggered,
            this, &VMainWindow::onePanelView);

    twoPanelViewAct = new QAction(QIcon(":/resources/icons/two_panels.svg"),
                                  tr("&Two Panels"), this);
    twoPanelViewAct->setStatusTip(tr("Display the directory and notes browser panel"));
    connect(twoPanelViewAct, &QAction::triggered,
            this, &VMainWindow::twoPanelView);

    QMenu *panelMenu = new QMenu(this);
    panelMenu->addAction(onePanelViewAct);
    panelMenu->addAction(twoPanelViewAct);

    expandViewAct = new QAction(QIcon(":/resources/icons/expand.svg"),
                                tr("&Expand"), this);
    expandViewAct->setStatusTip(tr("Expand the editing area"));
    expandViewAct->setCheckable(true);
    expandViewAct->setMenu(panelMenu);
    connect(expandViewAct, &QAction::triggered,
            this, &VMainWindow::expandPanelView);

}

void VMainWindow::initToolBar()
{
    m_fileToolBar = addToolBar(tr("Note"));
    m_fileToolBar->setObjectName("NoteToolBar");
    m_fileToolBar->setMovable(false);
    m_fileToolBar->addAction(newRootDirAct);
    m_fileToolBar->addAction(newNoteAct);
    m_fileToolBar->addAction(noteInfoAct);
    m_fileToolBar->addAction(deleteNoteAct);
    m_fileToolBar->addSeparator();
    m_fileToolBar->addAction(editNoteAct);
    m_fileToolBar->addAction(saveExitAct);
    m_fileToolBar->addAction(saveNoteAct);
    m_fileToolBar->addSeparator();

    newRootDirAct->setEnabled(false);
    newNoteAct->setEnabled(false);
    noteInfoAct->setEnabled(false);
    deleteNoteAct->setEnabled(false);
    editNoteAct->setVisible(false);
    saveExitAct->setVisible(false);
    saveNoteAct->setVisible(false);

    m_viewToolBar = addToolBar(tr("View"));
    m_viewToolBar->setObjectName("ViewToolBar");
    m_viewToolBar->setMovable(false);
    m_viewToolBar->addAction(expandViewAct);
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
    editMenu->addAction(m_insertImageAct);
    editMenu->addSeparator();
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
    toolDock = new QDockWidget(tr("Tools"), this);
    toolDock->setObjectName("tools_dock");
    toolDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    toolBox = new QToolBox(this);
    outline = new VOutline(this);
    connect(editArea, &VEditArea::outlineChanged,
            outline, &VOutline::updateOutline);
    connect(outline, &VOutline::outlineItemActivated,
            editArea, &VEditArea::handleOutlineItemActivated);
    connect(editArea, &VEditArea::curHeaderChanged,
            outline, &VOutline::updateCurHeader);
    toolBox->addItem(outline, QIcon(":/resources/icons/outline.svg"), tr("Outline"));
    toolDock->setWidget(toolBox);
    addDockWidget(Qt::RightDockWidgetArea, toolDock);
    viewMenu->addAction(toolDock->toggleViewAction());
}

void VMainWindow::initAvatar()
{
    m_avatar = new VAvatar(this);
    m_avatar->setAvatarPixmap(":/resources/icons/vnote.svg");
    m_avatar->setColor(vnote->getColorFromPalette("base-color"), vnote->getColorFromPalette("Indigo4"),
                       vnote->getColorFromPalette("teal4"));
    m_avatar->hide();
}

void VMainWindow::importNoteFromFile()
{
    static QString lastPath = QDir::homePath();
    QStringList files = QFileDialog::getOpenFileNames(this,
                                                      tr("Select files(HTML or Markdown) to be imported as notes"),
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

void VMainWindow::updateToolbarFromTabChage(const VFile *p_file, bool p_editMode)
{
    qDebug() << "update toolbar";
    if (!p_file) {
        editNoteAct->setVisible(false);
        saveExitAct->setVisible(false);
        saveNoteAct->setVisible(false);
        deleteNoteAct->setEnabled(false);
    } else if (p_editMode) {
        editNoteAct->setVisible(false);
        saveExitAct->setVisible(true);
        saveNoteAct->setVisible(true);
        deleteNoteAct->setEnabled(true);
    } else {
        editNoteAct->setVisible(true);
        saveExitAct->setVisible(false);
        saveNoteAct->setVisible(false);
        deleteNoteAct->setEnabled(true);
    }

    if (p_file) {
        noteInfoAct->setEnabled(true);
    } else {
        noteInfoAct->setEnabled(false);
    }
}

void VMainWindow::handleCurTabStatusChanged(const VFile *p_file, const VEditTab *p_editTab, bool p_editMode)
{
    updateToolbarFromTabChage(p_file, p_editMode);

    QString title;
    if (p_file) {
        title = QString("[%1] %2").arg(p_file->getNotebook()).arg(p_file->retrivePath());
        if (p_file->isModified()) {
            title.append('*');
        }
    }
    updateWindowTitle(title);
    m_curFile = const_cast<VFile *>(p_file);
    m_curTab = const_cast<VEditTab *>(p_editTab);
}

void VMainWindow::onePanelView()
{
    changeSplitterView(1);
    expandViewAct->setChecked(false);
}

void VMainWindow::twoPanelView()
{
    changeSplitterView(2);
    expandViewAct->setChecked(false);
}

void VMainWindow::expandPanelView(bool p_checked)
{
    int nrSplits = 0;
    if (p_checked) {
        nrSplits = 0;
    } else {
        nrSplits = 2;
    }
    changeSplitterView(nrSplits);
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

void VMainWindow::updateWindowTitle(const QString &str)
{
    QString title = "VNote";
    if (!str.isEmpty()) {
        title = title + " - " + str;
    }
    setWindowTitle(title);
}

void VMainWindow::curEditFileInfo()
{
    Q_ASSERT(m_curFile);
    fileList->fileInfo(m_curFile);
}

void VMainWindow::deleteCurNote()
{
    Q_ASSERT(m_curFile);
    fileList->deleteFile(m_curFile);
}

void VMainWindow::closeEvent(QCloseEvent *event)
{
    if (!editArea->closeAllFiles(false)) {
        // Fail to close all the opened files, cancel closing app
        event->ignore();
        return;
    }
    saveStateAndGeometry();
    QMainWindow::closeEvent(event);
}

void VMainWindow::saveStateAndGeometry()
{
    vconfig.setMainWindowGeometry(saveGeometry());
    vconfig.setMainWindowState(saveState());
    vconfig.setToolsDockChecked(toolDock->isVisible());
}

void VMainWindow::restoreStateAndGeometry()
{
    const QByteArray &geometry = vconfig.getMainWindowGeometry();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }
    const QByteArray &state = vconfig.getMainWindowState();
    if (!state.isEmpty()) {
        restoreState(state);
    }
    toolDock->setVisible(vconfig.getToolsDockChecked());
}

const QVector<QPair<QString, QString> >& VMainWindow::getPalette() const
{
    return vnote->getPalette();
}

void VMainWindow::handleCurrentDirectoryChanged(const VDirectory *p_dir)
{
    newNoteAct->setEnabled(p_dir);
}

void VMainWindow::handleCurrentNotebookChanged(const VNotebook *p_notebook)
{
    newRootDirAct->setEnabled(p_notebook);
}

void VMainWindow::resizeEvent(QResizeEvent *event)
{
    repositionAvatar();
    QMainWindow::resizeEvent(event);
}

void VMainWindow::repositionAvatar()
{
    int diameter = mainSplitter->pos().y();
    int x = width() - diameter - 5;
    int y = 0;
    qDebug() << "avatar:" << diameter << x << y;
    m_avatar->setDiameter(diameter);
    m_avatar->move(x, y);
    m_avatar->show();
}

void VMainWindow::insertImage()
{
    if (!m_curTab) {
        return;
    }
    Q_ASSERT(m_curTab == editArea->currentEditTab());
    m_curTab->insertImage();
}
