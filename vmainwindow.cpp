#include <QtGui>
#include "vmainwindow.h"
#include "vdirectorytree.h"

VMainWindow::VMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUI();
}

VMainWindow::~VMainWindow()
{

}

void VMainWindow::setupUI()
{
    // Notebook directory browser tree
    notebookLabel = new QLabel(tr("&Notebook"));
    notebookComboBox = new QComboBox();
    notebookLabel->setBuddy(notebookComboBox);
    directoryTree = new VDirectoryTree();

    QHBoxLayout *nbTopLayout = new QHBoxLayout;
    notebookComboBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
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
    fileListWidget = new QListWidget();
    fileListWidget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Expanding);

    // Editor tab widget
    editorTabWidget = new QTabWidget();
    editorTabWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
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
    mainSplitter->addWidget(fileListWidget);
    mainSplitter->addWidget(editorTabWidget);
    mainSplitter->setStretchFactor(0, 1);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setStretchFactor(2, 10);

    setCentralWidget(mainSplitter);
}
