#include "vfilelocation.h"

VFileLocation::VFileLocation()
{
}

VFileLocation::VFileLocation(const QString &p_notebook, const QString &p_relativePath)
    : m_notebook(p_notebook), m_relativePath(p_relativePath)
{
}
