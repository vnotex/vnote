#include <QSettings>
#include <QDebug>
#include "vnote.h"

const QString VNote::orgName = QString("tamlok");
const QString VNote::appName = QString("VNote");
const QString VNote::welcomePageUrl = QString(":/resources/welcome.html");

VNote::VNote()
    : curNotebookIndex(0)
{
}

void VNote::readGlobalConfig()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       orgName, appName);

    // [global] section
    settings.beginGroup("global");
    curNotebookIndex = settings.value("current_notebook", 0).toInt();
    qDebug() << "read current_notebook=" << curNotebookIndex;
    settings.endGroup();

    readGlobalConfigNotebooks(settings);
}

void VNote::writeGlobalConfig()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       orgName, appName);

    // [global] section
    settings.beginGroup("global");
    settings.setValue("current_notebook", curNotebookIndex);
    qDebug() << "write current_notebook=" << curNotebookIndex;
    settings.endGroup();

    writeGlobalConfigNotebooks(settings);
}

void VNote::readGlobalConfigNotebooks(QSettings &settings)
{
    notebooks.clear();
    int size = settings.beginReadArray("notebooks");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        VNotebook notebook;
        QString name = settings.value("name").toString();
        QString path = settings.value("path").toString();
        notebook.setName(name);
        notebook.setPath(path);
        notebooks.append(notebook);
    }
    settings.endArray();
    qDebug() << "read" << notebooks.size()
             << "notebook items from [notebooks] section";
}

void VNote::writeGlobalConfigNotebooks(QSettings &settings)
{
    settings.beginWriteArray("notebooks");
    for (int i = 0; i < notebooks.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("name", notebooks[i].getName());
        settings.setValue("path", notebooks[i].getPath());
    }
    settings.endArray();
    qDebug() << "write" << notebooks.size()
             << "notebook items in [notebooks] section";
}

const QVector<VNotebook>& VNote::getNotebooks()
{
    return notebooks;
}

int VNote::getCurNotebookIndex() const
{
    return curNotebookIndex;
}

void VNote::setCurNotebookIndex(int index)
{
    curNotebookIndex = index;

    // Update settings
    QSettings settings(QSettings::IniFormat, QSettings::UserScope,
                       orgName, appName);
    settings.setValue("global/current_notebook", curNotebookIndex);
    qDebug() << "write current_notebook=" << curNotebookIndex;
}
