#include "outlineprovider.h"

using namespace vnotex;

void Outline::clear() { m_headings.clear(); }

bool Outline::operator==(const Outline &p_a) const { return m_headings == p_a.m_headings; }

bool Outline::isEmpty() const { return m_headings.isEmpty(); }

Outline::Heading::Heading(const QString &p_name, int p_level) : m_name(p_name), m_level(p_level) {}

bool Outline::Heading::operator==(const Outline::Heading &p_a) const {
  return m_level == p_a.m_level && m_name == p_a.m_name;
}

OutlineProvider::OutlineProvider(QObject *p_parent) : QObject(p_parent) {}

OutlineProvider::~OutlineProvider() {}

void OutlineProvider::setOutline(const QSharedPointer<Outline> &p_outline) {
  m_outline = p_outline;
  m_currentHeadingIndex = -1;
  emit outlineChanged();
}

const QSharedPointer<Outline> &OutlineProvider::getOutline() const { return m_outline; }

int OutlineProvider::getCurrentHeadingIndex() const { return m_currentHeadingIndex; }

void OutlineProvider::setCurrentHeadingIndex(int p_idx) {
  if (m_currentHeadingIndex == p_idx) {
    return;
  }

  m_currentHeadingIndex = p_idx;
  emit currentHeadingChanged();
}

void OutlineProvider::increaseSectionNumber(SectionNumber &p_sectionNumber, int p_level,
                                            int p_baseLevel) {
  Q_ASSERT(p_level >= 1 && p_level < p_sectionNumber.size());
  if (p_level < p_baseLevel) {
    p_sectionNumber.fill(0);
    return;
  }

  ++p_sectionNumber[p_level];
  for (int i = p_level + 1; i < p_sectionNumber.size(); ++i) {
    p_sectionNumber[i] = 0;
  }
}

QString OutlineProvider::joinSectionNumber(const SectionNumber &p_sectionNumber, bool p_endingDot) {
  QString res;
  for (auto sec : p_sectionNumber) {
    if (sec != 0) {
      if (res.isEmpty()) {
        res = QString::number(sec);
      } else {
        res += '.' + QString::number(sec);
      }
    } else if (res.isEmpty()) {
      continue;
    } else {
      break;
    }
  }

  if (p_endingDot && !res.isEmpty()) {
    return res + '.';
  } else {
    return res;
  }
}
