#include "task.h"

#include <QFile>
#include <QDebug>
#include <QJsonDocument>

#include "utils/fileutils.h"
#include "utils/pathutils.h"

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

QString Task::getDisplayName(const QString &p_file, const QString &p_locale)
{
    auto obj = readTaskFile(p_file);    
    if (obj.contains("label")) {
        return obj.value("label").toString();
    }
    return QFileInfo(p_file).baseName();
}

QJsonObject Task::readTaskFile(const QString &p_file)
{
    auto bytes = FileUtils::readFile(p_file);
    return QJsonDocument::fromJson(bytes).object();
}
