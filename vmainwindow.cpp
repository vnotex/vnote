#include <QtWidgets>
#include "vmainwindow.h"
#include "vdirectorytree.h"
#include "vnote.h"
#include "vfilelist.h"

VMainWindow::VMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUI();

    vnote = new VNote();
    vnote->readGlobalConfig();
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
    notebookComboBox = new QComboBox();
    directoryTree = new VDirectoryTree();

    QHBoxLayout *nbTopLayout = new QHBoxLayout;
    notebookComboBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    notebookComboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    nbTopLayout->setAlignment(Qt::AlignLeft);
    nbTopLayout->addWidget(notebookLabel);
    nbTopLayout->addWidget(notebookComboBox);

    QVBoxLayout *nbLayout = new QVBoxLayout;
    nbLayout->addLayout(nbTopLayout);
    nbLayout->addWidget(directoryTree);
    QWidget *nbContainer = new QWidget();
    nbContainer->setLayout(nbLayout);
    nbContainer->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);

    // File list widget
    fileList = new VFileList();
    fileList->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);

    // Editor tab widget
    editorTabWidget = new QTabWidget();
    editorTabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    editorTabWidget->setTabBarAutoHide(true);
    QFile welcomeFile(":/resources/welcome.html");
    QString welcomeText("Welcome to VNote!");
    if (welcomeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        welcomeText = QString(welcomeFile.readAll());
        welcomeFile.close();
    }
    QTextBrowser *welcomePage = new QTextBrowser();
    welcomePage->setHtml(welcomeText);
    editorTabWidget->addTab(welcomePage, tr("Welcome to VNote"));

    // Main Splitter
    mainSplitter = new QSplitter();
    mainSplitter->addWidget(nbContainer);
    mainSplitter->addWidget(fileList);
    mainSplitter->addWidget(editorTabWidget);
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

    setCentralWidget(mainSplitter);
    // Create and show the status bar
    statusBar();
}

void VMainWindow::updateNotebookComboBox()
{
    const QVector<VNotebook> &notebooks = vnote->getNotebooks();

    notebookComboBox->clear();
    for (int i = 0; i <notebooks.size(); ++i) {
        notebookComboBox->addItem(notebooks[i].getName());
    }

    qDebug() << "update notebook combobox with" << notebookComboBox->count()
             << "items";
    notebookComboBox->setCurrentIndex(vnote->getCurNotebookIndex());
}

void VMainWindow::setCurNotebookIndex(int index)
{
    Q_ASSERT(index < vnote->getNotebooks().size());
    qDebug() << "set current notebook index:" << index;
    vnote->setCurNotebookIndex(index);
    notebookComboBox->setCurrentIndex(index);

    // Update directoryTree
    emit curNotebookIndexChanged(vnote->getNotebooks()[index].getPath());
}
