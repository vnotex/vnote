#include <QtWidgets>
#include "vmainwindow.h"
#include "vdirectorytree.h"
#include "vnote.h"
#include "vfilelist.h"
#include "vtabwidget.h"
#include "vconfigmanager.h"

extern VConfigManager vconfig;

VMainWindow::VMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    vnote = new VNote();
    setupUI();
    initActions();
    initToolBar();
    updateNotebookComboBox();
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
    notebookComboBox = new QComboBox();
    notebookComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    directoryTree = new VDirectoryTree();

    QVBoxLayout *nbLayout = new QVBoxLayout;
    nbLayout->addWidget(notebookLabel);
    nbLayout->addWidget(notebookComboBox);
    nbLayout->addWidget(directoryLabel);
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
    connect(saveNoteAct, &QAction::triggered,
            tabs, &VTabWidget::saveFile);
}

void VMainWindow::initToolBar()
{
    fileToolBar = addToolBar(tr("Note"));
    fileToolBar->setMovable(false);
    fileToolBar->addAction(editNoteAct);
    fileToolBar->addAction(readNoteAct);
    fileToolBar->addAction(saveNoteAct);
}

void VMainWindow::updateNotebookComboBox()
{
    const QVector<VNotebook> &notebooks = vnote->getNotebooks();

    notebookComboBox->clear();
    if (notebooks.isEmpty()) {
        return;
    }
    for (int i = 0; i <notebooks.size(); ++i) {
        notebookComboBox->addItem(notebooks[i].getName());
    }

    qDebug() << "update notebook combobox with" << notebookComboBox->count()
             << "items";
    notebookComboBox->setCurrentIndex(vconfig.getCurNotebookIndex());
}

void VMainWindow::setCurNotebookIndex(int index)
{
    Q_ASSERT(index < vnote->getNotebooks().size());
    qDebug() << "set current notebook index:" << index;
    vconfig.setCurNotebookIndex(index);

    // Update directoryTree
    QString treePath;
    if (index > -1) {
        treePath = vnote->getNotebooks()[index].getPath();
    }
    emit curNotebookIndexChanged(treePath);
}
