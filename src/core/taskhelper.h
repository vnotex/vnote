#ifndef TASKHELPER_H
#define TASKHELPER_H

#include <QString>
#include <QSharedPointer>

class QProcess;

namespace vnotex {

class Notebook;
class Task;

class TaskHelper
{
public:
    // helper functions
    
    static QString normalPath(const QString &p_text);

    static QString getCurrentFile();
    
    static QSharedPointer<Notebook> getCurrentNotebook();
    
    static QString getFileNotebookFolder(const QString p_currentFile);
    
    static QString getSelectedText();

    static QStringList getAllSpecialVariables(const QString &p_name, const QString &p_text);
    
    static QString replaceAllSepcialVariables(const QString &p_name,
                                              const QString &p_text,
                                              const QMap<QString, QString> &p_map);

    static QString evaluateJsonExpr(const QJsonObject &p_obj,
                                    const QString &p_expr);
    
    static QString getPathSeparator();
    
    static QString handleCommand(const QString &p_text,
                                 QProcess *p_process,
                                 const Task *p_task);
    
private:
    TaskHelper();
};

} // ns vnotex

#endif // TASKHELPER_H
