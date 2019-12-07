#include <QtWidgets>
#include <QList>
#include <QPrinter>
#include <QPrintDialog>
#include <QPainter>
#include <QWebEnginePage>

#include "vmainwindow.h"
#include "vdirectorytree.h"
#include "vnote.h"
#include "vfilelist.h"
#include "vconfigmanager.h"
#include "utils/vutils.h"
#include "veditarea.h"
#include "voutline.h"
#include "vnotebookselector.h"
#include "dialog/vfindreplacedialog.h"
#include "dialog/vsettingsdialog.h"
#include "vcaptain.h"
#include "vedittab.h"
#include "vwebview.h"
#include "vexporter.h"
#include "vmdtab.h"
#include "vvimindicator.h"
#include "vvimcmdlineedit.h"
#include "vtabindicator.h"
#include "dialog/vupdater.h"
#include "vorphanfile.h"
#include "dialog/vorphanfileinfodialog.h"
#include "vsingleinstanceguard.h"
#include "vnotefile.h"
#include "vbuttonwithwidget.h"
#include "vattachmentlist.h"
#include "vfilesessioninfo.h"
#include "vsnippetlist.h"
#include "vtoolbox.h"
#include "vbuttonmenuitem.h"
#include "vpalette.h"
#include "utils/viconutils.h"
#include "dialog/vtipsdialog.h"
#include "vcart.h"
#include "dialog/vexportdialog.h"
#include "vsearcher.h"
#include "vuniversalentry.h"
#include "vsearchue.h"
#include "voutlineue.h"
#include "vhelpue.h"
#include "vlistfolderue.h"
#include "dialog/vfixnotebookdialog.h"
#include "vhistorylist.h"
#include "vexplorer.h"
#include "vlistue.h"
#include "vtagexplorer.h"
#include "vmdeditor.h"

extern VConfigManager *g_config;

extern VPalette *g_palette;

VMainWindow *g_mainWin;

VNote *g_vnote;

VWebUtils *g_webUtils;

const int VMainWindow::c_sharedMemTimerInterval = 1000;

#if defined(QT_NO_DEBUG)
extern QFile g_logFile;
#endif

#define COLOR_PIXMAP_ICON_SIZE 64

enum NaviBoxIndex
{
    NotebookPanel = 0,
    HistoryList,
    Explorer,
    TagExplorer
};


VMainWindow::VMainWindow(VSingleInstanceGuard *p_guard, QWidget *p_parent)
    : QMainWindow(p_parent),
      m_guard(p_guard),
      m_windowOldState(Qt::WindowNoState),
      m_requestQuit(false),
      m_printer(NULL),
      m_ue(NULL),
      m_syncNoteListToCurrentTab(true)
{
    qsrand(QDateTime::currentDateTime().toTime_t());

    g_mainWin = this;

    setWindowIcon(QIcon(":/resources/icons/vnote.ico"));

    vnote = new VNote(this);
    g_vnote = vnote;

    m_webUtils.init();
    g_webUtils = &m_webUtils;

    initCaptain();

    setupUI();

    initMenuBar();

    initToolBar();

    initShortcuts();

    initDockWindows();

    int state = g_config->getPanelViewState();
    if (state < 0 || state >= (int)PanelViewState::Invalid) {
        state = (int)PanelViewState::VerticalMode;
    }

    changePanelView((PanelViewState)state);

    restoreStateAndGeometry();

    setContextMenuPolicy(Qt::NoContextMenu);

    m_notebookSelector->update();

    initSharedMemoryWatcher();

    initUpdateTimer();

    registerCaptainAndNavigationTargets();
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    QApplication::setQuitOnLastWindowClosed(false);
#endif
}

void VMainWindow::initSharedMemoryWatcher()
{
    m_sharedMemTimer = new QTimer(this);
    m_sharedMemTimer->setSingleShot(false);
    m_sharedMemTimer->setInterval(c_sharedMemTimerInterval);
    connect(m_sharedMemTimer, &QTimer::timeout,
            this, &VMainWindow::checkSharedMemory);

    m_sharedMemTimer->start();
}

void VMainWindow::initCaptain()
{
    // VCaptain should be visible to accpet key focus. But VCaptain
    // may hide other widgets.
    m_captain = new VCaptain(this);
    connect(m_captain, &VCaptain::captainModeChanged,
            this, [this](bool p_captainMode) {
                static QString normalStyle = m_avatarBtn->styleSheet();
                static QString captainStyle = QString("color: %1; background: %2;")
                                                     .arg(g_palette->color("avatar_captain_mode_fg"))
                                                     .arg(g_palette->color("avatar_captain_mode_bg"));

                if (p_captainMode) {
                    m_avatarBtn->setStyleSheet(captainStyle);
                } else {
                    m_avatarBtn->setStyleSheet(normalStyle);
                }
            });
}

void VMainWindow::registerCaptainAndNavigationTargets()
{
    m_captain->registerNavigationTarget(m_naviBox);
    m_captain->registerNavigationTarget(m_notebookSelector);
    m_captain->registerNavigationTarget(m_dirTree);
    m_captain->registerNavigationTarget(m_fileList);
    m_captain->registerNavigationTarget(m_historyList);

    m_tagExplorer->registerNavigationTarget();

    m_captain->registerNavigationTarget(m_editArea);

    m_tabIndicator->registerNavigationTarget();

    m_captain->registerNavigationTarget(m_toolBox);
    m_captain->registerNavigationTarget(outline);
    m_captain->registerNavigationTarget(m_snippetList);
    m_captain->registerNavigationTarget(m_cart);
    m_captain->registerNavigationTarget(m_searcher);

    // Register Captain mode targets.
    m_captain->registerCaptainTarget(tr("AttachmentList"),
                                     g_config->getCaptainShortcutKeySequence("AttachmentList"),
                                     this,
                                     showAttachmentListByCaptain);
    m_captain->registerCaptainTarget(tr("LocateCurrentFile"),
                                     g_config->getCaptainShortcutKeySequence("LocateCurrentFile"),
                                     this,
                                     locateCurrentFileByCaptain);
    m_captain->registerCaptainTarget(tr("ExpandMode"),
                                     g_config->getCaptainShortcutKeySequence("ExpandMode"),
                                     this,
                                     toggleExpandModeByCaptain);
    m_captain->registerCaptainTarget(tr("CurrentNoteInfo"),
                                     g_config->getCaptainShortcutKeySequence("CurrentNoteInfo"),
                                     this,
                                     currentNoteInfoByCaptain);
    m_captain->registerCaptainTarget(tr("DiscardAndRead"),
                                     g_config->getCaptainShortcutKeySequence("DiscardAndRead"),
                                     this,
                                     discardAndReadByCaptain);
    m_captain->registerCaptainTarget(tr("ToolBar"),
                                     g_config->getCaptainShortcutKeySequence("ToolBar"),
                                     this,
                                     toggleToolBarByCaptain);
    m_captain->registerCaptainTarget(tr("ToolsDock"),
                                     g_config->getCaptainShortcutKeySequence("ToolsDock"),
                                     this,
                                     toggleToolsDockByCaptain);
    m_captain->registerCaptainTarget(tr("SearchDock"),
                                     g_config->getCaptainShortcutKeySequence("SearchDock"),
                                     this,
                                     toggleSearchDockByCaptain);
    m_captain->registerCaptainTarget(tr("CloseNote"),
                                     g_config->getCaptainShortcutKeySequence("CloseNote"),
                                     this,
                                     closeFileByCaptain);
    m_captain->registerCaptainTarget(tr("ShortcutsHelp"),
                                     g_config->getCaptainShortcutKeySequence("ShortcutsHelp"),
                                     this,
                                     shortcutsHelpByCaptain);
    m_captain->registerCaptainTarget(tr("FlushLogFile"),
                                     g_config->getCaptainShortcutKeySequence("FlushLogFile"),
                                     this,
                                     flushLogFileByCaptain);
    m_captain->registerCaptainTarget(tr("Export"),
                                     g_config->getCaptainShortcutKeySequence("Export"),
                                     this,
                                     exportByCaptain);
    m_captain->registerCaptainTarget(tr("FocusEditArea"),
                                     g_config->getCaptainShortcutKeySequence("FocusEditArea"),
                                     this,
                                     focusEditAreaByCaptain);
}

void VMainWindow::setupUI()
{
    setupNaviBox();

    m_editArea = new VEditArea();
    m_editArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_findReplaceDialog = m_editArea->getFindReplaceDialog();
    m_fileList->setEditArea(m_editArea);
    m_dirTree->setEditArea(m_editArea);

    connect(m_editArea, &VEditArea::fileClosed,
            m_historyList, &VHistoryList::addFile);

    // Main Splitter
    m_mainSplitter = new QSplitter();
    m_mainSplitter->setObjectName("MainSplitter");
    m_mainSplitter->addWidget(m_naviBox);
    m_mainSplitter->addWidget(m_editArea);
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);

    connect(m_dirTree, &VDirectoryTree::directoryUpdated,
            m_editArea, &VEditArea::handleDirectoryUpdated);

    connect(m_notebookSelector, &VNotebookSelector::notebookUpdated,
            m_editArea, &VEditArea::handleNotebookUpdated);
    connect(m_notebookSelector, &VNotebookSelector::notebookCreated,
            m_dirTree, [this](const QString &p_name, bool p_import) {
                Q_UNUSED(p_name);
                if (!p_import) {
                    m_dirTree->newRootDirectory();
                }
            });

    connect(m_fileList, &VFileList::fileClicked,
            m_editArea, &VEditArea::openFile);
    connect(m_fileList, &VFileList::fileCreated,
            m_editArea, [this](VNoteFile *p_file,
                               OpenFileMode p_mode,
                               bool p_forceMode) {
                if (p_file->getDocType() == DocType::Markdown
                    || p_file->getDocType() == DocType::Html) {
                    m_editArea->openFile(p_file, p_mode, p_forceMode);
                }
            });
    connect(m_fileList, &VFileList::fileUpdated,
            m_editArea, &VEditArea::handleFileUpdated);
    connect(m_editArea, &VEditArea::tabStatusUpdated,
            this, &VMainWindow::handleAreaTabStatusUpdated);
    connect(m_editArea, &VEditArea::statusMessage,
            this, &VMainWindow::showStatusMessage);
    connect(m_editArea, &VEditArea::vimStatusUpdated,
            this, &VMainWindow::handleVimStatusUpdated);
    connect(m_findReplaceDialog, &VFindReplaceDialog::findTextChanged,
            this, &VMainWindow::handleFindDialogTextChanged);

    setCentralWidget(m_mainSplitter);

    initVimCmd();

    m_vimIndicator = new VVimIndicator(this);
    m_vimIndicator->hide();

    m_tabIndicator = new VTabIndicator(this);
    m_tabIndicator->hide();

    // Create and show the status bar
    statusBar()->addPermanentWidget(m_vimCmd);
    statusBar()->addPermanentWidget(m_vimIndicator);
    statusBar()->addPermanentWidget(m_tabIndicator);

    initTrayIcon();
}

void VMainWindow::setupNaviBox()
{
    m_naviBox = new VToolBox();
    m_naviBox->setObjectName("MainToolBox");

    setupNotebookPanel();
    m_naviBox->addItem(m_nbSplitter,
                       ":/resources/icons/notebook.svg",
                       tr("Notebooks"),
                       m_dirTree);

    m_historyList = new VHistoryList();
    m_naviBox->addItem(m_historyList,
                       ":/resources/icons/history.svg",
                       tr("History"));

    m_explorer = new VExplorer();
    m_naviBox->addItem(m_explorer,
                       ":/resources/icons/explorer.svg",
                       tr("Explorer"));

    m_tagExplorer = new VTagExplorer();
    m_naviBox->addItem(m_tagExplorer,
                       ":/resources/icons/tag_explorer.svg",
                       tr("Tags"));
    connect(m_notebookSelector, &VNotebookSelector::curNotebookChanged,
            m_tagExplorer, &VTagExplorer::setNotebook);

    connect(m_fileList, &VFileList::requestSplitOut,
            this, &VMainWindow::splitFileListOut);
}

void VMainWindow::setupNotebookPanel()
{
    m_notebookSelector = new VNotebookSelector();
    m_notebookSelector->setObjectName("NotebookSelector");
    m_notebookSelector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);

    // Folders.
    QLabel *directoryLabel = new QLabel(tr("Folders"));
    directoryLabel->setProperty("TitleLabel", true);

    m_dirTree = new VDirectoryTree;

    QVBoxLayout *naviLayout = new QVBoxLayout;
    naviLayout->addWidget(m_notebookSelector);
    naviLayout->addWidget(directoryLabel);
    naviLayout->addWidget(m_dirTree);
    naviLayout->setContentsMargins(0, 0, 0, 0);
    naviLayout->setSpacing(0);
    QWidget *naviWidget = new QWidget();
    naviWidget->setLayout(naviLayout);

    // Notes.
    m_fileList = new VFileList();
    m_fileList->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);

    m_nbSplitter = new QSplitter();
    m_nbSplitter->setObjectName("NotebookSplitter");
    m_nbSplitter->addWidget(naviWidget);
    m_nbSplitter->addWidget(m_fileList);

    setupFileListSplitOut(g_config->getEnableSplitFileList());

    connect(m_notebookSelector, &VNotebookSelector::curNotebookChanged,
            this, [this](VNotebook *p_notebook) {
                m_dirTree->setNotebook(p_notebook);
                m_dirTree->setFocus();
            });

    connect(m_notebookSelector, &VNotebookSelector::curNotebookChanged,
            this, &VMainWindow::handleCurrentNotebookChanged);

    connect(m_dirTree, &VDirectoryTree::currentDirectoryChanged,
            this, &VMainWindow::handleCurrentDirectoryChanged);

    connect(m_dirTree, &VDirectoryTree::currentDirectoryChanged,
            m_fileList, &VFileList::setDirectory);
}

void VMainWindow::initToolBar()
{
    const int tbIconSize = g_config->getToolBarIconSize() * VUtils::calculateScaleFactor();
    QSize iconSize(tbIconSize, tbIconSize);

    m_toolBars.append(initFileToolBar(iconSize));
    m_toolBars.append(initViewToolBar(iconSize));
    m_toolBars.append(initEditToolBar(iconSize));
    m_toolBars.append(initNoteToolBar(iconSize));

    setToolBarVisible(g_config->getToolBarChecked());
}

QToolBar *VMainWindow::initViewToolBar(QSize p_iconSize)
{
    m_viewToolBar = addToolBar(tr("View"));
    m_viewToolBar->setObjectName("ViewToolBar");
    m_viewToolBar->setMovable(false);
    if (p_iconSize.isValid()) {
        m_viewToolBar->setIconSize(p_iconSize);
    }

    QAction *fullScreenAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/fullscreen.svg"),
                                         tr("Full Screen"),
                                         this);
    QString keySeq = g_config->getShortcutKeySequence("FullScreen");
    QKeySequence seq(keySeq);
    if (!seq.isEmpty()) {
        fullScreenAct->setText(tr("Full Screen\t%1").arg(VUtils::getShortcutText(keySeq)));
        fullScreenAct->setShortcut(seq);
    }

    fullScreenAct->setStatusTip(tr("Toggle full screen"));
    connect(fullScreenAct, &QAction::triggered,
            this, [this]() {
                if (windowState() & Qt::WindowFullScreen) {
                    if (m_windowOldState & Qt::WindowMaximized) {
                        showMaximized();
                    } else {
                        showNormal();
                    }
                } else {
                    showFullScreen();
                }
            });

    QAction *stayOnTopAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/stay_on_top.svg"),
                                        tr("Stay On Top"),
                                        this);
    stayOnTopAct->setStatusTip(tr("Toggle stay-on-top"));
    stayOnTopAct->setCheckable(true);
    connect(stayOnTopAct, &QAction::triggered,
            this, &VMainWindow::stayOnTop);

    QAction *menuBarAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/menubar.svg"),
                                      tr("Menu Bar"),
                                      this);
    menuBarAct->setStatusTip(tr("Toggle menu bar"));
    menuBarAct->setCheckable(true);
    menuBarAct->setChecked(g_config->getMenuBarChecked());
    connect(menuBarAct, &QAction::triggered,
            this, [this](bool p_checked) {
                setMenuBarVisible(p_checked);
                g_config->setMenuBarChecked(p_checked);
            });

    QMenu *viewMenu = new QMenu(this);
    viewMenu->setToolTipsVisible(true);
    viewMenu->addAction(fullScreenAct);
    viewMenu->addAction(stayOnTopAct);
    viewMenu->addAction(menuBarAct);

    expandViewAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/expand.svg"),
                                tr("Expand Edit Area"),
                                this);
    VUtils::fixTextWithCaptainShortcut(expandViewAct, "ExpandMode");
    expandViewAct->setStatusTip(tr("Expand the edit area"));
    expandViewAct->setCheckable(true);
    expandViewAct->setMenu(viewMenu);
    connect(expandViewAct, &QAction::triggered,
            this, [this](bool p_checked) {
                changePanelView(p_checked ? PanelViewState::ExpandMode
                                          : PanelViewState::VerticalMode);
            });

    m_viewToolBar->addAction(expandViewAct);

    return m_viewToolBar;
}

// Enable/disable all actions of @p_widget.
static void setActionsEnabled(QWidget *p_widget, bool p_enabled)
{
    Q_ASSERT(p_widget);
    QList<QAction *> actions = p_widget->actions();
    for (auto const & act : actions) {
        act->setEnabled(p_enabled);
    }
}

QToolBar *VMainWindow::initEditToolBar(QSize p_iconSize)
{
    m_editToolBar = addToolBar(tr("Edit Toolbar"));
    m_editToolBar->setObjectName("EditToolBar");
    m_editToolBar->setMovable(false);
    if (p_iconSize.isValid()) {
        m_editToolBar->setIconSize(p_iconSize);
    }

    m_editToolBar->addSeparator();

    m_headingSequenceAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/heading_sequence.svg"),
                                       tr("Heading Sequence"),
                                       this);
    m_headingSequenceAct->setStatusTip(tr("Enable heading sequence in current note in edit mode"));
    m_headingSequenceAct->setCheckable(true);
    connect(m_headingSequenceAct, &QAction::triggered,
            this, [this](bool p_checked){
                if (isHeadingSequenceApplicable()) {
                    VMdTab *tab = dynamic_cast<VMdTab *>(m_curTab.data());
                    Q_ASSERT(tab);
                    tab->enableHeadingSequence(p_checked);
                }
            });

    m_editToolBar->addAction(m_headingSequenceAct);

    initHeadingButton(m_editToolBar);

    QAction *boldAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/bold.svg"),
                                   tr("Bold\t%1").arg(VUtils::getShortcutText("Ctrl+B")),
                                   this);
    boldAct->setStatusTip(tr("Insert bold text or change selected text to bold"));
    connect(boldAct, &QAction::triggered,
            this, [this](){
                if (m_curTab) {
                    m_curTab->decorateText(TextDecoration::Bold);
                }
            });

    m_editToolBar->addAction(boldAct);

    QAction *italicAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/italic.svg"),
                                     tr("Italic\t%1").arg(VUtils::getShortcutText("Ctrl+I")),
                                     this);
    italicAct->setStatusTip(tr("Insert italic text or change selected text to italic"));
    connect(italicAct, &QAction::triggered,
            this, [this](){
                if (m_curTab) {
                    m_curTab->decorateText(TextDecoration::Italic);
                }
            });

    m_editToolBar->addAction(italicAct);

    QAction *strikethroughAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/strikethrough.svg"),
                                            tr("Strikethrough\t%1").arg(VUtils::getShortcutText("Ctrl+D")),
                                            this);
    strikethroughAct->setStatusTip(tr("Insert strikethrough text or change selected text to strikethroughed"));
    connect(strikethroughAct, &QAction::triggered,
            this, [this](){
                if (m_curTab) {
                    m_curTab->decorateText(TextDecoration::Strikethrough);
                }
            });

    m_editToolBar->addAction(strikethroughAct);

    QAction *inlineCodeAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/inline_code.svg"),
                                         tr("Inline Code\t%1").arg(VUtils::getShortcutText("Ctrl+;")),
                                         this);
    inlineCodeAct->setStatusTip(tr("Insert inline-code text or change selected text to inline-coded"));
    connect(inlineCodeAct, &QAction::triggered,
            this, [this](){
                if (m_curTab) {
                    m_curTab->decorateText(TextDecoration::InlineCode);
                }
            });

    m_editToolBar->addAction(inlineCodeAct);

    QAction *codeBlockAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/code_block.svg"),
                                        tr("Code Block\t%1").arg(VUtils::getShortcutText("Ctrl+M")),
                                        this);
    codeBlockAct->setStatusTip(tr("Insert fenced code block text or wrap selected text into a fenced code block"));
    connect(codeBlockAct, &QAction::triggered,
            this, [this](){
                if (m_curTab) {
                    m_curTab->decorateText(TextDecoration::CodeBlock);
                }
            });

    m_editToolBar->addAction(codeBlockAct);

    QAction *tableAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/table.svg"),
                                    tr("Table\t%1").arg(VUtils::getShortcutText("Ctrl+.")),
                                    this);
    tableAct->setStatusTip(tr("Insert a table"));
    connect(tableAct, &QAction::triggered,
            this, [this](){
                if (m_curTab) {
                    m_curTab->insertTable();
                }
            });

    m_editToolBar->addAction(tableAct);

    // Insert link.
    QAction *insetLinkAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/link.svg"),
                                        tr("Link\t%1").arg(VUtils::getShortcutText("Ctrl+L")),
                                        this);
    insetLinkAct->setStatusTip(tr("Insert a link"));
    connect(insetLinkAct, &QAction::triggered,
            this, [this]() {
                if (m_curTab) {
                    m_curTab->insertLink();
                }
            });

    m_editToolBar->addAction(insetLinkAct);

    // Insert image.
    QAction *insertImageAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/insert_image.svg"),
                                          tr("Image\t%1").arg(VUtils::getShortcutText("Ctrl+'")),
                                          this);
    insertImageAct->setStatusTip(tr("Insert an image from file or URL"));
    connect(insertImageAct, &QAction::triggered,
            this, [this]() {
                if (m_curTab) {
                    m_curTab->insertImage();
                }
            });

    m_editToolBar->addAction(insertImageAct);

    setActionsEnabled(m_editToolBar, false);

    return m_editToolBar;
}

QToolBar *VMainWindow::initNoteToolBar(QSize p_iconSize)
{
    m_noteToolBar = addToolBar(tr("Note Toolbar"));
    m_noteToolBar->setObjectName("NoteToolBar");
    m_noteToolBar->setMovable(false);
    if (p_iconSize.isValid()) {
        m_noteToolBar->setIconSize(p_iconSize);
    }

    m_noteToolBar->addSeparator();

    // Attachment.
    m_attachmentList = new VAttachmentList(this);
    m_attachmentBtn = new VButtonWithWidget(VIconUtils::toolButtonIcon(":/resources/icons/attachment.svg"),
                                            "",
                                            m_attachmentList,
                                            this);
    m_attachmentBtn->setBubbleColor(g_palette->color("bubble_fg"),
                                    g_palette->color("bubble_bg"));
    m_attachmentBtn->setToolTip(tr("Attachments (drag files here to add attachments)"));
    m_attachmentBtn->setProperty("CornerBtn", true);
    m_attachmentBtn->setFocusPolicy(Qt::NoFocus);
    m_attachmentBtn->setEnabled(false);

    QAction *flashPageAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/flash_page.svg"),
                                        tr("Flash Page"),
                                        this);
    flashPageAct->setStatusTip(tr("Open the Flash Page to edit"));
    QString keySeq = g_config->getShortcutKeySequence("FlashPage");
    QKeySequence seq(keySeq);
    if (!seq.isEmpty()) {
        flashPageAct->setText(tr("Flash Page\t%1").arg(VUtils::getShortcutText(keySeq)));
        flashPageAct->setShortcut(seq);
    }

    connect(flashPageAct, &QAction::triggered,
            this, &VMainWindow::openFlashPage);

    QAction *quickAccessAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/quick_access.svg"),
                                          tr("Quick Access"),
                                          this);
    quickAccessAct->setStatusTip(tr("Open quick access note"));
    keySeq = g_config->getShortcutKeySequence("QuickAccess");
    seq = QKeySequence(keySeq);
    if (!seq.isEmpty()) {
        quickAccessAct->setText(tr("Quick Access\t%1").arg(VUtils::getShortcutText(keySeq)));
        quickAccessAct->setShortcut(seq);
    }

    connect(quickAccessAct, &QAction::triggered,
            this, &VMainWindow::openQuickAccess);

    QAction *universalEntryAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/universal_entry_tb.svg"),
                                             tr("Universal Entry"),
                                             this);
    universalEntryAct->setStatusTip(tr("Activate Universal Entry"));
    keySeq = g_config->getShortcutKeySequence("UniversalEntry");
    seq = QKeySequence(keySeq);
    if (!seq.isEmpty()) {
        universalEntryAct->setText(tr("Universal Entry\t%1").arg(VUtils::getShortcutText(keySeq)));
        universalEntryAct->setShortcut(seq);
    }

    connect(universalEntryAct, &QAction::triggered,
            this, &VMainWindow::activateUniversalEntry);

    m_noteToolBar->addWidget(m_attachmentBtn);
    m_noteToolBar->addAction(flashPageAct);
    m_noteToolBar->addAction(quickAccessAct);
    m_noteToolBar->addAction(universalEntryAct);

    return m_noteToolBar;
}

QToolBar *VMainWindow::initFileToolBar(QSize p_iconSize)
{
    m_fileToolBar = addToolBar(tr("Note"));
    m_fileToolBar->setObjectName("FileToolBar");
    m_fileToolBar->setMovable(false);
    if (p_iconSize.isValid()) {
        m_fileToolBar->setIconSize(p_iconSize);
    }

    m_avatarBtn = new QPushButton("VNote", this);
    m_avatarBtn->setProperty("AvatarBtn", true);
    m_avatarBtn->setFocusPolicy(Qt::NoFocus);
    m_avatarBtn->setToolTip(tr("Log In (Not Implemented Yet)"));

    newRootDirAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/create_rootdir_tb.svg"),
                                tr("New Root Folder"),
                                this);
    newRootDirAct->setStatusTip(tr("Create a root folder in current notebook"));
    connect(newRootDirAct, &QAction::triggered,
            m_dirTree, &VDirectoryTree::newRootDirectory);

    newNoteAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/create_note_tb.svg"),
                             tr("New Note"), this);
    newNoteAct->setStatusTip(tr("Create a note in current folder"));
    QString keySeq = g_config->getShortcutKeySequence("NewNote");
    QKeySequence seq(keySeq);
    if (!seq.isEmpty()) {
        newNoteAct->setText(tr("New Note\t%1").arg(VUtils::getShortcutText(keySeq)));
        newNoteAct->setShortcut(seq);
    }

    connect(newNoteAct, &QAction::triggered,
            m_fileList, &VFileList::newFile);

    noteInfoAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/note_info_tb.svg"),
                              tr("Note Info"),
                              this);
    VUtils::fixTextWithCaptainShortcut(noteInfoAct, "CurrentNoteInfo");
    noteInfoAct->setStatusTip(tr("View and edit current note's information"));
    connect(noteInfoAct, &QAction::triggered,
            this, &VMainWindow::curEditFileInfo);

    deleteNoteAct = new QAction(VIconUtils::toolButtonDangerIcon(":/resources/icons/delete_note_tb.svg"),
                                tr("Delete Note"), this);
    deleteNoteAct->setStatusTip(tr("Delete current note"));
    connect(deleteNoteAct, &QAction::triggered,
            this, &VMainWindow::deleteCurNote);

    m_editReadAct = new QAction(this);
    connect(m_editReadAct, &QAction::triggered,
            this, &VMainWindow::toggleEditReadMode);

    m_discardExitAct = new QAction(VIconUtils::menuIcon(":/resources/icons/discard_exit.svg"),
                                   tr("Discard Changes And Read"),
                                   this);
    VUtils::fixTextWithCaptainShortcut(m_discardExitAct, "DiscardAndRead");
    m_discardExitAct->setStatusTip(tr("Discard changes and exit edit mode"));
    connect(m_discardExitAct, &QAction::triggered,
            this, [this]() {
                m_editArea->readFile(true);
            });

    updateEditReadAct(NULL);

    saveNoteAct = new QAction(VIconUtils::toolButtonIcon(":/resources/icons/save_note.svg"),
                              tr("Save"), this);
    saveNoteAct->setStatusTip(tr("Save changes to current note"));
    keySeq = g_config->getShortcutKeySequence("SaveNote");
    seq = QKeySequence(keySeq);
    if (!seq.isEmpty()) {
        saveNoteAct->setText(tr("Save\t%1").arg(VUtils::getShortcutText(keySeq)));
        saveNoteAct->setShortcut(seq);
    }

    connect(saveNoteAct, &QAction::triggered,
            m_editArea, &VEditArea::saveFile);

    newRootDirAct->setEnabled(false);
    newNoteAct->setEnabled(false);
    noteInfoAct->setEnabled(false);
    deleteNoteAct->setEnabled(false);
    m_editReadAct->setEnabled(false);
    m_discardExitAct->setEnabled(false);
    saveNoteAct->setEnabled(false);

    m_fileToolBar->addWidget(m_avatarBtn);
    m_fileToolBar->addAction(newRootDirAct);
    m_fileToolBar->addAction(newNoteAct);
    m_fileToolBar->addAction(deleteNoteAct);
    m_fileToolBar->addAction(noteInfoAct);
    m_fileToolBar->addAction(m_editReadAct);
    m_fileToolBar->addAction(m_discardExitAct);
    m_fileToolBar->addAction(saveNoteAct);

    return m_fileToolBar;
}

void VMainWindow::initMenuBar()
{
    initFileMenu();
    initEditMenu();
    initViewMenu();
    initMarkdownMenu();
    initHelpMenu();

    setMenuBarVisible(g_config->getMenuBarChecked());
}

void VMainWindow::initHelpMenu()
{
    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->setToolTipsVisible(true);

#if defined(QT_NO_DEBUG)
    QAction *logAct = new QAction(tr("View &Log"), this);
    logAct->setToolTip(tr("View VNote's debug log (%1)").arg(g_config->getLogFilePath()));
    connect(logAct, &QAction::triggered,
            this, [](){
                QUrl url = QUrl::fromLocalFile(g_config->getLogFilePath());
                QDesktopServices::openUrl(url);
            });
#endif

    QAction *shortcutAct = new QAction(tr("&Shortcuts Help"), this);
    shortcutAct->setToolTip(tr("View information about shortcut keys"));
    VUtils::fixTextWithCaptainShortcut(shortcutAct, "ShortcutsHelp");
    connect(shortcutAct, &QAction::triggered,
            this, &VMainWindow::shortcutsHelp);

    QAction *mdGuideAct = new QAction(tr("&Markdown Guide"), this);
    mdGuideAct->setToolTip(tr("A quick guide of Markdown syntax"));
    connect(mdGuideAct, &QAction::triggered,
            this, [this](){
                QString docFile = VUtils::getDocFile(VNote::c_markdownGuideDocFile);
                VFile *file = vnote->getFile(docFile, true);
                m_editArea->openFile(file, OpenFileMode::Read);
            });

    QAction *docAct = new QAction(tr("&Documentation"), this);
    docAct->setToolTip(tr("View VNote's documentation"));
    connect(docAct, &QAction::triggered,
            this, []() {
                QString url("https://tamlok.github.io/vnote");
                QDesktopServices::openUrl(url);
            });

    QAction *donateAct = new QAction(tr("Do&nate"), this);
    donateAct->setToolTip(tr("Donate to VNote or view the donate list"));
    connect(donateAct, &QAction::triggered,
            this, []() {
                QString url("https://tamlok.github.io/vnote/en_us/#!donate.md");
                QDesktopServices::openUrl(url);
            });

    QAction *updateAct = new QAction(tr("Check For &Updates"), this);
    updateAct->setToolTip(tr("Check for updates of VNote"));
    connect(updateAct, &QAction::triggered,
            this, [this](){
                VUpdater updater(this);
                updater.exec();
            });

    QAction *starAct = new QAction(tr("Star VNote on &GitHub"), this);
    starAct->setToolTip(tr("Give a star to VNote on GitHub project"));
    connect(starAct, &QAction::triggered,
            this, []() {
                QString url("https://github.com/tamlok/vnote");
                QDesktopServices::openUrl(url);
            });

    QAction *feedbackAct = new QAction(tr("&Feedback"), this);
    feedbackAct->setToolTip(tr("Open an issue on GitHub"));
    connect(feedbackAct, &QAction::triggered,
            this, []() {
                QString url("https://github.com/tamlok/vnote/issues");
                QDesktopServices::openUrl(url);
            });

    QAction *aboutAct = new QAction(tr("&About VNote"), this);
    aboutAct->setToolTip(tr("View information about VNote"));
    aboutAct->setMenuRole(QAction::AboutRole);
    connect(aboutAct, &QAction::triggered,
            this, &VMainWindow::aboutMessage);

    QAction *aboutQtAct = new QAction(tr("About &Qt"), this);
    aboutQtAct->setToolTip(tr("View information about Qt"));
    aboutQtAct->setMenuRole(QAction::AboutQtRole);
    connect(aboutQtAct, &QAction::triggered,
            qApp, &QApplication::aboutQt);

    helpMenu->addAction(shortcutAct);
    helpMenu->addAction(mdGuideAct);
    helpMenu->addAction(docAct);
    helpMenu->addAction(donateAct);
    helpMenu->addAction(updateAct);
    helpMenu->addAction(starAct);
    helpMenu->addAction(feedbackAct);

#if defined(QT_NO_DEBUG)
    helpMenu->addAction(logAct);
#endif

    helpMenu->addAction(aboutQtAct);
    helpMenu->addAction(aboutAct);
}

void VMainWindow::initMarkdownMenu()
{
    QMenu *markdownMenu = menuBar()->addMenu(tr("&Markdown"));
    markdownMenu->setToolTipsVisible(true);

    initConverterMenu(markdownMenu);

    initMarkdownitOptionMenu(markdownMenu);

    markdownMenu->addSeparator();

    initRenderStyleMenu(markdownMenu);

    initRenderBackgroundMenu(markdownMenu);

    initCodeBlockStyleMenu(markdownMenu);

    QAction *constrainImageAct = new QAction(tr("Constrain The Width Of Images"), this);
    constrainImageAct->setToolTip(tr("Constrain the width of images to the window in read mode (re-open current tabs to make it work)"));
    constrainImageAct->setCheckable(true);
    connect(constrainImageAct, &QAction::triggered,
            this, &VMainWindow::enableImageConstraint);
    markdownMenu->addAction(constrainImageAct);
    constrainImageAct->setChecked(g_config->getEnableImageConstraint());

    QAction *imageCaptionAct = new QAction(tr("Enable Image Caption"), this);
    imageCaptionAct->setToolTip(tr("Center the images and display the alt text as caption (re-open current tabs to make it work)"));
    imageCaptionAct->setCheckable(true);
    connect(imageCaptionAct, &QAction::triggered,
            this, &VMainWindow::enableImageCaption);
    markdownMenu->addAction(imageCaptionAct);
    imageCaptionAct->setChecked(g_config->getEnableImageCaption());

    markdownMenu->addSeparator();

    initMarkdownExtensionMenu(markdownMenu);

    markdownMenu->addSeparator();

    QAction *codeBlockAct = new QAction(tr("Highlight Code Blocks In Edit Mode"), this);
    codeBlockAct->setToolTip(tr("Enable syntax highlight within code blocks in edit mode"));
    codeBlockAct->setCheckable(true);
    connect(codeBlockAct, &QAction::triggered,
            this, &VMainWindow::enableCodeBlockHighlight);
    markdownMenu->addAction(codeBlockAct);
    codeBlockAct->setChecked(g_config->getEnableCodeBlockHighlight());

    QAction *lineNumberAct = new QAction(tr("Display Line Number In Code Blocks"), this);
    lineNumberAct->setToolTip(tr("Enable line number in code blocks in read mode"));
    lineNumberAct->setCheckable(true);
    connect(lineNumberAct, &QAction::triggered,
            this, [](bool p_checked){
                g_config->setEnableCodeBlockLineNumber(p_checked);
            });
    markdownMenu->addAction(lineNumberAct);
    lineNumberAct->setChecked(g_config->getEnableCodeBlockLineNumber());

    QAction *previewImageAct = new QAction(tr("In-Place Preview"), this);
    previewImageAct->setToolTip(tr("Enable in-place preview (images, diagrams, and formulas) in edit mode (re-open current tabs to make it work)"));
    previewImageAct->setCheckable(true);
    connect(previewImageAct, &QAction::triggered,
            this, &VMainWindow::enableImagePreview);
    markdownMenu->addAction(previewImageAct);
    previewImageAct->setChecked(g_config->getEnablePreviewImages());

    QAction *previewWidthAct = new QAction(tr("Constrain The Width Of In-Place Preview"), this);
    previewWidthAct->setToolTip(tr("Constrain the width of in-place preview to the edit window in edit mode"));
    previewWidthAct->setCheckable(true);
    connect(previewWidthAct, &QAction::triggered,
            this, &VMainWindow::enableImagePreviewConstraint);
    markdownMenu->addAction(previewWidthAct);
    previewWidthAct->setChecked(g_config->getEnablePreviewImageConstraint());
}

void VMainWindow::initViewMenu()
{
    m_viewMenu = menuBar()->addMenu(tr("&View"));
    m_viewMenu->setToolTipsVisible(true);

    m_toolBarAct = new QAction(tr("Tool Bar"), this);
    m_toolBarAct->setToolTip(tr("Toogle the tool bar"));
    VUtils::fixTextWithCaptainShortcut(m_toolBarAct, "ToolBar");
    m_toolBarAct->setCheckable(true);
    m_toolBarAct->setChecked(g_config->getToolBarChecked());
    connect(m_toolBarAct, &QAction::triggered,
            this, [this] (bool p_checked) {
                g_config->setToolBarChecked(p_checked);
                setToolBarVisible(p_checked);
            });

    m_viewMenu->addAction(m_toolBarAct);
}

void VMainWindow::initFileMenu()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->setToolTipsVisible(true);

    // Open external files.
    QAction *openAct = new QAction(tr("&Open"), this);
    openAct->setToolTip(tr("Open external file to edit"));
    connect(openAct, &QAction::triggered,
            this, [this](){
                static QString lastPath = QDir::homePath();
                QStringList files = QFileDialog::getOpenFileNames(this,
                                                                  tr("Select External Files To Open"),
                                                                  lastPath);

                if (files.isEmpty()) {
                    return;
                }

                // Update lastPath
                lastPath = QFileInfo(files[0]).path();

                openFiles(VUtils::filterFilePathsToOpen(files),
                          false,
                          g_config->getNoteOpenMode(),
                          false,
                          false);
            });

    fileMenu->addAction(openAct);

    // Import notes from files.
    m_importNoteAct = newAction(VIconUtils::menuIcon(":/resources/icons/import_note.svg"),
                                tr("&New Notes From Files"), this);
    m_importNoteAct->setToolTip(tr("Create notes from external files in current folder "
                                   "(will copy files if they do not locate in current folder)"));
    connect(m_importNoteAct, &QAction::triggered,
            this, &VMainWindow::importNoteFromFile);
    m_importNoteAct->setEnabled(false);

    fileMenu->addAction(m_importNoteAct);

    fileMenu->addSeparator();

    // Export as PDF.
    m_exportAct = new QAction(tr("E&xport"), this);
    m_exportAct->setToolTip(tr("Export notes"));
    VUtils::fixTextWithCaptainShortcut(m_exportAct, "Export");
    connect(m_exportAct, &QAction::triggered,
            this, &VMainWindow::handleExportAct);

    fileMenu->addAction(m_exportAct);

    // Print.
    m_printAct = new QAction(VIconUtils::menuIcon(":/resources/icons/print.svg"),
                             tr("&Print"), this);
    m_printAct->setToolTip(tr("Print current note"));
    connect(m_printAct, &QAction::triggered,
            this, &VMainWindow::printNote);
    m_printAct->setEnabled(false);

    fileMenu->addAction(m_printAct);

    fileMenu->addSeparator();

    // Themes.
    initThemeMenu(fileMenu);

    // Settings.
    QAction *settingsAct = newAction(VIconUtils::menuIcon(":/resources/icons/settings.svg"),
                                     tr("&Settings"), this);
    settingsAct->setToolTip(tr("View and change settings for VNote"));
    settingsAct->setMenuRole(QAction::PreferencesRole);
    connect(settingsAct, &QAction::triggered,
            this, &VMainWindow::viewSettings);

    fileMenu->addAction(settingsAct);

    QAction *openConfigAct = new QAction(tr("Open Configuration Folder"), this);
    openConfigAct->setToolTip(tr("Open configuration folder of VNote"));
    connect(openConfigAct, &QAction::triggered,
            this, [](){
                QUrl url = QUrl::fromLocalFile(g_config->getConfigFolder());
                QDesktopServices::openUrl(url);
            });

    fileMenu->addAction(openConfigAct);

    QAction *customShortcutAct = new QAction(tr("Customize Shortcuts"), this);
    customShortcutAct->setToolTip(tr("Customize some standard shortcuts"));
    connect(customShortcutAct, &QAction::triggered,
            this, &VMainWindow::customShortcut);

    fileMenu->addAction(customShortcutAct);

    fileMenu->addSeparator();

    // Restart.
    QAction *restartAct = new QAction(tr("Restart"), this);
    connect(restartAct, &QAction::triggered,
            this, &VMainWindow::restartVNote);
    fileMenu->addAction(restartAct);

    // Exit.
    QAction *exitAct = new QAction(tr("&Quit"), this);
    exitAct->setToolTip(tr("Quit VNote"));
    exitAct->setShortcut(QKeySequence("Ctrl+Q"));
    exitAct->setMenuRole(QAction::QuitRole);
    connect(exitAct, &QAction::triggered,
            this, &VMainWindow::quitApp);

    fileMenu->addAction(exitAct);
}

void VMainWindow::quitApp()
{
    m_requestQuit = true;
    close();
}

void VMainWindow::initEditMenu()
{
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->setToolTipsVisible(true);

    // Find/Replace.
    m_findReplaceAct = newAction(VIconUtils::menuIcon(":/resources/icons/find_replace.svg"),
                                 tr("Find/Replace"), this);
    m_findReplaceAct->setToolTip(tr("Open Find/Replace dialog to search in current note"));
    QString keySeq = g_config->getShortcutKeySequence("Find");
    qDebug() << "set Find shortcut to" << keySeq;
    m_findReplaceAct->setShortcut(QKeySequence(keySeq));
    connect(m_findReplaceAct, &QAction::triggered,
            this, &VMainWindow::openFindDialog);

    QAction *advFindAct = new QAction(tr("Advanced Find"), this);
    advFindAct->setToolTip(tr("Advanced find within VNote"));
    keySeq = g_config->getShortcutKeySequence("AdvancedFind");
    qDebug() << "set AdvancedFind shortcut to" << keySeq;
    advFindAct->setShortcut(QKeySequence(keySeq));
    connect(advFindAct, &QAction::triggered,
            this, [this]() {
                m_searchDock->setVisible(true);
                m_searcher->focusToSearch();
            });

    m_findNextAct = new QAction(tr("Find Next"), this);
    m_findNextAct->setToolTip(tr("Find next occurence"));
    keySeq = g_config->getShortcutKeySequence("FindNext");
    qDebug() << "set FindNext shortcut to" << keySeq;
    m_findNextAct->setShortcut(QKeySequence(keySeq));
    connect(m_findNextAct, SIGNAL(triggered(bool)),
            m_findReplaceDialog, SLOT(findNext()));

    m_findPreviousAct = new QAction(tr("Find Previous"), this);
    m_findPreviousAct->setToolTip(tr("Find previous occurence"));
    keySeq = g_config->getShortcutKeySequence("FindPrevious");
    qDebug() << "set FindPrevious shortcut to" << keySeq;
    m_findPreviousAct->setShortcut(QKeySequence(keySeq));
    connect(m_findPreviousAct, SIGNAL(triggered(bool)),
            m_findReplaceDialog, SLOT(findPrevious()));

    m_replaceAct = new QAction(tr("Replace"), this);
    m_replaceAct->setToolTip(tr("Replace current occurence"));
    connect(m_replaceAct, SIGNAL(triggered(bool)),
            m_findReplaceDialog, SLOT(replace()));

    m_replaceFindAct = new QAction(tr("Replace && Find"), this);
    m_replaceFindAct->setToolTip(tr("Replace current occurence and find the next one"));
    connect(m_replaceFindAct, SIGNAL(triggered(bool)),
            m_findReplaceDialog, SLOT(replaceFind()));

    m_replaceAllAct = new QAction(tr("Replace All"), this);
    m_replaceAllAct->setToolTip(tr("Replace all occurences in current note"));
    connect(m_replaceAllAct, SIGNAL(triggered(bool)),
            m_findReplaceDialog, SLOT(replaceAll()));

    QAction *searchedWordAct = new QAction(tr("Highlight Searched Pattern"), this);
    searchedWordAct->setToolTip(tr("Highlight all occurences of searched pattern"));
    searchedWordAct->setCheckable(true);
    connect(searchedWordAct, &QAction::triggered,
            this, &VMainWindow::changeHighlightSearchedWord);

    // Expand Tab into spaces.
    QAction *expandTabAct = new QAction(tr("&Expand Tab"), this);
    expandTabAct->setToolTip(tr("Expand entered Tab to spaces"));
    expandTabAct->setCheckable(true);
    connect(expandTabAct, &QAction::triggered,
            this, &VMainWindow::changeExpandTab);

    // Tab stop width.
    QActionGroup *tabStopWidthAct = new QActionGroup(this);
    QAction *twoSpaceTabAct = new QAction(tr("2 Spaces"), tabStopWidthAct);
    twoSpaceTabAct->setToolTip(tr("Expand Tab to 2 spaces"));
    twoSpaceTabAct->setCheckable(true);
    twoSpaceTabAct->setData(2);
    QAction *fourSpaceTabAct = new QAction(tr("4 Spaces"), tabStopWidthAct);
    fourSpaceTabAct->setToolTip(tr("Expand Tab to 4 spaces"));
    fourSpaceTabAct->setCheckable(true);
    fourSpaceTabAct->setData(4);
    QAction *eightSpaceTabAct = new QAction(tr("8 Spaces"), tabStopWidthAct);
    eightSpaceTabAct->setToolTip(tr("Expand Tab to 8 spaces"));
    eightSpaceTabAct->setCheckable(true);
    eightSpaceTabAct->setData(8);
    connect(tabStopWidthAct, &QActionGroup::triggered,
            this, &VMainWindow::setTabStopWidth);

    // Auto Indent.
    m_autoIndentAct = new QAction(tr("Auto Indent"), this);
    m_autoIndentAct->setToolTip(tr("Indent automatically when inserting a new line"));
    m_autoIndentAct->setCheckable(true);
    connect(m_autoIndentAct, &QAction::triggered,
            this, &VMainWindow::changeAutoIndent);

    // Auto List.
    QAction *autoListAct = new QAction(tr("Auto List"), this);
    autoListAct->setToolTip(tr("Continue the list automatically when inserting a new line"));
    autoListAct->setCheckable(true);
    connect(autoListAct, &QAction::triggered,
            this, &VMainWindow::changeAutoList);

    // Highlight current cursor line.
    QAction *cursorLineAct = new QAction(tr("Highlight Cursor Line"), this);
    cursorLineAct->setToolTip(tr("Highlight current cursor line"));
    cursorLineAct->setCheckable(true);
    connect(cursorLineAct, &QAction::triggered,
            this, &VMainWindow::changeHighlightCursorLine);

    // Highlight selected word.
    QAction *selectedWordAct = new QAction(tr("Highlight Selected Words"), this);
    selectedWordAct->setToolTip(tr("Highlight all occurences of selected words"));
    selectedWordAct->setCheckable(true);
    connect(selectedWordAct, &QAction::triggered,
            this, &VMainWindow::changeHighlightSelectedWord);

    // Highlight trailing space.
    QAction *trailingSapceAct = new QAction(tr("Highlight Trailing Spaces"), this);
    trailingSapceAct->setToolTip(tr("Highlight all the spaces at the end of a line"));
    trailingSapceAct->setCheckable(true);
    connect(trailingSapceAct, &QAction::triggered,
            this, &VMainWindow::changeHighlightTrailingSapce);

    // Highlight tab.
    QAction *tabAct = new QAction(tr("Highlight Tabs"), this);
    tabAct->setToolTip(tr("Highlight all the tabs"));
    tabAct->setCheckable(true);
    connect(tabAct, &QAction::triggered,
            this, [](bool p_checked) {
                g_config->setEnableTabHighlight(p_checked);
            });

    QMenu *findReplaceMenu = editMenu->addMenu(tr("Find/Replace"));
    findReplaceMenu->setToolTipsVisible(true);
    findReplaceMenu->addAction(m_findReplaceAct);
    findReplaceMenu->addAction(advFindAct);
    findReplaceMenu->addSeparator();
    findReplaceMenu->addAction(m_findNextAct);
    findReplaceMenu->addAction(m_findPreviousAct);
    findReplaceMenu->addAction(m_replaceAct);
    findReplaceMenu->addAction(m_replaceFindAct);
    findReplaceMenu->addAction(m_replaceAllAct);
    findReplaceMenu->addSeparator();
    findReplaceMenu->addAction(searchedWordAct);
    searchedWordAct->setChecked(g_config->getHighlightSearchedWord());

    m_findReplaceAct->setEnabled(false);
    m_findNextAct->setEnabled(false);
    m_findPreviousAct->setEnabled(false);
    m_replaceAct->setEnabled(false);
    m_replaceFindAct->setEnabled(false);
    m_replaceAllAct->setEnabled(false);

    editMenu->addSeparator();
    editMenu->addAction(expandTabAct);
    if (g_config->getIsExpandTab()) {
        expandTabAct->setChecked(true);
    } else {
        expandTabAct->setChecked(false);
    }

    QMenu *tabStopWidthMenu = editMenu->addMenu(tr("Tab Stop Width"));
    tabStopWidthMenu->setToolTipsVisible(true);
    tabStopWidthMenu->addAction(twoSpaceTabAct);
    tabStopWidthMenu->addAction(fourSpaceTabAct);
    tabStopWidthMenu->addAction(eightSpaceTabAct);
    int tabStopWidth = g_config->getTabStopWidth();
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

    editMenu->addAction(m_autoIndentAct);
    m_autoIndentAct->setChecked(g_config->getAutoIndent());

    editMenu->addAction(autoListAct);
    if (g_config->getAutoList()) {
        // Let the trigger handler to trigger m_autoIndentAct, too.
        autoListAct->trigger();
    }
    Q_ASSERT(!(autoListAct->isChecked() && !m_autoIndentAct->isChecked()));

    editMenu->addSeparator();

    initEditorStyleMenu(editMenu);

    initEditorBackgroundMenu(editMenu);

    initEditorLineNumberMenu(editMenu);

    editMenu->addAction(cursorLineAct);
    cursorLineAct->setChecked(g_config->getHighlightCursorLine());

    editMenu->addAction(selectedWordAct);
    selectedWordAct->setChecked(g_config->getHighlightSelectedWord());

    editMenu->addAction(trailingSapceAct);
    trailingSapceAct->setChecked(g_config->getEnableTrailingSpaceHighlight());

    editMenu->addAction(tabAct);
    tabAct->setChecked(g_config->getEnableTabHighlight());

    initAutoScrollCursorLineMenu(editMenu);

    // Smart table.
    QAction *smartTableAct = new QAction(tr("Smart Table"), this);
    smartTableAct->setToolTip(tr("Format table automatically"));
    smartTableAct->setCheckable(true);
    connect(smartTableAct, &QAction::triggered,
            this, [](bool p_checked) {
                g_config->setEnableSmartTable(p_checked);
            });
    editMenu->addAction(smartTableAct);
    smartTableAct->setChecked(g_config->getEnableSmartTable());
}

void VMainWindow::initDockWindows()
{
    setTabPosition(Qt::LeftDockWidgetArea, QTabWidget::West);
    setTabPosition(Qt::RightDockWidgetArea, QTabWidget::East);
    setTabPosition(Qt::TopDockWidgetArea, QTabWidget::North);
    setTabPosition(Qt::BottomDockWidgetArea, QTabWidget::North);

    setDockNestingEnabled(true);

    initToolsDock();
    initSearchDock();
}

void VMainWindow::initToolsDock()
{
    m_toolDock = new QDockWidget(tr("Tools"), this);
    m_toolDock->setObjectName("ToolsDock");
    m_toolDock->setAllowedAreas(Qt::AllDockWidgetAreas);

    // Outline tree.
    outline = new VOutline(this);
    connect(m_editArea, &VEditArea::outlineChanged,
            outline, &VOutline::updateOutline);
    connect(m_editArea, &VEditArea::currentHeaderChanged,
            outline, &VOutline::updateCurrentHeader);
    connect(outline, &VOutline::outlineItemActivated,
            m_editArea, &VEditArea::scrollToHeader);

    // Snippets.
    m_snippetList = new VSnippetList(this);

    // Cart.
    m_cart = new VCart(this);

    m_toolBox = new VToolBox(this);
    m_toolBox->addItem(outline,
                       ":/resources/icons/outline.svg",
                       tr("Outline"));
    m_toolBox->addItem(m_snippetList,
                       ":/resources/icons/snippets.svg",
                       tr("Snippets"));
    m_toolBox->addItem(m_cart,
                       ":/resources/icons/cart.svg",
                       tr("Cart"));

    m_toolDock->setWidget(m_toolBox);
    addDockWidget(Qt::RightDockWidgetArea, m_toolDock);

    QAction *toggleAct = m_toolDock->toggleViewAction();
    toggleAct->setToolTip(tr("Toggle the tools dock widget"));
    VUtils::fixTextWithCaptainShortcut(toggleAct, "ToolsDock");

    m_viewMenu->addAction(toggleAct);
}

void VMainWindow::initSearchDock()
{
    m_searchDock = new QDockWidget(tr("Search"), this);
    m_searchDock->setObjectName("SearchDock");
    m_searchDock->setAllowedAreas(Qt::AllDockWidgetAreas);

    m_searcher = new VSearcher(this);

    m_searchDock->setWidget(m_searcher);

    addDockWidget(Qt::RightDockWidgetArea, m_searchDock);

    QAction *toggleAct = m_searchDock->toggleViewAction();
    toggleAct->setToolTip(tr("Toggle the search dock widget"));
    VUtils::fixTextWithCaptainShortcut(toggleAct, "SearchDock");

    m_viewMenu->addAction(toggleAct);
}

void VMainWindow::importNoteFromFile()
{
    static QString lastPath = QDir::homePath();
    QStringList files = QFileDialog::getOpenFileNames(this,
                                                      tr("Select Files To Create Notes"),
                                                      lastPath);
    if (files.isEmpty()) {
        return;
    }

    // Update lastPath
    lastPath = QFileInfo(files[0]).path();

    QString msg;
    if (!m_fileList->importFiles(files, &msg)) {
        VUtils::showMessage(QMessageBox::Warning,
                            tr("Warning"),
                            tr("Fail to create notes for all the files."),
                            msg,
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
    } else {
        int cnt = files.size();
        showStatusMessage(tr("%1 %2 created from external files")
                            .arg(cnt)
                            .arg(cnt > 1 ? tr("notes") : tr("note")));
    }
}

void VMainWindow::changeMarkdownConverter(QAction *action)
{
    if (!action) {
        return;
    }

    MarkdownConverterType type = (MarkdownConverterType)action->data().toInt();

    g_config->setMarkdownConverterType(type);
}

void VMainWindow::aboutMessage()
{
    QString info = tr("VNote");
    info += "<br/>";
    info += tr("Version: %1").arg(VConfigManager::c_version);
    info += "<br/>";
    info += tr("Author: Le Tan (tamlok)");
    info += "<br/><br/>";
    info += tr("VNote is a free and open source note-taking application that knows programmers and Markdown better.");
    info += "<br/><br/>";
    info += tr("Please visit <a href=\"https://tamlok.github.io/vnote\">VNote</a> for more information.");
    QMessageBox::about(this, tr("About VNote"), info);
}

void VMainWindow::changeExpandTab(bool checked)
{
    g_config->setIsExpandTab(checked);
}

void VMainWindow::enableMermaid(bool p_checked)
{
    g_config->setEnableMermaid(p_checked);
    VUtils::promptForReopen(this);
}

void VMainWindow::enableMathjax(bool p_checked)
{
    g_config->setEnableMathjax(p_checked);
    VUtils::promptForReopen(this);
}

void VMainWindow::changeHighlightCursorLine(bool p_checked)
{
    g_config->setHighlightCursorLine(p_checked);
}

void VMainWindow::changeHighlightSelectedWord(bool p_checked)
{
    g_config->setHighlightSelectedWord(p_checked);
}

void VMainWindow::changeHighlightSearchedWord(bool p_checked)
{
    g_config->setHighlightSearchedWord(p_checked);
}

void VMainWindow::changeHighlightTrailingSapce(bool p_checked)
{
    g_config->setEnableTrailingSapceHighlight(p_checked);
}

void VMainWindow::setTabStopWidth(QAction *action)
{
    if (!action) {
        return;
    }
    g_config->setTabStopWidth(action->data().toInt());
}

void VMainWindow::setEditorBackgroundColor(QAction *action)
{
    if (!action) {
        return;
    }

    g_config->setCurBackgroundColor(action->data().toString());
}

void VMainWindow::initConverterMenu(QMenu *p_menu)
{
    QMenu *converterMenu = p_menu->addMenu(tr("&Renderer"));
    converterMenu->setToolTipsVisible(true);

    QActionGroup *converterAct = new QActionGroup(this);
    QAction *markedAct = new QAction(tr("Marked"), converterAct);
    markedAct->setToolTip(tr("Use Marked to convert Markdown to HTML (re-open current tabs to make it work)"));
    markedAct->setCheckable(true);
    markedAct->setData(int(MarkdownConverterType::Marked));

    QAction *hoedownAct = new QAction(tr("Hoedown"), converterAct);
    hoedownAct->setToolTip(tr("Use Hoedown to convert Markdown to HTML (re-open current tabs to make it work)"));
    hoedownAct->setCheckable(true);
    hoedownAct->setData(int(MarkdownConverterType::Hoedown));

    QAction *markdownitAct = new QAction(tr("Markdown-it"), converterAct);
    markdownitAct->setToolTip(tr("Use Markdown-it to convert Markdown to HTML (re-open current tabs to make it work)"));
    markdownitAct->setCheckable(true);
    markdownitAct->setData(int(MarkdownConverterType::MarkdownIt));

    QAction *showdownAct = new QAction(tr("Showdown"), converterAct);
    showdownAct->setToolTip(tr("Use Showdown to convert Markdown to HTML (re-open current tabs to make it work)"));
    showdownAct->setCheckable(true);
    showdownAct->setData(int(MarkdownConverterType::Showdown));

    connect(converterAct, &QActionGroup::triggered,
            this, &VMainWindow::changeMarkdownConverter);
    converterMenu->addAction(hoedownAct);
    converterMenu->addAction(markedAct);
    converterMenu->addAction(markdownitAct);
    converterMenu->addAction(showdownAct);

    MarkdownConverterType converterType = g_config->getMdConverterType();
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

    case MarkdownConverterType::Showdown:
        showdownAct->setChecked(true);
        break;

    default:
        Q_ASSERT(false);
    }
}

void VMainWindow::initMarkdownitOptionMenu(QMenu *p_menu)
{
    QMenu *optMenu = p_menu->addMenu(tr("Markdown-it Options"));
    optMenu->setToolTipsVisible(true);

    const MarkdownitOption &opt = g_config->getMarkdownitOption();

    QAction *htmlAct = new QAction(tr("HTML"), this);
    htmlAct->setToolTip(tr("Enable HTML tags in source (re-open current tabs to make it work)"));
    htmlAct->setCheckable(true);
    htmlAct->setChecked(opt.m_html);
    connect(htmlAct, &QAction::triggered,
            this, [](bool p_checked) {
                MarkdownitOption opt = g_config->getMarkdownitOption();
                opt.m_html = p_checked;
                g_config->setMarkdownitOption(opt);
            });

    QAction *breaksAct = new QAction(tr("Line Break"), this);
    breaksAct->setToolTip(tr("Convert '\\n' in paragraphs into line break (re-open current tabs to make it work)"));
    breaksAct->setCheckable(true);
    breaksAct->setChecked(opt.m_breaks);
    connect(breaksAct, &QAction::triggered,
            this, [](bool p_checked) {
                MarkdownitOption opt = g_config->getMarkdownitOption();
                opt.m_breaks = p_checked;
                g_config->setMarkdownitOption(opt);
            });

    QAction *linkifyAct = new QAction(tr("Linkify"), this);
    linkifyAct->setToolTip(tr("Convert URL-like text into links (re-open current tabs to make it work)"));
    linkifyAct->setCheckable(true);
    linkifyAct->setChecked(opt.m_linkify);
    connect(linkifyAct, &QAction::triggered,
            this, [](bool p_checked) {
                MarkdownitOption opt = g_config->getMarkdownitOption();
                opt.m_linkify = p_checked;
                g_config->setMarkdownitOption(opt);
            });

    QAction *supAct = new QAction(tr("Superscript"), this);
    supAct->setToolTip(tr("Enable superscript like ^vnote^ (re-open current tabs to make it work)"));
    supAct->setCheckable(true);
    supAct->setChecked(opt.m_sup);
    connect(supAct, &QAction::triggered,
            this, [](bool p_checked) {
                MarkdownitOption opt = g_config->getMarkdownitOption();
                opt.m_sup = p_checked;
                g_config->setMarkdownitOption(opt);
            });

    QAction *subAct = new QAction(tr("Subscript"), this);
    subAct->setToolTip(tr("Enable subscript like ~vnote~ (re-open current tabs to make it work)"));
    subAct->setCheckable(true);
    subAct->setChecked(opt.m_sub);
    connect(subAct, &QAction::triggered,
            this, [](bool p_checked) {
                MarkdownitOption opt = g_config->getMarkdownitOption();
                opt.m_sub = p_checked;
                g_config->setMarkdownitOption(opt);
            });

    QAction *metadataAct = new QAction(tr("Metadata Aware"), this);
    metadataAct->setToolTip(tr("Be aware of metadata in YAML format (re-open current tabs to make it work)"));
    metadataAct->setCheckable(true);
    metadataAct->setChecked(opt.m_metadata);
    connect(metadataAct, &QAction::triggered,
            this, [](bool p_checked) {
                MarkdownitOption opt = g_config->getMarkdownitOption();
                opt.m_metadata = p_checked;
                g_config->setMarkdownitOption(opt);
            });

    QAction *emojiAct = new QAction(tr("Emoji"), this);
    emojiAct->setToolTip(tr("Enable emoji and emoticon (re-open current tabs to make it work)"));
    emojiAct->setCheckable(true);
    emojiAct->setChecked(opt.m_emoji);
    connect(emojiAct, &QAction::triggered,
            this, [](bool p_checked) {
                MarkdownitOption opt = g_config->getMarkdownitOption();
                opt.m_emoji = p_checked;
                g_config->setMarkdownitOption(opt);
            });

    optMenu->addAction(htmlAct);
    optMenu->addAction(breaksAct);
    optMenu->addAction(linkifyAct);
    optMenu->addAction(supAct);
    optMenu->addAction(subAct);
    optMenu->addAction(metadataAct);
    optMenu->addAction(emojiAct);
}

void VMainWindow::initMarkdownExtensionMenu(QMenu *p_menu)
{
    QMenu *optMenu = p_menu->addMenu(tr("Extensions"));
    optMenu->setToolTipsVisible(true);

    QAction *mermaidAct = new QAction(tr("&Mermaid"), optMenu);
    mermaidAct->setToolTip(tr("Enable Mermaid for graph and diagram (re-open current tabs to make it work)"));
    mermaidAct->setCheckable(true);
    mermaidAct->setChecked(g_config->getEnableMermaid());
    connect(mermaidAct, &QAction::triggered,
            this, &VMainWindow::enableMermaid);
    optMenu->addAction(mermaidAct);

    QAction *flowchartAct = new QAction(tr("&Flowchart.js"), optMenu);
    flowchartAct->setToolTip(tr("Enable Flowchart.js for flowchart diagram (re-open current tabs to make it work)"));
    flowchartAct->setCheckable(true);
    flowchartAct->setChecked(g_config->getEnableFlowchart());
    connect(flowchartAct, &QAction::triggered,
            this, [this](bool p_enabled){
                g_config->setEnableFlowchart(p_enabled);
                VUtils::promptForReopen(this);
            });
    optMenu->addAction(flowchartAct);

    QAction *mathjaxAct = new QAction(tr("Math&Jax"), optMenu);
    mathjaxAct->setToolTip(tr("Enable MathJax for math support in Markdown (re-open current tabs to make it work)"));
    mathjaxAct->setCheckable(true);
    mathjaxAct->setChecked(g_config->getEnableMathjax());
    connect(mathjaxAct, &QAction::triggered,
            this, &VMainWindow::enableMathjax);
    optMenu->addAction(mathjaxAct);

    QAction *wavedromAct = new QAction(tr("&WaveDrom"), optMenu);
    wavedromAct->setToolTip(tr("Enable WaveDrom for digital timing diagram (re-open current tabs to make it work)"));
    wavedromAct->setCheckable(true);
    wavedromAct->setChecked(g_config->getEnableWavedrom());
    connect(wavedromAct, &QAction::triggered,
            this, [this](bool p_enabled){
                g_config->setEnableWavedrom(p_enabled);
                VUtils::promptForReopen(this);
            });
    optMenu->addAction(wavedromAct);
}

void VMainWindow::initRenderBackgroundMenu(QMenu *menu)
{
    QActionGroup *renderBackgroundAct = new QActionGroup(this);
    connect(renderBackgroundAct, &QActionGroup::triggered,
            this, &VMainWindow::setRenderBackgroundColor);

    QMenu *renderBgMenu = menu->addMenu(tr("&Rendering Background"));
    renderBgMenu->setToolTipsVisible(true);
    const QString &curBgColor = g_config->getCurRenderBackgroundColor();
    QAction *tmpAct = new QAction(tr("System"), renderBackgroundAct);
    tmpAct->setToolTip(tr("Use system's background color configuration for Markdown rendering"));
    tmpAct->setCheckable(true);
    tmpAct->setData("System");
    if (curBgColor == "System") {
        tmpAct->setChecked(true);
    }

    renderBgMenu->addAction(tmpAct);

    tmpAct = new QAction(tr("Transparent"), renderBackgroundAct);
    tmpAct->setToolTip(tr("Use a transparent background for Markdown rendering"));
    tmpAct->setCheckable(true);
    tmpAct->setData("Transparent");
    if (curBgColor == "Transparent") {
        tmpAct->setChecked(true);
    }

    renderBgMenu->addAction(tmpAct);

    const QVector<VColor> &bgColors = g_config->getCustomColors();
    for (int i = 0; i < bgColors.size(); ++i) {
        tmpAct = new QAction(bgColors[i].m_name, renderBackgroundAct);
        tmpAct->setToolTip(tr("Set as the background color for Markdown rendering "
                              "(re-open current tabs to make it work)"));
        tmpAct->setCheckable(true);
        tmpAct->setData(bgColors[i].m_name);

#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
        QColor color(bgColors[i].m_color);
        QPixmap pixmap(COLOR_PIXMAP_ICON_SIZE, COLOR_PIXMAP_ICON_SIZE);
        pixmap.fill(color);
        tmpAct->setIcon(QIcon(pixmap));
#endif

        if (curBgColor == bgColors[i].m_name) {
            tmpAct->setChecked(true);
        }

        renderBgMenu->addAction(tmpAct);
    }
}

void VMainWindow::initRenderStyleMenu(QMenu *p_menu)
{
    QMenu *styleMenu = p_menu->addMenu(tr("Rendering &Style"));
    styleMenu->setToolTipsVisible(true);

    QAction *addAct = newAction(VIconUtils::menuIcon(":/resources/icons/add_style.svg"),
                                tr("Add Style"),
                                styleMenu);
    addAct->setToolTip(tr("Add custom style of read mode"));
    connect(addAct, &QAction::triggered,
            this, [this]() {
                VTipsDialog dialog(VUtils::getDocFile("tips_add_style.md"),
                                   tr("Add Style"),
                                   []() {
                                       QUrl url = QUrl::fromLocalFile(g_config->getStyleConfigFolder());
                                       QDesktopServices::openUrl(url);
                                   },
                                   this);
                dialog.exec();
            });

    styleMenu->addAction(addAct);

    QActionGroup *ag = new QActionGroup(this);
    connect(ag, &QActionGroup::triggered,
            this, [](QAction *p_action) {
                QString data = p_action->data().toString();
                g_config->setCssStyle(data);
                g_vnote->updateTemplate();
            });

    QList<QString> styles = g_config->getCssStyles();
    QString curStyle = g_config->getCssStyle();
    for (auto const &style : styles) {
        QAction *act = new QAction(style, ag);
        act->setToolTip(tr("Set as the CSS style for Markdown rendering "
                           "(re-open current tabs to make it work)"));
        act->setCheckable(true);
        act->setData(style);

        // Add it to the menu.
        styleMenu->addAction(act);

        if (curStyle == style) {
            act->setChecked(true);
        }
    }
}

void VMainWindow::initCodeBlockStyleMenu(QMenu *p_menu)
{
    QMenu *styleMenu = p_menu->addMenu(tr("Code Block Style"));
    styleMenu->setToolTipsVisible(true);

    QAction *addAct = newAction(VIconUtils::menuIcon(":/resources/icons/add_style.svg"),
                                tr("Add Style"),
                                styleMenu);
    addAct->setToolTip(tr("Add custom style of code block in read mode"));
    connect(addAct, &QAction::triggered,
            this, [this]() {
                VTipsDialog dialog(VUtils::getDocFile("tips_add_style.md"),
                                   tr("Add Style"),
                                   []() {
                                       QUrl url = QUrl::fromLocalFile(g_config->getCodeBlockStyleConfigFolder());
                                       QDesktopServices::openUrl(url);
                                   },
                                   this);
                dialog.exec();
            });

    styleMenu->addAction(addAct);

    QActionGroup *ag = new QActionGroup(this);
    connect(ag, &QActionGroup::triggered,
            this, [](QAction *p_action) {
                QString data = p_action->data().toString();
                g_config->setCodeBlockCssStyle(data);
                g_vnote->updateTemplate();
            });

    QList<QString> styles = g_config->getCodeBlockCssStyles();
    QString curStyle = g_config->getCodeBlockCssStyle();
    for (auto const &style : styles) {
        QAction *act = new QAction(style, ag);
        act->setToolTip(tr("Set as the code block CSS style for Markdown rendering "
                           "(re-open current tabs to make it work)"));
        act->setCheckable(true);
        act->setData(style);

        // Add it to the menu.
        styleMenu->addAction(act);

        if (curStyle == style) {
            act->setChecked(true);
        }
    }
}

void VMainWindow::initEditorBackgroundMenu(QMenu *menu)
{
    QMenu *backgroundColorMenu = menu->addMenu(tr("&Background Color"));
    backgroundColorMenu->setToolTipsVisible(true);

    QActionGroup *backgroundColorAct = new QActionGroup(this);
    connect(backgroundColorAct, &QActionGroup::triggered,
            this, &VMainWindow::setEditorBackgroundColor);

    // System background color
    const QString &curBgColor = g_config->getCurBackgroundColor();
    QAction *tmpAct = new QAction(tr("System"), backgroundColorAct);
    tmpAct->setToolTip(tr("Use system's background color configuration for editor"));
    tmpAct->setCheckable(true);
    tmpAct->setData("System");
    if (curBgColor == "System") {
        tmpAct->setChecked(true);
    }
    backgroundColorMenu->addAction(tmpAct);
    const QVector<VColor> &bgColors = g_config->getCustomColors();
    for (int i = 0; i < bgColors.size(); ++i) {
        tmpAct = new QAction(bgColors[i].m_name, backgroundColorAct);
        tmpAct->setToolTip(tr("Set as the background color for editor (re-open current tabs to make it work)"));
        tmpAct->setCheckable(true);
        tmpAct->setData(bgColors[i].m_name);

#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
        QColor color(bgColors[i].m_color);
        QPixmap pixmap(COLOR_PIXMAP_ICON_SIZE, COLOR_PIXMAP_ICON_SIZE);
        pixmap.fill(color);
        tmpAct->setIcon(QIcon(pixmap));
#endif

        if (curBgColor == bgColors[i].m_name) {
            tmpAct->setChecked(true);
        }

        backgroundColorMenu->addAction(tmpAct);
    }
}

void VMainWindow::initEditorLineNumberMenu(QMenu *p_menu)
{
    QMenu *lineNumMenu = p_menu->addMenu(tr("Line Number"));
    lineNumMenu->setToolTipsVisible(true);

    QActionGroup *lineNumAct = new QActionGroup(lineNumMenu);
    connect(lineNumAct, &QActionGroup::triggered,
            this, [this](QAction *p_action){
                if (!p_action) {
                    return;
                }

                g_config->setEditorLineNumber(p_action->data().toInt());
                emit editorConfigUpdated();
            });

    int lineNumberMode = g_config->getEditorLineNumber();

    QAction *act = lineNumAct->addAction(tr("None"));
    act->setToolTip(tr("Do not display line number in edit mode"));
    act->setCheckable(true);
    act->setData(0);
    lineNumMenu->addAction(act);
    if (lineNumberMode == 0) {
        act->setChecked(true);
    }

    act = lineNumAct->addAction(tr("Absolute"));
    act->setToolTip(tr("Display absolute line number in edit mode"));
    act->setCheckable(true);
    act->setData(1);
    lineNumMenu->addAction(act);
    if (lineNumberMode == 1) {
        act->setChecked(true);
    }

    act = lineNumAct->addAction(tr("Relative"));
    act->setToolTip(tr("Display line number relative to current cursor line in edit mode"));
    act->setCheckable(true);
    act->setData(2);
    lineNumMenu->addAction(act);
    if (lineNumberMode == 2) {
        act->setChecked(true);
    }

    act = lineNumAct->addAction(tr("CodeBlock"));
    act->setToolTip(tr("Display line number in code block in edit mode (for Markdown only)"));
    act->setCheckable(true);
    act->setData(3);
    lineNumMenu->addAction(act);
    if (lineNumberMode == 3) {
        act->setChecked(true);
    }
}

void VMainWindow::initEditorStyleMenu(QMenu *p_menu)
{
    QMenu *styleMenu = p_menu->addMenu(tr("Editor &Style"));
    styleMenu->setToolTipsVisible(true);

    QAction *addAct = newAction(VIconUtils::menuIcon(":/resources/icons/add_style.svg"),
                                tr("Add Style"),
                                styleMenu);
    addAct->setToolTip(tr("Add custom style of editor"));
    connect(addAct, &QAction::triggered,
            this, [this]() {
                VTipsDialog dialog(VUtils::getDocFile("tips_add_style.md"),
                                   tr("Add Style"),
                                   []() {
                                       QUrl url = QUrl::fromLocalFile(g_config->getStyleConfigFolder());
                                       QDesktopServices::openUrl(url);
                                   },
                                   this);
                dialog.exec();
            });

    styleMenu->addAction(addAct);

    QActionGroup *ag = new QActionGroup(this);
    connect(ag, &QActionGroup::triggered,
            this, [](QAction *p_action) {
                QString data = p_action->data().toString();
                g_config->setEditorStyle(data);
            });

    QList<QString> styles = g_config->getEditorStyles();
    QString style = g_config->getEditorStyle();
    for (auto const &item : styles) {
        QAction *act = new QAction(item, ag);
        act->setToolTip(tr("Set as the editor style (re-open current tabs to make it work)"));
        act->setCheckable(true);
        act->setData(item);

        // Add it to the menu.
        styleMenu->addAction(act);

        if (style == item) {
            act->setChecked(true);
        }
    }
}

void VMainWindow::initAutoScrollCursorLineMenu(QMenu *p_menu)
{
    QMenu *subMenu = p_menu->addMenu(tr("Auto Scroll Cursor Line"));
    subMenu->setToolTipsVisible(true);

    QActionGroup *ag = new QActionGroup(this);
    connect(ag, &QActionGroup::triggered,
            this, [](QAction *p_action) {
                g_config->setAutoScrollCursorLine(p_action->data().toInt());
            });

    int mode = g_config->getAutoScrollCursorLine();

    int data = AutoScrollDisabled;
    QAction *act = new QAction(tr("Disabled"), ag);
    act->setCheckable(true);
    act->setData(data);
    subMenu->addAction(act);
    if (mode == data) {
        act->setChecked(true);
    }

    data = AutoScrollEndOfDoc;
    act = new QAction(tr("End Of Document"), ag);
    act->setToolTip(tr("Scroll cursor line into the center when it locates at the end of document"));
    act->setCheckable(true);
    act->setData(data);
    subMenu->addAction(act);
    if (mode == data) {
        act->setChecked(true);
    }

    data = AutoScrollAlways;
    act = new QAction(tr("Always"), ag);
    act->setToolTip(tr("Always scroll cursor line into the center"));
    act->setCheckable(true);
    act->setData(data);
    subMenu->addAction(act);
    if (mode == data) {
        act->setChecked(true);
    }
}

void VMainWindow::setRenderBackgroundColor(QAction *action)
{
    if (!action) {
        return;
    }

    g_config->setCurRenderBackgroundColor(action->data().toString());
    vnote->updateTemplate();
}

void VMainWindow::updateActionsStateFromTab(const VEditTab *p_tab)
{
    const VFile *file = p_tab ? p_tab->getFile() : NULL;
    bool editMode = p_tab ? p_tab->isEditMode() : false;
    bool systemFile = file
                      && file->getType() == FileType::Orphan
                      && dynamic_cast<const VOrphanFile *>(file)->isSystemFile();

    m_printAct->setEnabled(file && file->getDocType() == DocType::Markdown);

    updateEditReadAct(p_tab);

    saveNoteAct->setEnabled(file && editMode && file->isModifiable());
    deleteNoteAct->setEnabled(file && file->getType() == FileType::Note);
    noteInfoAct->setEnabled(file && !systemFile);

    m_attachmentBtn->setEnabled(file && file->getType() == FileType::Note);

    m_headingBtn->setEnabled(file && editMode);

    setActionsEnabled(m_editToolBar, file && editMode);

    // Handle heading sequence act independently.
    m_headingSequenceAct->setEnabled(editMode
                                     && file->isModifiable()
                                     && isHeadingSequenceApplicable());
    const VMdTab *mdTab = dynamic_cast<const VMdTab *>(p_tab);
    m_headingSequenceAct->setChecked(mdTab
                                     && editMode
                                     && file->isModifiable()
                                     && mdTab->isHeadingSequenceEnabled());

    // Find/Replace
    m_findReplaceAct->setEnabled(file);
    m_findNextAct->setEnabled(file);
    m_findPreviousAct->setEnabled(file);
    m_replaceAct->setEnabled(file && editMode);
    m_replaceFindAct->setEnabled(file && editMode);
    m_replaceAllAct->setEnabled(file && editMode);

    if (!file) {
        m_findReplaceDialog->closeDialog();
    }

    m_updateTimer->start();
}

void VMainWindow::handleAreaTabStatusUpdated(const VEditTabInfo &p_info)
{
    if (m_curTab != p_info.m_editTab) {
        if (m_curTab) {
            if (m_vimCmd->isVisible()) {
                m_curTab->handleVimCmdCommandCancelled();
            }

            // Disconnect the trigger signal from edit tab.
            disconnect((VEditTab *)m_curTab, 0, m_vimCmd, 0);
        }

        m_curTab = p_info.m_editTab;
        if (m_curTab) {
            connect((VEditTab *)m_curTab, &VEditTab::triggerVimCmd,
                    m_vimCmd, &VVimCmdLineEdit::reset);
        }

        m_vimCmd->hide();
    }

    if (m_curTab) {
        m_curFile = m_curTab->getFile();
    } else {
        m_curFile = NULL;
    }

    if (p_info.m_type == VEditTabInfo::InfoType::All) {
        updateActionsStateFromTab(m_curTab);

        m_attachmentList->setFile(dynamic_cast<VNoteFile *>(m_curFile.data()));

        if (m_syncNoteListToCurrentTab && g_config->getSyncNoteListToTab()) {
            locateFile(m_curFile, false, false);
        }

        QString title;
        if (m_curFile) {
            m_findReplaceDialog->updateState(m_curFile->getDocType(),
                                             m_curTab->isEditMode());

            if (m_curFile->getType() == FileType::Note) {
                const VNoteFile *tmpFile = dynamic_cast<const VNoteFile *>((VFile *)m_curFile);
                title = QString("[%1] %2").arg(tmpFile->getNotebookName()).arg(tmpFile->fetchPath());
            } else {
                title = QString("%1").arg(m_curFile->fetchPath());
            }

            if (!m_curFile->isModifiable()) {
                title.append('#');
            }

            if (m_curTab->isModified()) {
                title.append('*');
            }
        }

        updateWindowTitle(title);
    }

    updateStatusInfo(p_info);
}

void VMainWindow::changePanelView(PanelViewState p_state)
{
    switch (p_state) {
    case PanelViewState::ExpandMode:
        m_mainSplitter->widget(0)->hide();
        m_mainSplitter->widget(1)->show();
        break;

    case PanelViewState::HorizontalMode:
    case PanelViewState::VerticalMode:
        m_mainSplitter->widget(0)->show();
        m_mainSplitter->widget(1)->show();
        break;

    default:
        Q_ASSERT(false);
        break;
    }

    g_config->setPanelViewState((int)p_state);

    expandViewAct->setChecked(p_state == PanelViewState::ExpandMode);
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

    if (m_curFile->getType() == FileType::Note) {
        VNoteFile *file = dynamic_cast<VNoteFile *>((VFile *)m_curFile);
        Q_ASSERT(file);
        m_fileList->fileInfo(file);
    } else if (m_curFile->getType() == FileType::Orphan) {
        VOrphanFile *file = dynamic_cast<VOrphanFile *>((VFile *)m_curFile);
        Q_ASSERT(file);
        if (!file->isSystemFile()) {
            editOrphanFileInfo(m_curFile);
        }
    }
}

void VMainWindow::deleteCurNote()
{
    if (!m_curFile || m_curFile->getType() != FileType::Note) {
        return;
    }

    VNoteFile *file = dynamic_cast<VNoteFile *>((VFile *)m_curFile);
    m_fileList->deleteFile(file);
}

void VMainWindow::closeEvent(QCloseEvent *event)
{
    bool isExit = m_requestQuit || !g_config->getMinimizeToStystemTray();
    m_requestQuit = false;

    m_captain->exitCaptainMode();

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    // Do not support minimized to tray on macOS.
    if (!isExit) {
        event->accept();
        return;
    }
#endif

    if (!isExit && g_config->getMinimizeToStystemTray() == -1) {
        // Not initialized yet. Prompt for user.
        int ret = VUtils::showMessage(QMessageBox::Information,
                                      tr("Close VNote"),
                                      tr("Do you want to minimize VNote to system tray "
                                         "instead of quitting it when closing VNote?"),
                                      tr("You could change the option in Settings later."),
                                      QMessageBox::Ok | QMessageBox::No | QMessageBox::Cancel,
                                      QMessageBox::Ok,
                                      this);
        if (ret == QMessageBox::Ok) {
            g_config->setMinimizeToSystemTray(1);
        } else if (ret == QMessageBox::No) {
            g_config->setMinimizeToSystemTray(0);
            isExit = true;
        } else {
            event->ignore();
            return;
        }
    }

    if (isVisible()) {
        saveStateAndGeometry();
    }

    if (isExit || !m_trayIcon->isVisible()) {
        // Get all the opened tabs.
        bool saveOpenedNotes = g_config->getStartupPageType() == StartupPageType::ContinueLeftOff;
        QVector<VFileSessionInfo> fileInfos;
        QVector<VEditTabInfo> tabs;
        if (saveOpenedNotes) {
            tabs = m_editArea->getAllTabsInfo();

            fileInfos.reserve(tabs.size());

            for (auto const & tab : tabs) {
                // Skip system file.
                VFile *file = tab.m_editTab->getFile();
                if (file->getType() == FileType::Orphan
                    && dynamic_cast<VOrphanFile *>(file)->isSystemFile()) {
                    continue;
                }

                VFileSessionInfo info = VFileSessionInfo::fromEditTabInfo(&tab);
                if (tab.m_editTab == m_curTab) {
                    info.setActive(true);
                }

                fileInfos.push_back(info);

                qDebug() << "file session:" << info.m_file << (info.m_mode == OpenFileMode::Edit);
            }
        }

        m_syncNoteListToCurrentTab = false;

        if (!m_editArea->closeAllFiles(false)) {
            // Fail to close all the opened files, cancel closing app.
            event->ignore();
            m_syncNoteListToCurrentTab = true;
            return;
        }

        if (saveOpenedNotes) {
            g_config->setLastOpenedFiles(fileInfos);
        }

        QMainWindow::closeEvent(event);
        qApp->quit();
    } else {
        hide();
        event->ignore();
    }
}

void VMainWindow::saveStateAndGeometry()
{
    g_config->setMainWindowGeometry(saveGeometry());
    g_config->setMainWindowState(saveState());
    g_config->setToolsDockChecked(m_toolDock->isVisible());
    g_config->setSearchDockChecked(m_searchDock->isVisible());
    g_config->setMainSplitterState(m_mainSplitter->saveState());
    g_config->setNotebookSplitterState(m_nbSplitter->saveState());
    m_tagExplorer->saveStateAndGeometry();
    g_config->setNaviBoxCurrentIndex(m_naviBox->currentIndex());
}

void VMainWindow::restoreStateAndGeometry()
{
    const QByteArray geometry = g_config->getMainWindowGeometry();
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    }

    const QByteArray state = g_config->getMainWindowState();
    if (!state.isEmpty()) {
        // restoreState() will restore the state of dock widgets.
        restoreState(state);
    }

    const QByteArray splitterState = g_config->getMainSplitterState();
    if (!splitterState.isEmpty()) {
        m_mainSplitter->restoreState(splitterState);
    }

    const QByteArray nbSplitterState = g_config->getNotebookSplitterState();
    if (!nbSplitterState.isEmpty()) {
        m_nbSplitter->restoreState(nbSplitterState);
    }

    m_naviBox->setCurrentIndex(g_config->getNaviBoxCurrentIndex());
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

void VMainWindow::keyPressEvent(QKeyEvent *event)
{
    int key = event->key();
    Qt::KeyboardModifiers modifiers = event->modifiers();
    if (key == Qt::Key_Escape
        || (key == Qt::Key_BracketLeft
            && modifiers == Qt::ControlModifier)) {
        m_findReplaceDialog->closeDialog();
        event->accept();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

bool VMainWindow::locateFile(VFile *p_file, bool p_focus, bool p_show)
{
    bool ret = false;
    if (!p_file || p_file->getType() != FileType::Note) {
        return ret;
    }

    VNoteFile *file = dynamic_cast<VNoteFile *>(p_file);
    VNotebook *notebook = file->getNotebook();
    if (m_notebookSelector->locateNotebook(notebook)) {
        while (m_dirTree->currentNotebook() != notebook) {
            QCoreApplication::sendPostedEvents();
        }

        VDirectory *dir = file->getDirectory();
        if (m_dirTree->locateDirectory(dir)) {
            while (m_fileList->currentDirectory() != dir) {
                QCoreApplication::sendPostedEvents();
            }

            if (m_fileList->locateFile(file)) {
                ret = true;
                if (p_focus) {
                    m_fileList->setFocus();
                }
            }
        }
    }

    // Open the directory and file panels after location.
    if (ret && p_show) {
        showNotebookPanel();
    }

    return ret;
}

bool VMainWindow::locateDirectory(VDirectory *p_directory)
{
    bool ret = false;
    if (!p_directory) {
        return ret;
    }

    VNotebook *notebook = p_directory->getNotebook();
    if (m_notebookSelector->locateNotebook(notebook)) {
        while (m_dirTree->currentNotebook() != notebook) {
            QCoreApplication::sendPostedEvents();
        }

        if (m_dirTree->locateDirectory(p_directory)) {
            ret = true;
            m_dirTree->setFocus();
        }
    }

    // Open the directory and file panels after location.
    if (ret) {
        showNotebookPanel();
    }

    return ret;
}

bool VMainWindow::locateNotebook(VNotebook *p_notebook)
{
    bool ret = false;
    if (!p_notebook) {
        return ret;
    }

    if (m_notebookSelector->locateNotebook(p_notebook)) {
        ret = true;
        m_dirTree->setFocus();
    }

    // Open the directory and file panels after location.
    if (ret) {
        showNotebookPanel();
    }

    return ret;
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
    m_findReplaceDialog->openDialog(m_editArea->getSelectedText());
}

void VMainWindow::viewSettings()
{
    VSettingsDialog settingsDialog(this);
    if (settingsDialog.exec()) {
        if (settingsDialog.getNeedUpdateEditorFont()) {
            updateFontOfAllTabs();
        }
    }
}

void VMainWindow::closeCurrentFile()
{
    if (m_curFile) {
        m_editArea->closeFile(m_curFile, false);
    }
}

void VMainWindow::changeAutoIndent(bool p_checked)
{
    g_config->setAutoIndent(p_checked);
}

void VMainWindow::changeAutoList(bool p_checked)
{
    g_config->setAutoList(p_checked);
    if (p_checked) {
        if (!m_autoIndentAct->isChecked()) {
            m_autoIndentAct->trigger();
        }
        m_autoIndentAct->setEnabled(false);
    } else {
        m_autoIndentAct->setEnabled(true);
    }
}

void VMainWindow::enableCodeBlockHighlight(bool p_checked)
{
    g_config->setEnableCodeBlockHighlight(p_checked);
}

void VMainWindow::enableImagePreview(bool p_checked)
{
    g_config->setEnablePreviewImages(p_checked);

    emit editorConfigUpdated();
}

void VMainWindow::enableImagePreviewConstraint(bool p_checked)
{
    g_config->setEnablePreviewImageConstraint(p_checked);

    emit editorConfigUpdated();
}

void VMainWindow::enableImageConstraint(bool p_checked)
{
    g_config->setEnableImageConstraint(p_checked);

    vnote->updateTemplate();
}

void VMainWindow::enableImageCaption(bool p_checked)
{
    g_config->setEnableImageCaption(p_checked);
}

void VMainWindow::shortcutsHelp()
{
    QString docFile = VUtils::getDocFile(VNote::c_shortcutsDocFile);
    VFile *file = vnote->getFile(docFile, true);
    m_editArea->openFile(file, OpenFileMode::Read);
}

void VMainWindow::printNote()
{
    if (m_printer
        || !m_curFile
        || m_curFile->getDocType() != DocType::Markdown) {
        return;
    }

    m_printer = new QPrinter();
    QPrintDialog dialog(m_printer, this);
    dialog.setWindowTitle(tr("Print Note"));

    V_ASSERT(m_curTab);

    VMdTab *mdTab = dynamic_cast<VMdTab *>((VEditTab *)m_curTab);
    VWebView *webView = mdTab->getWebViewer();

    V_ASSERT(webView);

    if (webView->hasSelection()) {
        dialog.addEnabledOption(QAbstractPrintDialog::PrintSelection);
    }

    if (dialog.exec() == QDialog::Accepted) {
        webView->page()->print(m_printer, [this](bool p_succ) {
                    qDebug() << "print web page callback" << p_succ;
                    delete m_printer;
                    m_printer = NULL;
                });
    } else {
        delete m_printer;
        m_printer = NULL;
    }
}

QAction *VMainWindow::newAction(const QIcon &p_icon,
                                const QString &p_text,
                                QObject *p_parent)
{
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    Q_UNUSED(p_icon);
    return new QAction(p_text, p_parent);
#else
    return new QAction(p_icon, p_text, p_parent);
#endif
}

void VMainWindow::showStatusMessage(const QString &p_msg)
{
    const int timeout = 5000;
    statusBar()->showMessage(p_msg, timeout);
}

void VMainWindow::updateStatusInfo(const VEditTabInfo &p_info)
{
    if (m_curTab) {
        m_tabIndicator->update(p_info);
        m_tabIndicator->show();

        if (m_curTab->isEditMode()) {
            if (p_info.m_type == VEditTabInfo::InfoType::All) {
                m_curTab->requestUpdateVimStatus();
            }
        } else {
            m_vimIndicator->hide();
        }
    } else {
        m_tabIndicator->hide();
        m_vimIndicator->hide();
    }
}

void VMainWindow::handleVimStatusUpdated(const VVim *p_vim)
{
    m_vimIndicator->update(p_vim);
    if (!p_vim || !m_curTab || !m_curTab->isEditMode()) {
        m_vimIndicator->hide();
    } else {
        m_vimIndicator->show();
    }
}

bool VMainWindow::tryOpenInternalFile(const QString &p_filePath)
{
    if (p_filePath.isEmpty()) {
        return false;
    }

    if (QFileInfo::exists(p_filePath)) {
        VFile *file = vnote->getInternalFile(p_filePath);

        if (file) {
            m_editArea->openFile(file, OpenFileMode::Read);
            return true;
        }
    }

    return false;
}

QVector<VFile *> VMainWindow::openFiles(const QStringList &p_files,
                                        bool p_forceOrphan,
                                        OpenFileMode p_mode,
                                        bool p_forceMode,
                                        bool p_oneByOne)
{
    QVector<VFile *> vfiles;
    vfiles.reserve(p_files.size());

    for (int i = 0; i < p_files.size(); ++i) {
        if (!QFileInfo::exists(p_files[i])) {
            qWarning() << "file" << p_files[i] << "does not exist";
            continue;
        }

        VFile *file = vnote->getFile(p_files[i], p_forceOrphan);

        m_editArea->openFile(file, p_mode, p_forceMode);
        vfiles.append(file);

        if (p_oneByOne) {
            QCoreApplication::sendPostedEvents();
        }
    }

    return vfiles;
}

void VMainWindow::editOrphanFileInfo(VFile *p_file)
{
    VOrphanFile *file = dynamic_cast<VOrphanFile *>(p_file);
    Q_ASSERT(file);

    VOrphanFileInfoDialog dialog(file, this);
    if (dialog.exec() == QDialog::Accepted) {
        QString imgFolder = dialog.getImageFolder();
        file->setImageFolder(imgFolder);
    }
}

void VMainWindow::checkSharedMemory()
{
    QStringList files = m_guard->fetchFilesToOpen();
    if (!files.isEmpty()) {
        qDebug() << "shared memory fetch files" << files;
        openFiles(files, false, g_config->getNoteOpenMode(), false, false);

        // Eliminate the signal.
        m_guard->fetchAskedToShow();

        showMainWindow();
    } else if (m_guard->fetchAskedToShow()) {
        qDebug() << "shared memory asked to show up";
        showMainWindow();
    }
}

void VMainWindow::initTrayIcon()
{
    QMenu *menu = new QMenu(this);
    QAction *showMainWindowAct = menu->addAction(tr("Show VNote"));
    connect(showMainWindowAct, &QAction::triggered,
            this, &VMainWindow::showMainWindow);

    QAction *exitAct = menu->addAction(tr("Quit"));
    connect(exitAct, &QAction::triggered,
            this, &VMainWindow::quitApp);

    QIcon sysIcon(":/resources/icons/256x256/vnote.png");

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    sysIcon.setIsMask(true);
#endif

    m_trayIcon = new QSystemTrayIcon(sysIcon, this);
    m_trayIcon->setToolTip(tr("VNote"));
    m_trayIcon->setContextMenu(menu);

    connect(m_trayIcon, &QSystemTrayIcon::activated,
            this, [this](QSystemTrayIcon::ActivationReason p_reason){
#if !defined(Q_OS_MACOS) && !defined(Q_OS_MAC)
                if (p_reason == QSystemTrayIcon::Trigger) {
                    this->showMainWindow();
                }
#endif
            });

    m_trayIcon->show();
}

void VMainWindow::changeEvent(QEvent *p_event)
{
    if (p_event->type() == QEvent::WindowStateChange) {
        QWindowStateChangeEvent *eve = dynamic_cast<QWindowStateChangeEvent *>(p_event);
        m_windowOldState = eve->oldState();
    }

    QMainWindow::changeEvent(p_event);
}

void VMainWindow::showMainWindow()
{
    if (this->isMinimized()) {
        if (m_windowOldState & Qt::WindowMaximized) {
            this->showMaximized();
        } else if (m_windowOldState & Qt::WindowFullScreen) {
            this->showFullScreen();
        } else {
            this->showNormal();
        }
    } else {
        this->show();
        // Need to call raise() in macOS.
        this->raise();
    }

    this->activateWindow();
}

void VMainWindow::openStartupPages()
{
    StartupPageType type = g_config->getStartupPageType();
    switch (type) {
    case StartupPageType::ContinueLeftOff:
    {
        QVector<VFileSessionInfo> files = g_config->getLastOpenedFiles();
        qDebug() << "open" << files.size() << "last opened files";
        m_editArea->openFiles(files, true);
        break;
    }

    case StartupPageType::SpecificPages:
    {
        QStringList pagesToOpen = VUtils::filterFilePathsToOpen(g_config->getStartupPages());
        qDebug() << "open startup pages" << pagesToOpen;
        openFiles(pagesToOpen, false, g_config->getNoteOpenMode(), false, true);
        break;
    }

    default:
        break;
    }
}

bool VMainWindow::isHeadingSequenceApplicable() const
{
    if (!m_curTab) {
        return false;
    }

    Q_ASSERT(m_curFile);

    if (!m_curFile->isModifiable()
        || m_curFile->getDocType() != DocType::Markdown) {
        return false;
    }

    return true;
}

// Popup the attachment list if it is enabled.
bool VMainWindow::showAttachmentListByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VMainWindow *obj = static_cast<VMainWindow *>(p_target);
    if (obj->m_attachmentBtn->isEnabled()) {
        // Show tool bar first.
        bool toolBarChecked = obj->m_toolBarAct->isChecked();
        if (!toolBarChecked) {
            obj->setToolBarVisible(true);

            // Make it visible first.
            QCoreApplication::sendPostedEvents();
        }

        obj->m_attachmentBtn->showPopupWidget();

        if (!toolBarChecked) {
            obj->setToolBarVisible(false);
        }
    }

    return true;
}

bool VMainWindow::locateCurrentFileByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VMainWindow *obj = static_cast<VMainWindow *>(p_target);
    if (obj->m_curFile) {
        if (obj->locateFile(obj->m_curFile)) {
            return false;
        }
    }

    return true;
}

bool VMainWindow::toggleExpandModeByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VMainWindow *obj = static_cast<VMainWindow *>(p_target);
    obj->expandViewAct->trigger();
    return true;
}

bool VMainWindow::currentNoteInfoByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VMainWindow *obj = static_cast<VMainWindow *>(p_target);
    if (obj->noteInfoAct->isEnabled()) {
        obj->curEditFileInfo();
    }

    return true;
}

bool VMainWindow::discardAndReadByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VMainWindow *obj = static_cast<VMainWindow *>(p_target);
    if (obj->m_curTab) {
        obj->m_discardExitAct->trigger();
        obj->m_curTab->setFocus();

        return false;
    }

    return true;
}

bool VMainWindow::toggleToolBarByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VMainWindow *obj = static_cast<VMainWindow *>(p_target);
    obj->m_toolBarAct->trigger();
    return true;
}

bool VMainWindow::toggleToolsDockByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VMainWindow *obj = static_cast<VMainWindow *>(p_target);
    obj->m_toolDock->setVisible(!obj->m_toolDock->isVisible());
    return true;
}

bool VMainWindow::toggleSearchDockByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VMainWindow *obj = static_cast<VMainWindow *>(p_target);
    bool visible = obj->m_searchDock->isVisible();
    obj->m_searchDock->setVisible(!visible);
    if (!visible) {
        obj->m_searcher->focusToSearch();
        return false;
    }

    return true;
}

bool VMainWindow::closeFileByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VMainWindow *obj = static_cast<VMainWindow *>(p_target);
    obj->closeCurrentFile();

    QWidget *nextFocus = obj->m_editArea->getCurrentTab();
    if (nextFocus) {
        nextFocus->setFocus();
    } else {
        obj->m_fileList->setFocus();
    }

    return false;
}

bool VMainWindow::shortcutsHelpByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);
    VMainWindow *obj = static_cast<VMainWindow *>(p_target);
    obj->shortcutsHelp();
    return false;
}

bool VMainWindow::flushLogFileByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_target);
    Q_UNUSED(p_data);

#if defined(QT_NO_DEBUG)
    // Flush g_logFile.
    g_logFile.flush();
#endif

    return true;
}

bool VMainWindow::exportByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);

    VMainWindow *obj = static_cast<VMainWindow *>(p_target);
    QTimer::singleShot(50, obj, SLOT(handleExportAct()));

    return true;
}

bool VMainWindow::focusEditAreaByCaptain(void *p_target, void *p_data)
{
    Q_UNUSED(p_data);

    VMainWindow *obj = static_cast<VMainWindow *>(p_target);
    obj->focusEditArea();
    return false;
}

void VMainWindow::promptNewNotebookIfEmpty()
{
    if (vnote->getNotebooks().isEmpty()) {
        m_notebookSelector->newNotebook();
    }
}

void VMainWindow::initShortcuts()
{
    QString keySeq = g_config->getShortcutKeySequence("CloseNote");
    qDebug() << "set CloseNote shortcut to" << keySeq;
    if (!keySeq.isEmpty()) {
        QShortcut *closeNoteShortcut = new QShortcut(QKeySequence(keySeq), this);
        closeNoteShortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(closeNoteShortcut, &QShortcut::activated,
                this, &VMainWindow::closeCurrentFile);
    }
}

void VMainWindow::openFlashPage()
{
    openFiles(QStringList() << g_config->getFlashPage(),
              false,
              OpenFileMode::Edit,
              true);
}

void VMainWindow::openQuickAccess()
{
    const QString &qaPath = g_config->getQuickAccess();
    if (qaPath.isEmpty()) {
        VUtils::showMessage(QMessageBox::Information,
                            tr("Information"),
                            tr("Quick Access is not set."),
                            tr("Please specify the note for Quick Access in the settings dialog "
                               "or the context menu of a note."),
                            QMessageBox::Ok,
                            QMessageBox::Ok,
                            this);
        return;
    }

    openFiles(QStringList(qaPath), false, g_config->getNoteOpenMode());
}

void VMainWindow::initHeadingButton(QToolBar *p_tb)
{
    m_headingBtn = new QPushButton(VIconUtils::toolButtonIcon(":/resources/icons/heading.svg"),
                                   "",
                                   this);
    m_headingBtn->setToolTip(tr("Headings"));
    m_headingBtn->setProperty("CornerBtn", true);
    m_headingBtn->setFocusPolicy(Qt::NoFocus);
    m_headingBtn->setEnabled(false);

    QMenu *menu = new QMenu(this);
    QString text(tr("Heading %1"));
    QString tooltip(tr("Heading %1\t%2"));
    QWidgetAction *wact = new QWidgetAction(menu);
    wact->setData(1);
    VButtonMenuItem *w = new VButtonMenuItem(wact, text.arg(1), this);
    w->setToolTip(tooltip.arg(1).arg(VUtils::getShortcutText("Ctrl+1")));
    w->setProperty("Heading1", true);
    wact->setDefaultWidget(w);
    menu->addAction(wact);

    wact = new QWidgetAction(menu);
    wact->setData(2);
    w = new VButtonMenuItem(wact, text.arg(2), this);
    w->setToolTip(tooltip.arg(2).arg(VUtils::getShortcutText("Ctrl+2")));
    w->setProperty("Heading2", true);
    wact->setDefaultWidget(w);
    menu->addAction(wact);

    wact = new QWidgetAction(menu);
    wact->setData(3);
    w = new VButtonMenuItem(wact, text.arg(3), this);
    w->setToolTip(tooltip.arg(3).arg(VUtils::getShortcutText("Ctrl+3")));
    w->setProperty("Heading3", true);
    wact->setDefaultWidget(w);
    menu->addAction(wact);

    wact = new QWidgetAction(menu);
    wact->setData(4);
    w = new VButtonMenuItem(wact, text.arg(4), this);
    w->setToolTip(tooltip.arg(4).arg(VUtils::getShortcutText("Ctrl+4")));
    w->setProperty("Heading4", true);
    wact->setDefaultWidget(w);
    menu->addAction(wact);

    wact = new QWidgetAction(menu);
    wact->setData(5);
    w = new VButtonMenuItem(wact, text.arg(5), this);
    w->setToolTip(tooltip.arg(5).arg(VUtils::getShortcutText("Ctrl+5")));
    w->setProperty("Heading5", true);
    wact->setDefaultWidget(w);
    menu->addAction(wact);

    wact = new QWidgetAction(menu);
    wact->setData(6);
    w = new VButtonMenuItem(wact, text.arg(6), this);
    w->setToolTip(tooltip.arg(6).arg(VUtils::getShortcutText("Ctrl+6")));
    w->setProperty("Heading6", true);
    wact->setDefaultWidget(w);
    menu->addAction(wact);

    wact = new QWidgetAction(menu);
    wact->setData(0);
    w = new VButtonMenuItem(wact, tr("Clear"), this);
    w->setToolTip(tr("Clear\t%1").arg(VUtils::getShortcutText("Ctrl+7")));
    wact->setDefaultWidget(w);
    menu->addAction(wact);

    connect(menu, &QMenu::triggered,
            this, [this, menu](QAction *p_action) {
                if (m_curTab) {
                    int level = p_action->data().toInt();
                    m_curTab->decorateText(TextDecoration::Heading, level);
                }

                menu->hide();
            });

    m_headingBtn->setMenu(menu);
    p_tb->addWidget(m_headingBtn);
}

void VMainWindow::initThemeMenu(QMenu *p_menu)
{
    QMenu *themeMenu = p_menu->addMenu(tr("Theme"));
    themeMenu->setToolTipsVisible(true);

    QAction *addAct = newAction(VIconUtils::menuIcon(":/resources/icons/add_style.svg"),
                                tr("Add Theme"),
                                themeMenu);
    addAct->setToolTip(tr("Add custom theme"));
    connect(addAct, &QAction::triggered,
            this, [this]() {
                VTipsDialog dialog(VUtils::getDocFile("tips_add_theme.md"),
                                   tr("Add Theme"),
                                   []() {
                                       QUrl url = QUrl::fromLocalFile(g_config->getThemeConfigFolder());
                                       QDesktopServices::openUrl(url);
                                   },
                                   this);
                dialog.exec();
            });

    themeMenu->addAction(addAct);

    QActionGroup *ag = new QActionGroup(this);
    connect(ag, &QActionGroup::triggered,
            this, [this](QAction *p_action) {
                QString data = p_action->data().toString();
                g_config->setTheme(data);

                promptForVNoteRestart();
            });

    QList<QString> themes = g_config->getThemes();
    QString theme = g_config->getTheme();
    for (auto const &item : themes) {
        QAction *act = new QAction(item, ag);
        act->setToolTip(tr("Set as the theme of VNote (restart VNote to make it work)"));
        act->setCheckable(true);
        act->setData(item);

        // Add it to the menu.
        themeMenu->addAction(act);

        if (theme == item) {
            act->setChecked(true);
        }
    }
}

void VMainWindow::customShortcut()
{
    VTipsDialog dialog(VUtils::getDocFile("tips_custom_shortcut.md"),
                       tr("Customize Shortcuts"),
                       []() {
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
                           // On macOS, it seems that we could not open that ini file directly.
                           QUrl url = QUrl::fromLocalFile(g_config->getConfigFolder());
#else
                           QUrl url = QUrl::fromLocalFile(g_config->getConfigFilePath());
#endif
                           QDesktopServices::openUrl(url);
                       },
                       this);
    dialog.exec();
}

void VMainWindow::initVimCmd()
{
    m_vimCmd = new VVimCmdLineEdit(this);
    m_vimCmd->setProperty("VimCommandLine", true);

    connect(m_vimCmd, &VVimCmdLineEdit::commandCancelled,
            this, [this]() {
                if (m_curTab) {
                    m_curTab->focusTab();
                }

                // NOTICE: should not hide before setting focus to edit tab.
                m_vimCmd->hide();

                if (m_curTab) {
                    m_curTab->handleVimCmdCommandCancelled();
                }
            });

    connect(m_vimCmd, &VVimCmdLineEdit::commandFinished,
            this, [this](VVim::CommandLineType p_type, const QString &p_cmd) {
                if (m_curTab) {
                    m_curTab->focusTab();
                }

                m_vimCmd->hide();

                // Hide the cmd line edit before execute the command.
                // If we execute the command first, we will get Chinese input
                // method enabled after returning to read mode.
                if (m_curTab) {
                    m_curTab->handleVimCmdCommandFinished(p_type, p_cmd);
                }
            });

    connect(m_vimCmd, &VVimCmdLineEdit::commandChanged,
            this, [this](VVim::CommandLineType p_type, const QString &p_cmd) {
                if (m_curTab) {
                    m_curTab->handleVimCmdCommandChanged(p_type, p_cmd);
                }
            });

    connect(m_vimCmd, &VVimCmdLineEdit::requestNextCommand,
            this, [this](VVim::CommandLineType p_type, const QString &p_cmd) {
                if (m_curTab) {
                    QString cmd = m_curTab->handleVimCmdRequestNextCommand(p_type, p_cmd);
                    if (!cmd.isNull()) {
                        m_vimCmd->setCommand(cmd);
                    } else {
                        m_vimCmd->restoreUserLastInput();
                    }
                }
            });

    connect(m_vimCmd, &VVimCmdLineEdit::requestPreviousCommand,
            this, [this](VVim::CommandLineType p_type, const QString &p_cmd) {
                if (m_curTab) {
                    QString cmd = m_curTab->handleVimCmdRequestPreviousCommand(p_type, p_cmd);
                    if (!cmd.isNull()) {
                        m_vimCmd->setCommand(cmd);
                    }
                }
            });

    connect(m_vimCmd, &VVimCmdLineEdit::requestRegister,
            this, [this](int p_key, int p_modifiers){
                if (m_curTab) {
                    QString val = m_curTab->handleVimCmdRequestRegister(p_key, p_modifiers);
                    if (!val.isEmpty()) {
                        m_vimCmd->setText(m_vimCmd->text() + val);
                    }
                }
            });

    m_vimCmd->hide();
}

void VMainWindow::toggleEditReadMode()
{
    if (!m_curTab) {
        return;
    }

    if (m_curTab->isEditMode()) {
        // Save changes and read.
        m_editArea->saveAndReadFile();
    } else {
        // Edit.
        m_editArea->editFile();
    }
}

void VMainWindow::updateEditReadAct(const VEditTab *p_tab)
{
    static QIcon editIcon = VIconUtils::toolButtonIcon(":/resources/icons/edit_note.svg");
    static QString editText;
    static QIcon readIcon = VIconUtils::toolButtonIcon(":/resources/icons/save_exit.svg");
    static QString readText;

    if (editText.isEmpty()) {
        QString keySeq = g_config->getShortcutKeySequence("EditReadNote");
        QKeySequence seq(keySeq);
        if (!seq.isEmpty()) {
            QString shortcutText = VUtils::getShortcutText(keySeq);
            editText = tr("Edit\t%1").arg(shortcutText);
            readText = tr("Save Changes And Read\t%1").arg(shortcutText);

            m_editReadAct->setShortcut(seq);
        } else {
            editText = tr("Edit");
            readText = tr("Save Changes And Read");
        }
    }

    if (!p_tab || !p_tab->isEditMode()) {
        // Edit.
        m_editReadAct->setIcon(editIcon);
        m_editReadAct->setText(editText);
        m_editReadAct->setStatusTip(tr("Edit current note"));

        m_discardExitAct->setEnabled(false);
    } else {
        // Read.
        m_editReadAct->setIcon(readIcon);
        m_editReadAct->setText(readText);
        m_editReadAct->setStatusTip(tr("Save changes and exit edit mode"));

        m_discardExitAct->setEnabled(true);
    }

    m_editReadAct->setEnabled(p_tab);
}

void VMainWindow::handleExportAct()
{
    VExportDialog dialog(m_notebookSelector->currentNotebook(),
                         m_dirTree->currentDirectory(),
                         m_curFile,
                         m_cart,
                         g_config->getMdConverterType(),
                         this);
    dialog.exec();
}

VNotebook *VMainWindow::getCurrentNotebook() const
{
    return m_notebookSelector->currentNotebook();
}

void VMainWindow::activateUniversalEntry()
{
    if (!m_ue) {
        initUniversalEntry();
    }

    m_captain->setCaptainModeEnabled(false);

    // Move it to the top left corner of edit area.
    QPoint topLeft = m_editArea->mapToGlobal(QPoint(0, 0));
    QRect eaRect = m_editArea->editAreaRect();
    topLeft += eaRect.topLeft();

    // Use global position.
    m_ue->move(topLeft);

    eaRect.moveTop(0);
    m_ue->setAvailableRect(eaRect);

    m_ue->show();
    m_ue->raise();
}

void VMainWindow::initUniversalEntry()
{
    m_ue = new VUniversalEntry(this);
    m_ue->hide();

#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    // Qt::Popup on macOS does not work well with input method.
    m_ue->setWindowFlags(Qt::Tool
                         | Qt::NoDropShadowWindowHint);
    m_ue->setWindowModality(Qt::ApplicationModal);
#else
    m_ue->setWindowFlags(Qt::Popup
                         | Qt::FramelessWindowHint
                         | Qt::NoDropShadowWindowHint);
#endif

    connect(m_ue, &VUniversalEntry::exited,
            this, [this]() {
                m_captain->setCaptainModeEnabled(true);
            });

    // Register entries.
    VSearchUE *searchUE = new VSearchUE(this);
    m_ue->registerEntry('q', searchUE, VSearchUE::Name_FolderNote_AllNotebook);
    m_ue->registerEntry('a', searchUE, VSearchUE::Content_Note_AllNotebook);
    m_ue->registerEntry('z', searchUE, VSearchUE::Tag_Note_AllNotebook);
    m_ue->registerEntry('w', searchUE, VSearchUE::Name_Notebook_AllNotebook);
    m_ue->registerEntry('e', searchUE, VSearchUE::Name_FolderNote_CurrentNotebook);
    m_ue->registerEntry('d', searchUE, VSearchUE::Content_Note_CurrentNotebook);
    m_ue->registerEntry('c', searchUE, VSearchUE::Tag_Note_CurrentNotebook);
    m_ue->registerEntry('r', searchUE, VSearchUE::Name_FolderNote_CurrentFolder);
    m_ue->registerEntry('f', searchUE, VSearchUE::Content_Note_CurrentFolder);
    m_ue->registerEntry('v', searchUE, VSearchUE::Tag_Note_CurrentFolder);
    m_ue->registerEntry('t', searchUE, VSearchUE::Name_Note_Buffer);
    m_ue->registerEntry('g', searchUE, VSearchUE::Content_Note_Buffer);
    m_ue->registerEntry('b', searchUE, VSearchUE::Outline_Note_Buffer);
    m_ue->registerEntry('u', searchUE, VSearchUE::Content_Note_ExplorerDirectory);
    m_ue->registerEntry('y', new VOutlineUE(this), 0);
    m_ue->registerEntry('h', searchUE, VSearchUE::Path_FolderNote_AllNotebook);
    m_ue->registerEntry('n', searchUE, VSearchUE::Path_FolderNote_CurrentNotebook);
    m_ue->registerEntry('m', new VListFolderUE(this), 0);
    m_ue->registerEntry('j', new VListUE(this), VListUE::History);
    m_ue->registerEntry('?', new VHelpUE(this), 0);
}

void VMainWindow::checkNotebooks()
{
    bool updated = false;
    QVector<VNotebook *> &notebooks = g_vnote->getNotebooks();
    for (int i = 0; i < notebooks.size(); ++i) {
        VNotebook *nb = notebooks[i];
        if (nb->isValid()) {
            continue;
        }

        VFixNotebookDialog dialog(nb, notebooks, this);
        if (dialog.exec()) {
            qDebug() << "fix path of notebook" << nb->getName() << "to" << dialog.getPathInput();
            nb->updatePath(dialog.getPathInput());
        } else {
            notebooks.removeOne(nb);
            --i;
        }

        updated = true;
    }

    if (updated) {
        g_config->setNotebooks(notebooks);

        m_notebookSelector->update();
    }

    m_notebookSelector->restoreCurrentNotebook();
}

void VMainWindow::setMenuBarVisible(bool p_visible)
{
    // Hiding the menubar will disable the shortcut of QActions.
    if (p_visible) {
        menuBar()->setFixedSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
    } else {
        menuBar()->setFixedHeight(0);
    }
}

void VMainWindow::setToolBarVisible(bool p_visible)
{
    for (auto bar : m_toolBars) {
        if (p_visible) {
            bar->setFixedSize(QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        } else {
            bar->setFixedHeight(0);
        }
    }
}

void VMainWindow::kickOffStartUpTimer(const QStringList &p_files)
{
    QTimer::singleShot(300, [this, p_files]() {
        m_syncNoteListToCurrentTab = false;

        checkNotebooks();
        QCoreApplication::sendPostedEvents();
        promptNewNotebookIfEmpty();
        QCoreApplication::sendPostedEvents();
        openStartupPages();
        openFiles(p_files, false, g_config->getNoteOpenMode(), false, true);

        checkIfNeedToShowWelcomePage();

        if (g_config->versionChanged() && !g_config->getAllowUserTrack()) {
            // Ask user whether allow tracking.
            int ret = VUtils::showMessage(QMessageBox::Information,
                                          tr("Collect User Statistics"),
                                          tr("VNote would like to send a request to count active users."
                                             "Do you allow this request?"),
                                          tr("A request to https://tamlok.github.io/user_track/vnote.html will be sent if allowed."),
                                          QMessageBox::Ok | QMessageBox::No,
                                          QMessageBox::Ok,
                                          this);
            g_config->setAllowUserTrack(ret == QMessageBox::Ok);
        }

        if (g_config->getAllowUserTrack()) {
            int interval = (30 + qrand() % 60) * 1000;
            QTimer::singleShot(interval, this, SLOT(collectUserStat()));
        }

        m_syncNoteListToCurrentTab = true;

#if defined(Q_OS_WIN)
        if (g_config->isFreshInstall()) {
            VUtils::showMessage(QMessageBox::Information,
                                tr("Notices for Windows Users"),
                                tr("OpenGL requried by VNote may not work well on Windows by default."
                                   "You may update your display card driver or set another openGL option in VNote's Settings dialog."
                                   "Check <a href=\"https://github.com/tamlok/vnote/issues/853\">GitHub issue</a> for details."),
                                tr("Strange behaviors include:<br/>"
                                   "* Interface freezes and does not response;<br/>"
                                   "* Widgets are out of order after maximizing and restoring the main window;<br/>"
                                   "* No cursor in edit mode;<br/>"
                                   "* Menus are not clickable in full screen mode."),
                                QMessageBox::Ok,
                                QMessageBox::Ok,
                                this);
        }
#endif

        g_config->updateLastStartDateTime();
    });
}

void VMainWindow::showNotebookPanel()
{
    changePanelView(PanelViewState::VerticalMode);
    m_naviBox->setCurrentIndex(NaviBoxIndex::NotebookPanel, false);
}

void VMainWindow::showExplorerPanel(bool p_focus)
{
    changePanelView(PanelViewState::VerticalMode);
    m_naviBox->setCurrentIndex(NaviBoxIndex::Explorer, p_focus);
}

void VMainWindow::stayOnTop(bool p_enabled)
{
    bool shown = isVisible();
    Qt::WindowFlags flags = this->windowFlags();

    Qt::WindowFlags magicFlag = Qt::WindowStaysOnTopHint;
    if (p_enabled) {
        setWindowFlags(flags | magicFlag);
    } else {
        setWindowFlags(flags ^ magicFlag);
    }

    if (shown) {
        show();
    }
}

void VMainWindow::focusEditArea() const
{
    QWidget *widget = m_editArea->getCurrentTab();
    if (!widget) {
        widget = m_editArea;
    }

    widget->setFocus();
}

void VMainWindow::setCaptainModeEnabled(bool p_enabled)
{
    m_captain->setCaptainModeEnabled(p_enabled);
}

void VMainWindow::initUpdateTimer()
{
    m_updateTimer = new QTimer(this);
    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(200);
    connect(m_updateTimer, &QTimer::timeout,
            this, [this]() {
                m_fileToolBar->update();
                m_viewToolBar->update();
                m_editToolBar->update();
                m_noteToolBar->update();
            });
}

void VMainWindow::restartVNote()
{
    QCoreApplication::exit(RESTART_EXIT_CODE);
}

void VMainWindow::updateFontOfAllTabs()
{
    QVector<VEditTab *> tabs = m_editArea->getAllTabs();
    for (auto tab : tabs) {
        const VMdTab *mdTab = dynamic_cast<const VMdTab *>(tab);
        if (mdTab && mdTab->getEditor()) {
            mdTab->getEditor()->updateFontAndPalette();
        }
    }
}

void VMainWindow::splitFileListOut(bool p_enabled)
{
    showNotebookPanel();

    g_config->setEnableSplitFileList(p_enabled);

    setupFileListSplitOut(p_enabled);

    g_config->setNotebookSplitterState(m_nbSplitter->saveState());
}

void VMainWindow::setupFileListSplitOut(bool p_enabled)
{
    m_fileList->setEnableSplitOut(p_enabled);
    if (p_enabled) {
        m_nbSplitter->setOrientation(Qt::Horizontal);
        m_nbSplitter->setStretchFactor(0, 0);
        m_nbSplitter->setStretchFactor(1, 1);
    } else {
        m_nbSplitter->setOrientation(Qt::Vertical);
        m_nbSplitter->setStretchFactor(0, 1);
        m_nbSplitter->setStretchFactor(1, 2);
    }
}

void VMainWindow::collectUserStat() const
{
    // One request per day.
    auto lastCheckDate = g_config->getLastUserTrackDate();
    if (lastCheckDate != QDate::currentDate()) {
        g_config->updateLastUserTrackDate();

        qDebug() << "send user track" << QDate::currentDate();

        QWebEnginePage *page = new QWebEnginePage;

        QString url = QString("https://tamlok.github.io/user_track/vnote/vnote_%1.html").arg(VConfigManager::c_version);
        page->load(QUrl(url));
        connect(page, &QWebEnginePage::loadFinished,
                this, [page](bool) {
                    VUtils::sleepWait(2000);
                    page->deleteLater();
                });
    }

    QTimer::singleShot(30 * 60 * 1000, this, SLOT(collectUserStat()));
}

void VMainWindow::promptForVNoteRestart()
{
    int ret = VUtils::showMessage(QMessageBox::Information,
                                  tr("Restart Needed"),
                                  tr("Do you want to restart VNote now?"),
                                  tr("VNote needs to restart to apply new configurations."),
                                  QMessageBox::Ok | QMessageBox::No,
                                  QMessageBox::Ok,
                                  this);
    if (ret == QMessageBox::Ok) {
        restartVNote();
    }
}

void VMainWindow::checkIfNeedToShowWelcomePage()
{
    if (g_config->versionChanged()
        || (QDate::currentDate().dayOfYear() % 64 == 1
            && g_config->getLastStartDateTime().date() != QDate::currentDate())) {
        QString docFile = VUtils::getDocFile("welcome.md");
        VFile *file = vnote->getFile(docFile, true);
        m_editArea->openFile(file, OpenFileMode::Read);
    }
}
