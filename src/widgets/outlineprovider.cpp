#include "outlineprovider.h"

using namespace vnotex;

void Outline::clear()
{
    m_headings.clear();
}

bool Outline::operator==(const Outline &p_a) const
{
    return m_headings == p_a.m_headings;
}

bool Outline::isEmpty() const
{
    return m_headings.isEmpty();
}

Outline::Heading::Heading(const QString &p_name, int p_level)
    : m_name(p_name),
      m_level(p_level)
{
}

bool Outline::Heading::operator==(const Outline::Heading &p_a) const
{
    return m_level == p_a.m_level && m_name == p_a.m_name;
}

OutlineProvider::OutlineProvider(QObject *p_parent)
    : QObject(p_parent)
{
}

OutlineProvider::~OutlineProvider()
{
}

void OutlineProvider::setOutline(const QSharedPointer<Outline> &p_outline)
{
    m_outline = p_outline;
    m_currentHeadingIndex = -1;
    emit outlineChanged();
}

const QSharedPointer<Outline> &OutlineProvider::getOutline() const
{
    return m_outline;
}

int OutlineProvider::getCurrentHeadingIndex() const
{
    return m_currentHeadingIndex;
}

void OutlineProvider::setCurrentHeadingIndex(int p_idx)
{
    if (m_currentHeadingIndex == p_idx) {
        return;
    }

    m_currentHeadingIndex = p_idx;
    emit currentHeadingChanged();
}
