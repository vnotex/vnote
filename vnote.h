#ifndef VNOTE_H
#define VNOTE_H

#include <QString>
#include <QVector>
#include <QSettings>
#include <QFont>
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

    static void decorateTemplate();

    static const QString orgName;
    static const QString appName;
    static const QString welcomePagePath;
    static const QString templatePath;

    static QString templateHtml;

private:
    // Write notebooks section of global config
    void writeGlobalConfigNotebooks(QSettings &settings);
    // Read notebooks section of global config
    void readGlobalConfigNotebooks(QSettings &settings);

    QVector<VNotebook> notebooks;
    int curNotebookIndex;
    static const QString preTemplatePath;
    static const QString postTemplatePath;
    static const QString defaultCssUrl;

    // For self CSS definition
    static QString cssUrl;
};

#endif // VNOTE_H
