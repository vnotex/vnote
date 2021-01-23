#include "task.h"

#include <QFile>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>

#include "utils/fileutils.h"
#include "utils/pathutils.h"
#include "taskmgr.h"

using namespace vnotex;

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

Task *Task::fromFile(const QString &p_file)
{
    Q_ASSERT(!p_file.isEmpty());
    return new Task(p_file);
}

TaskInfo* Task::getTaskInfo(const QString &p_file, 
                            const QString &p_locale)
{
    auto obj = readTaskFile(p_file);
    auto ptr = getTaskInfoFromTaskDescription(obj, p_locale);
    auto fileName = QFileInfo(p_file).baseName();
    ptr->m_filePath = p_file;
    if (ptr->m_name.isNull()) ptr->m_name = fileName; 
    if (ptr->m_displayName.isNull()) ptr->m_displayName = fileName;
    return ptr;
}

QJsonObject Task::readTaskFile(const QString &p_file)
{
    auto bytes = FileUtils::readFile(p_file);
    return QJsonDocument::fromJson(bytes).object();
}

Task::Task(const QString &p_taskFilePath)
    : m_taskFilePath(p_taskFilePath)
{
    
}

TaskInfo* Task::getTaskInfoFromTaskDescription(
        const QJsonObject &p_obj, 
        const QString &p_locale)
{
    auto ptr = new TaskInfo;
    ptr->m_name = p_obj["label"].toString();
    ptr->m_displayName = ptr->m_name;
    for (const auto &task : p_obj["tasks"].toArray()) {
        ptr->m_subTask.append(
            getTaskInfoFromTaskDescription(task.toObject(), p_locale));
    }
    return ptr;
}
