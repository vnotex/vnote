#include "dummyversioncontroller.h"

using namespace vnotex;


DummyVersionController::DummyVersionController(const QString &p_name,
                                               const QString &p_displayName,
                                               const QString &p_description,
                                               QObject *p_parent)
    : IVersionController(p_parent),
      m_info(p_name, p_displayName, p_description)
{
}

QString DummyVersionController::getName() const
{
    return m_info.m_name;
}

QString DummyVersionController::getDisplayName() const
{
    return m_info.m_displayName;
}

QString DummyVersionController::getDescription() const
{
    return m_info.m_description;
}
