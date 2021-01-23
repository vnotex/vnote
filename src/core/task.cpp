#include "task.h"

#include <QFile>
#include <QDebug>

using namespace vnotex;

Task::Task()
{
    
}

QString Task::name() const
{
    return "";
}

bool Task::isValidTaskFile(const QString &p_file)
{
    QFile file(p_file);
    if (!file.exists()) {
        qWarning() << "task file does not exist: " << p_file;
        return false;
    }
    
    return true;
}
