#include "outlineprovider.h"

#include <QCryptographicHash>

using namespace vnotex;

void Outline::clear() {
  m_headings.clear();
  m_hasSectionNumber = false;
}

bool Outline::operator==(const Outline &p_a) const {
  return m_headings == p_a.m_headings && m_hasSectionNumber == p_a.m_hasSectionNumber;
}

bool Outline::isEmpty() const { return m_headings.isEmpty(); }

QByteArray Outline::navigationFingerprint() const {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const auto addScalar = [&hash](const auto &p_value) {
    hash.addData(QByteArrayView(reinterpret_cast<const char *>(&p_value), sizeof(p_value)));
  };
  const quint64 count = m_headings.size();
  addScalar(count);
  for (const auto &heading : m_headings) {
    const qint32 level = heading.m_level;
    const quint8 placeholder = heading.m_isPlaceholder;
    const quint64 length = heading.m_name.size();
    addScalar(level);
    addScalar(placeholder);
    addScalar(length);
    hash.addData(QByteArrayView(reinterpret_cast<const char *>(heading.m_name.constData()),
                                heading.m_name.size() * sizeof(QChar)));
  }
  return hash.result();
}

OutlineMove Outline::planMove(int p_sourceHeadingIndex, int p_targetHeadingIndex,
                              OutlineDropPosition p_position) const {
  OutlineMove move;
  if (!m_reorderSupported || p_sourceHeadingIndex < 0 ||
      p_sourceHeadingIndex >= m_headings.size()) {
    return move;
  }
  const auto &source = m_headings[p_sourceHeadingIndex];
  if (!source.m_reorderable || source.m_isPlaceholder || source.m_level < 1) {
    return move;
  }
  const auto subtreeEnd = [this](int p_index) {
    int end = p_index + 1;
    while (end < m_headings.size() && m_headings[end].m_level > m_headings[p_index].m_level) {
      ++end;
    }
    return end;
  };
  const int sourceEnd = subtreeEnd(p_sourceHeadingIndex);
  int before = -1;
  int parent = -1;
  qint64 targetLevel = source.m_level;
  if (p_position == OutlineDropPosition::OnViewport) {
    int rootLevel = -1;
    for (int i = 0; i < m_headings.size(); ++i) {
      const auto &heading = m_headings[i];
      if ((i < p_sourceHeadingIndex || i >= sourceEnd) && !heading.m_isPlaceholder &&
          (heading.m_reorderable || heading.m_acceptsChildren) && heading.m_level > 0) {
        rootLevel = rootLevel < 0 ? heading.m_level : qMin(rootLevel, heading.m_level);
      }
    }
    targetLevel = rootLevel < 0 ? source.m_level : rootLevel;
  } else {
    if (p_targetHeadingIndex < 0 || p_targetHeadingIndex >= m_headings.size() ||
        (p_targetHeadingIndex >= p_sourceHeadingIndex && p_targetHeadingIndex < sourceEnd)) {
      return move;
    }
    const auto &target = m_headings[p_targetHeadingIndex];
    if (target.m_isPlaceholder || target.m_level < 1 ||
        (!target.m_reorderable &&
         !(p_position == OutlineDropPosition::OnItem && target.m_acceptsChildren))) {
      return move;
    }
    if (p_position == OutlineDropPosition::OnItem) {
      parent = p_targetHeadingIndex;
      targetLevel = qint64(target.m_level) + 1;
    } else if (p_position == OutlineDropPosition::AboveItem ||
               p_position == OutlineDropPosition::BelowItem) {
      targetLevel = target.m_level;
      for (int i = p_targetHeadingIndex - 1; i >= 0; --i) {
        if (!m_headings[i].m_isPlaceholder && m_headings[i].m_level < target.m_level) {
          parent = i;
          break;
        }
      }
    } else {
      return move;
    }
    if (p_position == OutlineDropPosition::AboveItem) {
      before = p_targetHeadingIndex;
    } else {
      int end = subtreeEnd(p_targetHeadingIndex);
      if (end >= p_sourceHeadingIndex && end < sourceEnd) {
        end = sourceEnd;
      }
      while (end < m_headings.size() && !m_headings[end].m_reorderable) {
        ++end;
      }
      before = end < m_headings.size() ? end : -1;
    }
  }
  if (targetLevel < 1 || targetLevel > m_maxLevel) {
    return move;
  }
  const qint64 delta = targetLevel - source.m_level;
  for (int i = p_sourceHeadingIndex; i < sourceEnd; ++i) {
    if (m_headings[i].m_reorderable) {
      const qint64 level = m_headings[i].m_level + delta;
      if (level < 1 || level > m_maxLevel) {
        return move;
      }
    }
  }
  // Inserting immediately after the source subtree leaves order unchanged.
  // A different level still matters: an adjacent move can reparent the section.
  int next = sourceEnd;
  while (next < m_headings.size() && !m_headings[next].m_reorderable) {
    ++next;
  }
  if (delta == 0 && before == (next < m_headings.size() ? next : -1)) {
    return move;
  }
  move.m_sourceHeadingIndex = p_sourceHeadingIndex;
  move.m_beforeHeadingIndex = before;
  move.m_targetLevel = int(targetLevel);
  move.m_parentHeadingIndex = parent;
  return move;
}

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

bool OutlineProvider::getAutoSectionNumberAllowed() const { return m_autoSectionNumberAllowed; }

void OutlineProvider::setAutoSectionNumberAllowed(bool p_allowed) {
  if (m_autoSectionNumberAllowed == p_allowed) {
    return;
  }
  m_autoSectionNumberAllowed = p_allowed;
  emit autoSectionNumberAllowedChanged();
}

void OutlineProvider::requestMove(const OutlineMove &p_move) {
  if (!m_outline || !m_outline->m_reorderSupported || !p_move.isValid() ||
      p_move.m_sourceHeadingIndex >= m_outline->m_headings.size() ||
      !m_outline->m_headings[p_move.m_sourceHeadingIndex].m_reorderable ||
      p_move.m_beforeHeadingIndex < -1 ||
      p_move.m_beforeHeadingIndex >= m_outline->m_headings.size() ||
      p_move.m_parentHeadingIndex < -1 ||
      p_move.m_parentHeadingIndex >= m_outline->m_headings.size() || p_move.m_targetLevel < 1 ||
      p_move.m_targetLevel > m_outline->m_maxLevel) {
    return;
  }
  emit moveRequested(p_move);
}

int OutlineProvider::getCurrentHeadingIndex() const { return m_currentHeadingIndex; }

void OutlineProvider::setCurrentHeadingIndex(int p_idx) {
  if (m_currentHeadingIndex == p_idx) {
    return;
  }

  m_currentHeadingIndex = p_idx;
  emit currentHeadingChanged();
}
