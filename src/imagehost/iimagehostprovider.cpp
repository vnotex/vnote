#include "iimagehostprovider.h"

using namespace vnotex;

IImageHostProvider::IImageHostProvider(QObject *p_parent)
  : QObject(p_parent)
{
}

const QString &IImageHostProvider::getName() const
{
  return m_name;
}

void IImageHostProvider::setName(const QString &p_name)
{
  m_name = p_name;
}
