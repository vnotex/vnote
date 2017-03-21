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
#include "dialog/vfindreplacedialog.h"
#include "dialog/vsettingsdialog.h"

extern VConfigManager vconfig;

VNote *g_vnote;

VMainWindow::VMainWindow(QWidget *parent)
    : QMainWindow(parent), m_onePanel(false)
{
    setWindowIcon(QIcon(":/resources/icons/vnote.ico"));
    vnote = new VNote(this);
    g_vnote = vnote;
    vnote->initPalette(palette());
    initPredefinedColorPixmaps();

    setupUI();

    initMenuBar();
    initToolBar();
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
    m_findReplaceDialog = editArea->getFindReplaceDialog();
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
    connect(m_findReplaceDialog, &VFindReplaceDialog::findTextChanged,
            this, &VMainWindow::handleFindDialogTextChanged);

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

void VMainWindow::initToolBar()
{
    initFileToolBar();
    initViewToolBar();
}

void VMainWindow::initViewToolBar()
{
    QToolBar *viewToolBar = addToolBar(tr("View"));
    viewToolBar->setObjectName("ViewToolBar");
    viewToolBar->setMovable(false);

    QAction *onePanelViewAct = new QAction(QIcon(":/resources/icons/one_panel.svg"),
                                           tr("&Single Panel"), this);
    onePanelViewAct->setStatusTip(tr("Display only the note list panel"));
    connect(onePanelViewAct, &QAction::triggered,
            this, &VMainWindow::onePanelView);

    QAction *twoPanelViewAct = new QAction(QIcon(":/resources/icons/two_panels.svg"),
                                           tr("&Two Panels"), this);
    twoPanelViewAct->setStatusTip(tr("Display both the directory and note list panel"));
    connect(twoPanelViewAct, &QAction::triggered,
            this, &VMainWindow::twoPanelView);

    QMenu *panelMenu = new QMenu(this);
    panelMenu->addAction(onePanelViewAct);
    panelMenu->addAction(twoPanelViewAct);

    expandViewAct = new QAction(QIcon(":/resources/icons/expand.svg"),
                                         tr("Expand"), this);
    expandViewAct->setStatusTip(tr("Expand the edit area"));
    expandViewAct->setCheckable(true);
    expandViewAct->setShortcut(QKeySequence("Ctrl+E"));
    expandViewAct->setMenu(panelMenu);
    connect(expandViewAct, &QAction::triggered,
            this, &VMainWindow::expandPanelView);

    viewToolBar->addAction(expandViewAct);
}

void VMainWindow::initFileToolBar()
{
    QToolBar *fileToolBar = addToolBar(tr("Note"));
    fileToolBar->setObjectName("NoteToolBar");
    fileToolBar->setMovable(false);

    newRootDirAct = new QAction(QIcon(":/resources/icons/create_rootdir_tb.svg"),
                                tr("New &Root Directory"), this);
    newRootDirAct->setStatusTip(tr("Create a root directory in current notebook"));
    connect(newRootDirAct, &QAction::triggered,
            directoryTree, &VDirectoryTree::newRootDirectory);

    newNoteAct = new QAction(QIcon(":/resources/icons/create_note_tb.svg"),
                             tr("New &Note"), this);
    newNoteAct->setStatusTip(tr("Create a note in current directory"));
    newNoteAct->setShortcut(QKeySequence::New);
    connect(newNoteAct, &QAction::triggered,
            fileList, &VFileList::newFile);

    noteInfoAct = new QAction(QIcon(":/resources/icons/note_info_tb.svg"),
                              tr("Note &Info"), this);
    noteInfoAct->setStatusTip(tr("View and edit current note's information"));
    connect(noteInfoAct, &QAction::triggered,
            this, &VMainWindow::curEditFileInfo);

    deleteNoteAct = new QAction(QIcon(":/resources/icons/delete_note_tb.svg"),
                                tr("&Delete Note"), this);
    deleteNoteAct->setStatusTip(tr("Delete current note"));
    connect(deleteNoteAct, &QAction::triggered,
            this, &VMainWindow::deleteCurNote);

    editNoteAct = new QAction(QIcon(":/resources/icons/edit_note.svg"),
                              tr("&Edit"), this);
    editNoteAct->setStatusTip(tr("Edit current note"));
    editNoteAct->setShortcut(QKeySequence("Ctrl+W"));
    connect(editNoteAct, &QAction::triggered,
            editArea, &VEditArea::editFile);

    discardExitAct = new QAction(QIcon(":/resources/icons/discard_exit.svg"),
                                 tr("Discard Changes And Exit"), this);
    discardExitAct->setStatusTip(tr("Discard changes and exit edit mode"));
    discardExitAct->setShortcut(QKeySequence("Ctrl+Q"));
    connect(discardExitAct, &QAction::triggered,
            editArea, &VEditArea::readFile);

    QMenu *exitEditMenu = new QMenu(this);
    exitEditMenu->addAction(discardExitAct);

    saveExitAct = new QAction(QIcon(":/resources/icons/save_exit.svg"),
                              tr("Save Changes And Exit"), this);
    saveExitAct->setStatusTip(tr("Save changes and exit edit mode"));
    saveExitAct->setMenu(exitEditMenu);
    saveExitAct->setShortcut(QKeySequence("Ctrl+R"));
    connect(saveExitAct, &QAction::triggered,
            editArea, &VEditArea::saveAndReadFile);

    saveNoteAct = new QAction(QIcon(":/resources/icons/save_note.svg"),
                              tr("Save"), this);
    saveNoteAct->setStatusTip(tr("Save changes to current note"));
    saveNoteAct->setShortcut(QKeySequence::Save);
    connect(saveNoteAct, &QAction::triggered,
            editArea, &VEditArea::saveFile);

    newRootDirAct->setEnabled(false);
    newNoteAct->setEnabled(false);
    noteInfoAct->setEnabled(false);
    deleteNoteAct->setEnabled(false);
    editNoteAct->setVisible(false);
    saveExitAct->setVisible(false);
    saveNoteAct->setVisible(false);

    fileToolBar->addAction(newRootDirAct);
    fileToolBar->addAction(newNoteAct);
    fileToolBar->addAction(noteInfoAct);
    fileToolBar->addAction(deleteNoteAct);
    fileToolBar->addSeparator();
    fileToolBar->addAction(editNoteAct);
    fileToolBar->addAction(saveExitAct);
    fileToolBar->addAction(saveNoteAct);
    fileToolBar->addSeparator();
}

void VMainWindow::initMenuBar()
{
    initFileMenu();
    initEditMenu();
    initViewMenu();
    initMarkdownMenu();
    initHelpMenu();
}

void VMainWindow::initHelpMenu()
{
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));

    QAction *aboutAct = new QAction(tr("&About"), this);
    aboutAct->setStatusTip(tr("View information about VNote"));
    connect(aboutAct, &QAction::triggered,
            this, &VMainWindow::aboutMessage);
    QAction *aboutQtAct = new QAction(tr("About &Qt"), this);
    aboutQtAct->setStatusTip(tr("View information about Qt"));
    connect(aboutQtAct, &QAction::triggered,
            qApp, &QApplication::aboutQt);

    helpMenu->addAction(aboutQtAct);
    helpMenu->addAction(aboutAct);
}

void VMainWindow::initMarkdownMenu()
{
    QMenu *markdownMenu = menuBar()->addMenu(tr("&Markdown"));
    QMenu *converterMenu = markdownMenu->addMenu(tr("&Converter"));

    QActionGroup *converterAct = new QActionGroup(this);
    QAction *markedAct = new QAction(tr("Marked"), converterAct);
    markedAct->setStatusTip(tr("Use Marked to convert Markdown to HTML (re-open current tabs to make it work)"));
    markedAct->setCheckable(true);
    markedAct->setData(int(MarkdownConverterType::Marked));

    QAction *hoedownAct = new QAction(tr("Hoedown"), converterAct);
    hoedownAct->setStatusTip(tr("Use Hoedown to convert Markdown to HTML (re-open current tabs to make it work)"));
    hoedownAct->setCheckable(true);
    hoedownAct->setData(int(MarkdownConverterType::Hoedown));

    QAction *markdownitAct = new QAction(tr("Markdown-it"), converterAct);
    markdownitAct->setStatusTip(tr("Use Markdown-it to convert Markdown to HTML (re-open current tabs to make it work)"));
    markdownitAct->setCheckable(true);
    markdownitAct->setData(int(MarkdownConverterType::MarkdownIt));

    connect(converterAct, &QActionGroup::triggered,
            this, &VMainWindow::changeMarkdownConverter);
    converterMenu->addAction(hoedownAct);
    converterMenu->addAction(markedAct);
    converterMenu->addAction(markdownitAct);

    MarkdownConverterType converterType = vconfig.getMdConverterType();
    switch (converterType) {
    case MarkdownConverterType::Marked:
        markedAct->setChecked(true);
        break;

    case MarkdownConverterType::Hoedown:
        hoedownAct->setChecked(true);
        break;

    case MarkdownConverterType::MarkdownIt:
        markdownitAct->setChecked(true);
        break;

    default:
        Q_ASSERT(false);
    }

    initRenderBackgroundMenu(markdownMenu);

    QAction *mermaidAct = new QAction(tr("&Mermaid Diagram"), this);
    mermaidAct->setStatusTip(tr("Enable Mermaid for graph and diagram"));
    mermaidAct->setCheckable(true);
    connect(mermaidAct, &QAction::triggered,
            this, &VMainWindow::enableMermaid);
    markdownMenu->addAction(mermaidAct);

    mermaidAct->setChecked(vconfig.getEnableMermaid());
}

void VMainWindow::initViewMenu()
{
    viewMenu = menuBar()->addMenu(tr("&View"));
}

void VMainWindow::initFileMenu()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    // Import notes from files.
    m_importNoteAct = new QAction(QIcon(":/resources/icons/import_note.svg"),
                                        tr("&Import Notes From Files"), this);
    m_importNoteAct->setStatusTip(tr("Import notes from files into current directory"));
    connect(m_importNoteAct, &QAction::triggered,
            this, &VMainWindow::importNoteFromFile);
    m_importNoteAct->setEnabled(false);

    // Settings.
    QAction *settingsAct = new QAction(QIcon(":/resources/icons/settings.svg"),
                                       tr("Settings"), this);
    settingsAct->setStatusTip(tr("View and change settings for VNote"));
    connect(settingsAct, &QAction::triggered,
            this, &VMainWindow::viewSettings);

    fileMenu->addAction(m_importNoteAct);
    fileMenu->addSeparator();
    fileMenu->addAction(settingsAct);
}

void VMainWindow::initEditMenu()
{
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));

    // Insert image.
    m_insertImageAct = new QAction(QIcon(":/resources/icons/insert_image.svg"),
                                   tr("Insert &Image"), this);
    m_insertImageAct->setStatusTip(tr("Insert an image from file into current note"));
    connect(m_insertImageAct, &QAction::triggered,
            this, &VMainWindow::insertImage);

    // Find/Replace.
    m_findReplaceAct = new QAction(QIcon(":/resources/icons/find_replace.svg"),
                                   tr("Find/Replace"), this);
    m_findReplaceAct->setStatusTip(tr("Open Find/Replace dialog to search in current note"));
    m_findReplaceAct->setShortcut(QKeySequence::Find);
    connect(m_findReplaceAct, &QAction::triggered,
            this, &VMainWindow::openFindDialog);

    m_findNextAct = new QAction(tr("Find Next"), this);
    m_findNextAct->setStatusTip(tr("Find next occurence"));
    m_findNextAct->setShortcut(QKeySequence::FindNext);
    connect(m_findNextAct, SIGNAL(triggered(bool)),
            m_findReplaceDialog, SLOT(findNext()));

    m_findPreviousAct = new QAction(tr("Find Previous"), this);
    m_findPreviousAct->setStatusTip(tr("Find previous occurence"));
    m_findPreviousAct->setShortcut(QKeySequence::FindPrevious);
    connect(m_findPreviousAct, SIGNAL(triggered(bool)),
            m_findReplaceDialog, SLOT(findPrevious()));

    m_replaceAct = new QAction(tr("Replace"), this);
    m_replaceAct->setStatusTip(tr("Replace current occurence"));
    m_replaceAct->setShortcut(QKeySequence::Replace);
    connect(m_replaceAct, SIGNAL(triggered(bool)),
            m_findReplaceDialog, SLOT(replace()));

    m_replaceFindAct = new QAction(tr("Replace && Find"), this);
    m_replaceFindAct->setStatusTip(tr("Replace current occurence and find the next one"));
    connect(m_replaceFindAct, SIGNAL(triggered(bool)),
            m_findReplaceDialog, SLOT(replaceFind()));

    m_replaceAllAct = new QAction(tr("Replace All"), this);
    m_replaceAllAct->setStatusTip(tr("Replace all occurences in current note"));
    connect(m_replaceAllAct, SIGNAL(triggered(bool)),
            m_findReplaceDialog, SLOT(replaceAll()));

    QAction *searchedWordAct = new QAction(tr("Highlight Searched Pattern"), this);
    searchedWordAct->setStatusTip(tr("Highlight all occurences of searched pattern"));
    searchedWordAct->setCheckable(true);
    connect(searchedWordAct, &QAction::triggered,
            this, &VMainWindow::changeHighlightSearchedWord);

    // Expand Tab into spaces.
    QAction *expandTabAct = new QAction(tr("&Expand Tab"), this);
    expandTabAct->setStatusTip(tr("Expand entered Tab to spaces"));
    expandTabAct->setCheckable(true);
    connect(expandTabAct, &QAction::triggered,
            this, &VMainWindow::changeExpandTab);

    // Tab stop width.
    QActionGroup *tabStopWidthAct = new QActionGroup(this);
    QAction *twoSpaceTabAct = new QAction(tr("2 Spaces"), tabStopWidthAct);
    twoSpaceTabAct->setStatusTip(tr("Expand Tab to 2 spaces"));
    twoSpaceTabAct->setCheckable(true);
    twoSpaceTabAct->setData(2);
    QAction *fourSpaceTabAct = new QAction(tr("4 Spaces"), tabStopWidthAct);
    fourSpaceTabAct->setStatusTip(tr("Expand Tab to 4 spaces"));
    fourSpaceTabAct->setCheckable(true);
    fourSpaceTabAct->setData(4);
    QAction *eightSpaceTabAct = new QAction(tr("8 Spaces"), tabStopWidthAct);
    eightSpaceTabAct->setStatusTip(tr("Expand Tab to 8 spaces"));
    eightSpaceTabAct->setCheckable(true);
    eightSpaceTabAct->setData(8);
    connect(tabStopWidthAct, &QActionGroup::triggered,
            this, &VMainWindow::setTabStopWidth);

    // Highlight current cursor line.
    QAction *cursorLineAct = new QAction(tr("Highlight Cursor Line"), this);
    cursorLineAct->setStatusTip(tr("Highlight current cursor line"));
    cursorLineAct->setCheckable(true);
    connect(cursorLineAct, &QAction::triggered,
            this, &VMainWindow::changeHighlightCursorLine);

    // Highlight selected word.
    QAction *selectedWordAct = new QAction(tr("Highlight Selected Words"), this);
    selectedWordAct->setStatusTip(tr("Highlight all occurences of selected words"));
    selectedWordAct->setCheckable(true);
    connect(selectedWordAct, &QAction::triggered,
            this, &VMainWindow::changeHighlightSelectedWord);

    editMenu->addAction(m_insertImageAct);
    editMenu->addSeparator();
    m_insertImageAct->setEnabled(false);

    QMenu *findReplaceMenu = editMenu->addMenu(tr("Find/Replace"));
    findReplaceMenu->addAction(m_findReplaceAct);
    findReplaceMenu->addAction(m_findNextAct);
    findReplaceMenu->addAction(m_findPreviousAct);
    findReplaceMenu->addAction(m_replaceAct);
    findReplaceMenu->addAction(m_replaceFindAct);
    findReplaceMenu->addAction(m_replaceAllAct);
    findReplaceMenu->addSeparator();
    findReplaceMenu->addAction(searchedWordAct);
    if (vconfig.getHighlightSearchedWord()) {
        searchedWordAct->setChecked(true);
    } else {
        searchedWordAct->setChecked(false);
    }

    editMenu->addSeparator();
    m_findReplaceAct->setEnabled(false);
    m_findNextAct->setEnabled(false);
    m_findPreviousAct->setEnabled(false);
    m_replaceAct->setEnabled(false);
    m_replaceFindAct->setEnabled(false);
    m_replaceAllAct->setEnabled(false);

    editMenu->addAction(expandTabAct);
    if (vconfig.getIsExpandTab()) {
        expandTabAct->setChecked(true);
    } else {
        expandTabAct->setChecked(false);
    }
    QMenu *tabStopWidthMenu = editMenu->addMenu(tr("Tab Stop Width"));
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
        qWarning() << "unsupported tab stop width" << tabStopWidth <<  "in config";
    }
    initEditorBackgroundMenu(editMenu);
    editMenu->addSeparator();
    editMenu->addAction(cursorLineAct);
    if (vconfig.getHighlightCursorLine()) {
        cursorLineAct->setChecked(true);
    } else {
        cursorLineAct->setChecked(false);
    }

    editMenu->addAction(selectedWordAct);
    if (vconfig.getHighlightSelectedWord()) {
        selectedWordAct->setChecked(true);
    } else {
        selectedWordAct->setChecked(false);
    }
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
                       vnote->getColorFromPalette("Teal4"));
    m_avatar->hide();
}

void VMainWindow::importNoteFromFile()
{
    static QString lastPath = QDir::homePath();
    QStringList files = QFileDialog::getOpenFileNames(this,
                                                      tr("Select Files(HTML or Markdown) To Import"),
                                                      lastPath);
    if (files.isEmpty()) {
        return;
    }
    // Update lastPath
    lastPath = QFileInfo(files[0]).path();

    int failedFiles = 0;
    for (int i = 0; i < files.size(); ++i) {
        bool ret = fileList->importFile(files[i]);
        if (!ret) {
            ++failedFiles;
        }
    }
    QMessageBox msgBox(QMessageBox::Information, tr("Import Notes From File"),
                       tr("Imported notes: %1 succeed, %2 failed.")
                       .arg(files.size() - failedFiles).arg(failedFiles),
                       QMessageBox::Ok, this);
    if (failedFiles > 0) {
        msgBox.setInformativeText(tr("Fail to import files maybe due to name conflicts."));
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

void VMainWindow::enableMermaid(bool p_checked)
{
    vconfig.setEnableMermaid(p_checked);
}

void VMainWindow::changeHighlightCursorLine(bool p_checked)
{
    vconfig.setHighlightCursorLine(p_checked);
}

void VMainWindow::changeHighlightSelectedWord(bool p_checked)
{
    vconfig.setHighlightSelectedWord(p_checked);
}

void VMainWindow::changeHighlightSearchedWord(bool p_checked)
{
    vconfig.setHighlightSearchedWord(p_checked);
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
    QActionGroup *renderBackgroundAct = new QActionGroup(this);
    connect(renderBackgroundAct, &QActionGroup::triggered,
            this, &VMainWindow::setRenderBackgroundColor);

    QMenu *renderBgMenu = menu->addMenu(tr("&Rendering Background"));
    const QString &curBgColor = vconfig.getCurRenderBackgroundColor();
    QAction *tmpAct = new QAction(tr("System"), renderBackgroundAct);
    tmpAct->setStatusTip(tr("Use system's background color configuration for Markdown rendering"));
    tmpAct->setCheckable(true);
    tmpAct->setData("System");
    if (curBgColor == "System") {
        tmpAct->setChecked(true);
    }
    renderBgMenu->addAction(tmpAct);

    const QVector<VColor> &bgColors = vconfig.getPredefinedColors();
    for (int i = 0; i < bgColors.size(); ++i) {
        tmpAct = new QAction(bgColors[i].name, renderBackgroundAct);
        tmpAct->setStatusTip(tr("Set as the background color for Markdown rendering"));
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

    QActionGroup *backgroundColorAct = new QActionGroup(this);
    connect(backgroundColorAct, &QActionGroup::triggered,
            this, &VMainWindow::setEditorBackgroundColor);

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
        tmpAct->setStatusTip(tr("Set as the background color for editor"));
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

void VMainWindow::updateActionStateFromTabStatusChange(const VFile *p_file,
                                                       bool p_editMode)
{
    if (p_file) {
        if (p_editMode) {
            editNoteAct->setVisible(false);
            saveExitAct->setVisible(true);
            saveNoteAct->setVisible(true);
            deleteNoteAct->setEnabled(true);

            m_insertImageAct->setEnabled(true);
        } else {
            editNoteAct->setVisible(true);
            saveExitAct->setVisible(false);
            saveNoteAct->setVisible(false);
            deleteNoteAct->setEnabled(true);

            m_insertImageAct->setEnabled(false);
            m_replaceAct->setEnabled(false);
            m_replaceFindAct->setEnabled(false);
            m_replaceAllAct->setEnabled(false);
        }
        noteInfoAct->setEnabled(true);

        m_findReplaceAct->setEnabled(true);
    } else {
        editNoteAct->setVisible(false);
        saveExitAct->setVisible(false);
        saveNoteAct->setVisible(false);
        deleteNoteAct->setEnabled(false);
        noteInfoAct->setEnabled(false);

        m_insertImageAct->setEnabled(false);
        // Find/Replace
        m_findReplaceAct->setEnabled(false);
        m_findNextAct->setEnabled(false);
        m_findPreviousAct->setEnabled(false);
        m_replaceAct->setEnabled(false);
        m_replaceFindAct->setEnabled(false);
        m_replaceAllAct->setEnabled(false);
        m_findReplaceDialog->closeDialog();
    }
}

void VMainWindow::handleCurTabStatusChanged(const VFile *p_file, const VEditTab *p_editTab, bool p_editMode)
{
    updateActionStateFromTabStatusChange(p_file, p_editMode);
    if (p_file) {
        m_findReplaceDialog->updateState(p_file->getDocType(), p_editMode);
    }

    QString title;
    if (p_file) {
        title = QString("[%1] %2").arg(p_file->getNotebookName()).arg(p_file->retrivePath());
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
    m_onePanel = true;
}

void VMainWindow::twoPanelView()
{
    changeSplitterView(2);
    expandViewAct->setChecked(false);
    m_onePanel = false;
}

void VMainWindow::expandPanelView(bool p_checked)
{
    int nrSplits = 0;
    if (p_checked) {
        nrSplits = 0;
    } else {
        if (m_onePanel) {
            nrSplits = 1;
        } else {
            nrSplits = 2;
        }
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
        qWarning() << "invalid panel number" << nrPanel;
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
    vconfig.setMainSplitterState(mainSplitter->saveState());
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
    const QByteArray &splitterState = vconfig.getMainSplitterState();
    if (!splitterState.isEmpty()) {
        mainSplitter->restoreState(splitterState);
    }
}

const QVector<QPair<QString, QString> >& VMainWindow::getPalette() const
{
    return vnote->getPalette();
}

void VMainWindow::handleCurrentDirectoryChanged(const VDirectory *p_dir)
{
    newNoteAct->setEnabled(p_dir);
    m_importNoteAct->setEnabled(p_dir);
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

void VMainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape
        || (event->key() == Qt::Key_BracketLeft
            && event->modifiers() == Qt::ControlModifier)) {
        m_findReplaceDialog->closeDialog();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
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

void VMainWindow::locateFile(VFile *p_file)
{
    if (!p_file) {
        return;
    }
    qDebug() << "locate file" << p_file->retrivePath();
    VNotebook *notebook = p_file->getNotebook();
    if (notebookSelector->locateNotebook(notebook)) {
        while (directoryTree->currentNotebook() != notebook) {
            QCoreApplication::sendPostedEvents();
        }
        VDirectory *dir = p_file->getDirectory();
        if (directoryTree->locateDirectory(dir)) {
            while (fileList->currentDirectory() != dir) {
                QCoreApplication::sendPostedEvents();
            }
            fileList->locateFile(p_file);
        }
    }

    // Open the directory and file panels after location.
    changeSplitterView(2);
}

void VMainWindow::handleFindDialogTextChanged(const QString &p_text, uint /* p_options */)
{
    bool enabled = true;
    if (p_text.isEmpty()) {
        enabled = false;
    }
    m_findNextAct->setEnabled(enabled);
    m_findPreviousAct->setEnabled(enabled);
    m_replaceAct->setEnabled(enabled);
    m_replaceFindAct->setEnabled(enabled);
    m_replaceAllAct->setEnabled(enabled);
}

void VMainWindow::openFindDialog()
{
    m_findReplaceDialog->openDialog(editArea->getSelectedText());
}

void VMainWindow::viewSettings()
{
    VSettingsDialog settingsDialog(this);
    settingsDialog.exec();
}
