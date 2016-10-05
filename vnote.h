#ifndef VNOTE_H
#define VNOTE_H

#include <QString>
#include <QVector>
#include <QSettings>
#include "vnotebook.h"

class VNote
{
public:
    VNote();
    void readGlobalConfig();
    void writeGlobalConfig();

    const QVector<VNotebook>& getNotebooks();
    int getCurNotebookIndex() const;
    void setCurNotebookIndex(int index);

    // The name of the config file in each subdirectory
    static const QString dirConfigFileName;
    static const QString orgName;
    static const QString appName;

private:
    // Write notebooks section of global config
    void writeGlobalConfigNotebooks(QSettings &settings);
    // Read notebooks section of global config
    void readGlobalConfigNotebooks(QSettings &settings);

    QVector<VNotebook> notebooks;
    int curNotebookIndex;
};

#endif // VNOTE_H
