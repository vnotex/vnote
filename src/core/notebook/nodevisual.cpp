#include "nodevisual.h"

#include <QColor>
#include <QtMath>

using namespace vnotex;

NodeVisual::NodeVisual(const QString &p_backgroundColor, const QString &p_borderColor,
                       const QString &p_nameColor)
    : m_backgroundColor(p_backgroundColor), m_borderColor(p_borderColor), m_nameColor(p_nameColor) {
}

bool NodeVisual::hasAnyVisualEffect() const {
  return !m_backgroundColor.isEmpty() || !m_borderColor.isEmpty() || !m_nameColor.isEmpty();
}

void NodeVisual::clearAllColors() {
  m_backgroundColor.clear();
  m_borderColor.clear();
  m_nameColor.clear();
}
