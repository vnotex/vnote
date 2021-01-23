#include "build.h"

#include <QFile>
#include <QDebug>

using namespace vnotex;

Build::Build()
{
    
}

QString Build::name() const
{
    return "";
}

bool Build::isValidBuildFile(const QString &p_file)
{
    QFile file(p_file);
    if (!file.exists()) {
        qWarning() << "build file does not exist: " << p_file;
        return false;
    }
    
    return true;
}
