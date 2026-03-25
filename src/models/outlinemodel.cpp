#include "outlinemodel.h"

#include <QStack>

#include <widgets/outlineprovider.h>

using namespace vnotex;

// OutlineNode

OutlineNode::~OutlineNode() {
  qDeleteAll(m_children);
}

// OutlineModel

OutlineModel::OutlineModel(QObject *p_parent)
    : QAbstractItemModel(p_parent), m_root(new OutlineNode()) {}

OutlineModel::~OutlineModel() {
  delete m_root;
}

void OutlineModel::setOutline(const QSharedPointer<Outline> &p_outline) {
  beginResetModel();

  m_outline = p_outline;
  m_currentHeadingIndex = -1;

  // Pick up section number config from the outline if provided.
  if (m_outline) {
    m_sectionNumberBaseLevel = m_outline->m_sectionNumberBaseLevel;
    m_sectionNumberEndingDot = m_outline->m_sectionNumberEndingDot;
    m_sectionNumberEnabled = (m_outline->m_sectionNumberBaseLevel != -1);
  }

  buildTree();

  endResetModel();
}

void OutlineModel::setCurrentHeadingIndex(int p_idx) {
  m_currentHeadingIndex = p_idx;
}

int OutlineModel::getCurrentHeadingIndex() const {
  return m_currentHeadingIndex;
}

void OutlineModel::setSectionNumberEnabled(bool p_enabled) {
  if (m_sectionNumberEnabled == p_enabled) {
    return;
  }

  m_sectionNumberEnabled = p_enabled;

  // Rebuild tree to recompute display strings.
  beginResetModel();
  buildTree();
  endResetModel();
}

void OutlineModel::setSectionNumberBaseLevel(int p_level) {
  if (m_sectionNumberBaseLevel == p_level) {
    return;
  }

  m_sectionNumberBaseLevel = p_level;

  // Rebuild tree to recompute section numbers.
  beginResetModel();
  buildTree();
  endResetModel();
}

void OutlineModel::setSectionNumberEndingDot(bool p_endingDot) {
  if (m_sectionNumberEndingDot == p_endingDot) {
    return;
  }

  m_sectionNumberEndingDot = p_endingDot;

  // Rebuild tree to recompute section numbers.
  beginResetModel();
  buildTree();
  endResetModel();
}

QModelIndex OutlineModel::indexForHeadingIndex(int p_headingIndex) const {
  if (p_headingIndex < 0 || !m_root) {
    return QModelIndex();
  }

  return findNodeByHeadingIndex(m_root, p_headingIndex);
}

QModelIndex OutlineModel::index(int p_row, int p_column,
                                const QModelIndex &p_parent) const {
  if (!hasIndex(p_row, p_column, p_parent)) {
    return QModelIndex();
  }

  OutlineNode *parentNode =
      p_parent.isValid()
          ? static_cast<OutlineNode *>(p_parent.internalPointer())
          : m_root;

  if (p_row >= 0 && p_row < parentNode->m_children.size()) {
    return createIndex(p_row, p_column, parentNode->m_children[p_row]);
  }

  return QModelIndex();
}

QModelIndex OutlineModel::parent(const QModelIndex &p_child) const {
  if (!p_child.isValid()) {
    return QModelIndex();
  }

  auto *childNode = static_cast<OutlineNode *>(p_child.internalPointer());
  OutlineNode *parentNode = childNode->m_parent;

  if (!parentNode || parentNode == m_root) {
    return QModelIndex();
  }

  // Find the row of parentNode within its own parent.
  OutlineNode *grandParent = parentNode->m_parent;
  if (!grandParent) {
    return QModelIndex();
  }

  int row = grandParent->m_children.indexOf(parentNode);
  if (row < 0) {
    return QModelIndex();
  }

  return createIndex(row, 0, parentNode);
}

int OutlineModel::rowCount(const QModelIndex &p_parent) const {
  if (p_parent.column() > 0) {
    return 0;
  }

  const OutlineNode *parentNode =
      p_parent.isValid()
          ? static_cast<const OutlineNode *>(p_parent.internalPointer())
          : m_root;

  return parentNode ? parentNode->m_children.size() : 0;
}

int OutlineModel::columnCount(const QModelIndex &p_parent) const {
  Q_UNUSED(p_parent);
  return 1;
}

QVariant OutlineModel::data(const QModelIndex &p_index, int p_role) const {
  if (!p_index.isValid()) {
    return QVariant();
  }

  auto *node = static_cast<OutlineNode *>(p_index.internalPointer());
  if (!node) {
    return QVariant();
  }

  switch (p_role) {
  case Qt::DisplayRole: {
    if (m_sectionNumberEnabled && !node->m_sectionNumber.isEmpty()) {
      return node->m_sectionNumber + QLatin1Char(' ') + node->m_name;
    }
    return node->m_name;
  }

  case Qt::ToolTipRole:
    return node->m_name;

  case HeadingIndexRole:
    return node->m_headingIndex;

  default:
    return QVariant();
  }
}

Qt::ItemFlags OutlineModel::flags(const QModelIndex &p_index) const {
  if (!p_index.isValid()) {
    return Qt::NoItemFlags;
  }

  return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void OutlineModel::buildTree() {
  clearChildren(m_root);

  if (!m_outline || m_outline->m_headings.isEmpty()) {
    return;
  }

  // Fill gaps for skipped heading levels (e.g., H1 -> H3 inserts [EMPTY] H2).
  QVector<Outline::Heading> perfectHeadings;
  OutlineProvider::makePerfectHeadings(m_outline->m_headings, perfectHeadings);

  // Build a mapping from perfect-heading index to original heading index.
  // Perfect headings include gap-fillers; originals don't.
  // We walk both vectors in sync to find matches.
  QVector<int> perfectToOriginal(perfectHeadings.size(), -1);
  {
    int origIdx = 0;
    for (int i = 0; i < perfectHeadings.size(); ++i) {
      if (origIdx < m_outline->m_headings.size() &&
          perfectHeadings[i] == m_outline->m_headings[origIdx]) {
        perfectToOriginal[i] = origIdx;
        ++origIdx;
      }
      // else: this is a gap-filling heading, index stays -1.
    }
  }

  // Initialize section number vector.
  // Levels are 1-based; the vector is indexed by level.
  // Allocate enough slots for the maximum level encountered.
  int maxLevel = 0;
  for (const auto &h : perfectHeadings) {
    if (h.m_level > maxLevel) {
      maxLevel = h.m_level;
    }
  }

  SectionNumber sectionNumber(maxLevel + 1, 0);

  // Build tree nodes.
  // Maintain a stack of (level, node) to track current nesting position.
  // m_root acts as the virtual parent at level 0.
  OutlineNode *currentParent = m_root;
  int currentLevel = 0;

  for (int i = 0; i < perfectHeadings.size(); ++i) {
    const auto &heading = perfectHeadings[i];
    int level = heading.m_level;

    // Compute section number for this heading.
    if (m_sectionNumberEnabled && m_sectionNumberBaseLevel > 0) {
      OutlineProvider::increaseSectionNumber(
          sectionNumber, level, m_sectionNumberBaseLevel);
    }

    // Navigate to the correct parent based on level.
    if (level > currentLevel) {
      // Go deeper: the last child of the current parent becomes the new parent
      // (if there is one). Otherwise, the current parent stays.
      if (!currentParent->m_children.isEmpty()) {
        currentParent = currentParent->m_children.last();
        currentLevel = currentParent->m_level;
      }
      // If still deeper, walk down the last-child chain.
      while (level > currentLevel + 1 && !currentParent->m_children.isEmpty()) {
        currentParent = currentParent->m_children.last();
        currentLevel = currentParent->m_level;
      }
    } else if (level < currentLevel) {
      // Go up: walk up parents until we find the right nesting level.
      while (currentParent != m_root && currentParent->m_level >= level) {
        currentParent = currentParent->m_parent;
      }
      currentLevel = (currentParent == m_root) ? 0 : currentParent->m_level;
    }
    // If level == currentLevel, currentParent stays the same (sibling).

    auto *node = new OutlineNode();
    node->m_name = heading.m_name;
    node->m_level = level;
    node->m_headingIndex = perfectToOriginal[i];
    node->m_parent = currentParent;

    // Compute section number string.
    if (m_sectionNumberEnabled && m_sectionNumberBaseLevel > 0) {
      node->m_sectionNumber = OutlineProvider::joinSectionNumber(
          sectionNumber, m_sectionNumberEndingDot);
    }

    currentParent->m_children.append(node);

    // After adding this node, update currentLevel so that the next heading
    // at the same level will be a sibling, and deeper levels will be children.
    currentLevel = level;
  }
}

void OutlineModel::clearChildren(OutlineNode *p_node) {
  if (!p_node) {
    return;
  }

  qDeleteAll(p_node->m_children);
  p_node->m_children.clear();
}

QModelIndex OutlineModel::findNodeByHeadingIndex(OutlineNode *p_node,
                                                 int p_headingIndex) const {
  if (!p_node) {
    return QModelIndex();
  }

  for (int i = 0; i < p_node->m_children.size(); ++i) {
    OutlineNode *child = p_node->m_children[i];
    if (child->m_headingIndex == p_headingIndex) {
      return createIndex(i, 0, child);
    }

    // Recurse into children.
    QModelIndex found = findNodeByHeadingIndex(child, p_headingIndex);
    if (found.isValid()) {
      return found;
    }
  }

  return QModelIndex();
}
