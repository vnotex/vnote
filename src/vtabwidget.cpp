#include <QtWidgets>
#include <QtDebug>
#include "vtabwidget.h"
#include "veditor.h"
#include "vnote.h"
#include "vconfigmanager.h"

extern VConfigManager vconfig;

VTabWidget::VTabWidget(VNote *vnote, QWidget *parent)
    : QTabWidget(parent), vnote(vnote)
{
    setTabsClosable(true);
    setMovable(true);
    connect(tabBar(), &QTabBar::tabCloseRequested,
            this, &VTabWidget::handleTabCloseRequest);

    openWelcomePage();
}

void VTabWidget::openWelcomePage()
{
    int idx = openFileInTab("", vconfig.getWelcomePagePath(), false);
    setTabText(idx, "Welcome to VNote");
    setTabToolTip(idx, "VNote");
}

int VTabWidget::insertTabWithData(int index, QWidget *page,
                                  const QJsonObject &tabData)
{
    QString label = getFileName(tabData["relative_path"].toString());
    int idx = insertTab(index, page, label);
    QTabBar *tabs = tabBar();
    tabs->setTabData(idx, tabData);

    return idx;
}

int VTabWidget::appendTabWithData(QWidget *page, const QJsonObject &tabData)
{
    return insertTabWithData(count(), page, tabData);
}

void VTabWidget::openFile(QJsonObject fileJson)
{
    if (fileJson.isEmpty()) {
        return;
    }

    QString notebook = fileJson["notebook"].toString();
    QString relativePath = fileJson["relative_path"].toString();
    int mode = OpenFileMode::Read;
    if (fileJson.contains("mode")) {
        mode = fileJson["mode"].toInt();
    }

    qDebug() << "open notebook" << notebook << "path" << relativePath << mode;

    // Find if it has been opened already
    int idx = findTabByFile(notebook, relativePath);
    if (idx > -1) {
        goto out;
    }

    idx = openFileInTab(notebook, relativePath, true);

out:
    setCurrentIndex(idx);
    if (mode == OpenFileMode::Edit) {
        VEditor *editor = dynamic_cast<VEditor *>(currentWidget());
        Q_ASSERT(editor);
        editor->editFile();
    }
}

void VTabWidget::closeFile(QJsonObject fileJson)
{
    if (fileJson.isEmpty()) {
        return;
    }
    qDebug() << "close file:" << fileJson;

    QString notebook = fileJson["notebook"].toString();
    QString relativePath = fileJson["relative_path"].toString();

    // Find if it has been opened already
    int idx = findTabByFile(notebook, relativePath);
    if (idx == -1) {
        return;
    }

    QWidget* page = widget(idx);
    Q_ASSERT(page);
    removeTab(idx);
    delete page;
}

int VTabWidget::openFileInTab(const QString &notebook, const QString &relativePath,
                              bool modifiable)
{
    QString rootPath;
    const QVector<VNotebook> &notebooks = vnote->getNotebooks();
    for (int i = 0; i < notebooks.size(); ++i) {
        if (notebooks[i].getName() == notebook) {
            rootPath = notebooks[i].getPath();
            break;
        }
    }

    VEditor *editor = new VEditor(QDir::cleanPath(QDir(rootPath).filePath(relativePath)),
                                  modifiable);
    QJsonObject tabJson;
    tabJson["notebook"] = notebook;
    tabJson["relative_path"] = relativePath;
    return appendTabWithData(editor, tabJson);
}

int VTabWidget::findTabByFile(const QString &notebook, const QString &relativePath) const
{
    QTabBar *tabs = tabBar();
    int nrTabs = tabs->count();

    for (int i = 0; i < nrTabs; ++i) {
        QJsonObject tabJson = tabs->tabData(i).toJsonObject();
        if (tabJson["notebook"] == notebook && tabJson["relative_path"] == relativePath) {
            return i;
        }
    }
    return -1;
}

void VTabWidget::handleTabCloseRequest(int index)
{
    qDebug() << "request closing tab" << index;
    VEditor *editor = dynamic_cast<VEditor *>(widget(index));
    Q_ASSERT(editor);
    bool ok = editor->requestClose();
    if (ok) {
        removeTab(index);
        delete editor;
    }
}

void VTabWidget::readFile()
{
    VEditor *editor = dynamic_cast<VEditor *>(currentWidget());
    Q_ASSERT(editor);
    editor->readFile();
}

void VTabWidget::editFile()
{
    VEditor *editor = dynamic_cast<VEditor *>(currentWidget());
    Q_ASSERT(editor);
    editor->editFile();
}

void VTabWidget::saveFile()
{
    VEditor *editor = dynamic_cast<VEditor *>(currentWidget());
    Q_ASSERT(editor);
    editor->saveFile();
}

void VTabWidget::handleNotebookRenamed(const QVector<VNotebook> &notebooks,
                                       const QString &oldName, const QString &newName)
{
    QTabBar *tabs = tabBar();
    int nrTabs = tabs->count();
    for (int i = 0; i < nrTabs; ++i) {
        QJsonObject tabJson = tabs->tabData(i).toJsonObject();
        if (tabJson["notebook"] == oldName) {
            tabJson["notebook"] = newName;
            tabs->setTabData(i, tabJson);
        }
    }
}
