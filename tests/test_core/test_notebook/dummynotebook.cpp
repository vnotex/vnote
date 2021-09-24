#include "dummynotebook.h"

#include <notebook/node.h>

using namespace tests;

using namespace vnotex;

DummyNotebook::DummyNotebook(const QString &p_name, QObject *p_parent)
    : Notebook(p_name, p_parent)
{
}

void DummyNotebook::updateNotebookConfig()
{
}

void DummyNotebook::removeNotebookConfig()
{
}

void DummyNotebook::remove()
{
}

void DummyNotebook::initializeInternal()
{
}

const QJsonObject &DummyNotebook::getExtraConfigs() const
{
    return m_extraConfigs;
}

void DummyNotebook::setExtraConfig(const QString &p_key, const QJsonObject &p_obj)
{
    Q_UNUSED(p_key);
    Q_UNUSED(p_obj);
}

QSharedPointer<vnotex::Node> DummyNotebook::loadNodeByPath(const QString &p_path)
{
    Q_UNUSED(p_path);
    return nullptr;
}
