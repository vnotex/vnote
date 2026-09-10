#include "outlineprovider.h"

using namespace vnotex;

void Outline::clear() {
  m_headings.clear();
  m_hasSectionNumber = false;
}

bool Outline::operator==(const Outline &p_a) const {
  return m_headings == p_a.m_headings && m_hasSectionNumber == p_a.m_hasSectionNumber;
}

bool Outline::isEmpty() const { return m_headings.isEmpty(); }

Outline::Heading::Heading(const QString &p_name, int p_level) : m_name(p_name), m_level(p_level) {}

bool Outline::Heading::operator==(const Outline::Heading &p_a) const {
  return m_level == p_a.m_level && m_name == p_a.m_name && m_isPlaceholder == p_a.m_isPlaceholder;
}

OutlineProvider::OutlineProvider(QObject *p_parent) : QObject(p_parent) {}

OutlineProvider::~OutlineProvider() {}

void OutlineProvider::setOutline(const QSharedPointer<Outline> &p_outline) {
  m_outline = p_outline;
  m_currentHeadingIndex = -1;
  emit outlineChanged();
}

const QSharedPointer<Outline> &OutlineProvider::getOutline() const { return m_outline; }

void OutlineProvider::setReorderSupported(bool p_supported) {
  if (!m_outline || m_outline->m_reorderSupported == p_supported) {
    return;
  }

  m_outline->m_reorderSupported = p_supported;
  emit outlineChanged();
}

void OutlineProvider::requestMove(int p_sourceHeadingIndex, int p_beforeHeadingIndex,
                                  int p_targetLevel) {
  if (!m_outline || !m_outline->m_reorderSupported) {
    return;
  }

  emit moveRequested(p_sourceHeadingIndex, p_beforeHeadingIndex, p_targetLevel);
}

int OutlineProvider::getCurrentHeadingIndex() const { return m_currentHeadingIndex; }

void OutlineProvider::setCurrentHeadingIndex(int p_idx) {
  if (m_currentHeadingIndex == p_idx) {
    return;
  }

  m_currentHeadingIndex = p_idx;
  emit currentHeadingChanged();
}
