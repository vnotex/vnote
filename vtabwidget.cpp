#include <QtWidgets>
#include <QtDebug>
#include "vtabwidget.h"
#include "veditor.h"
#include "vnote.h"

VTabWidget::VTabWidget(QWidget *parent)
    : QTabWidget(parent)
{
    setTabsClosable(true);
    connect(tabBar(), &QTabBar::tabCloseRequested,
            this, &VTabWidget::handleTabCloseRequest);

    openWelcomePage();
}

void VTabWidget::openWelcomePage()
{
    int idx = openFileInTab(VNote::welcomePagePath, "", false);
    setTabText(idx, "Welcome to VNote");
    setTabToolTip(idx, "VNote");
}

int VTabWidget::insertTabWithData(int index, QWidget *page, const QString &label,
                                  const QJsonObject &tabData)
{
    int idx = insertTab(index, page, label);
    QTabBar *tabs = tabBar();
    tabs->setTabData(idx, tabData);
    Q_ASSERT(tabs->tabText(idx) == label);
    return idx;
}

int VTabWidget::appendTabWithData(QWidget *page, const QString &label, const QJsonObject &tabData)
{
    return insertTabWithData(count(), page, label, tabData);
}

void VTabWidget::openFile(QJsonObject fileJson)
{
    if (fileJson.isEmpty()) {
        return;
    }
    qDebug() << "open file:" << fileJson;

    QString path = fileJson["path"].toString();
    QString name = fileJson["name"].toString();

    // Find if it has been opened already
    int idx = findTabByFile(path, name);
    if (idx > -1) {
        setCurrentIndex(idx);
        return;
    }

    idx = openFileInTab(path, name, true);

    setCurrentIndex(idx);
}

int VTabWidget::openFileInTab(const QString &path, const QString &name, bool modifiable)
{
    VEditor *editor = new VEditor(path, name, modifiable);
    QJsonObject tabJson;
    tabJson["path"] = path;
    tabJson["name"] = name;
    int idx = appendTabWithData(editor, name, tabJson);
    setTabToolTip(idx, path);
    return idx;
}

int VTabWidget::findTabByFile(const QString &path, const QString &name)
{
    QTabBar *tabs = tabBar();
    int nrTabs = tabs->count();

    for (int i = 0; i < nrTabs; ++i) {
        QJsonObject tabJson = tabs->tabData(i).toJsonObject();
        if (tabJson["name"] == name && tabJson["path"] == path) {
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
