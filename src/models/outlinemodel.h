#ifndef OUTLINEMODEL_H
#define OUTLINEMODEL_H

#include <QAbstractItemModel>
#include <QSharedPointer>
#include <QString>
#include <QVector>

class QMimeData;

namespace vnotex {

struct Outline;

// Internal tree node for the outline model.
struct OutlineNode {
  OutlineNode() = default;
  ~OutlineNode();

  // Heading name.
  QString m_name;

  // Heading level (1-based).
  int m_level = -1;

  // Index in the original flat Outline::m_headings vector.
  // -1 for virtual/gap-filling nodes.
  int m_headingIndex = -1;

  bool m_reorderable = false;

  // Precomputed section number string (e.g., "1.2.").
  // Empty when section numbering is disabled.
  QString m_sectionNumber;

  OutlineNode *m_parent = nullptr;
  QVector<OutlineNode *> m_children;
};

// A QAbstractItemModel that exposes Outline heading data as a tree structure
// for use with QTreeView. Headings nest by level: H1 is top-level, H2 under
// H1, H3 under H2, etc. Gap-filling nodes are inserted for skipped levels
// via OutlineProvider::makePerfectHeadings().
//
// This model is pure data — it does not depend on ServiceLocator or any
// singleton. It receives outline data via setOutline() and rebuilds its
// internal tree each time.
class OutlineModel : public QAbstractItemModel {
  Q_OBJECT

public:
  enum Roles {
    HeadingIndexRole = Qt::UserRole, // int — index in flat Outline::m_headings
    HeadingLevelRole,                // int — one-based heading level
    ReorderableRole                  // bool — concrete editable heading
  };

  explicit OutlineModel(QObject *p_parent = nullptr);
  ~OutlineModel() override;

  // Replace the entire outline. Rebuilds the tree.
  void setOutline(const QSharedPointer<Outline> &p_outline);

  bool isReorderSupported() const;

  // Set/get the current heading index (for highlighting in the view).
  void setCurrentHeadingIndex(int p_idx);
  int getCurrentHeadingIndex() const;

  // Section number display configuration.
  void setAutoSectionNumberEnabled(bool p_enabled);
  void setSectionNumberPattern(const QString &p_pattern);

  // Get the QModelIndex for a given heading index (for the view to highlight).
  QModelIndex indexForHeadingIndex(int p_headingIndex) const;

  // QAbstractItemModel interface.
  QModelIndex index(int p_row, int p_column,
                    const QModelIndex &p_parent = QModelIndex()) const override;
  QModelIndex parent(const QModelIndex &p_child) const override;
  int rowCount(const QModelIndex &p_parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &p_parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &p_index, int p_role = Qt::DisplayRole) const override;
  Qt::ItemFlags flags(const QModelIndex &p_index) const override;
  QStringList mimeTypes() const override;
  Qt::DropActions supportedDropActions() const override;
  bool canDropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row, int p_column,
                       const QModelIndex &p_parent) const override;
  bool dropMimeData(const QMimeData *p_data, Qt::DropAction p_action, int p_row, int p_column,
                    const QModelIndex &p_parent) override;

private:
  // Rebuild the internal tree from the current outline data.
  void buildTree();

  // Recursively delete all children of a node.
  static void clearChildren(OutlineNode *p_node);

  // Recursively search for a node with the given heading index.
  QModelIndex findNodeByHeadingIndex(OutlineNode *p_node, int p_headingIndex) const;

  // Virtual root node (not displayed).
  OutlineNode *m_root = nullptr;

  // The current outline data.
  QSharedPointer<Outline> m_outline;

  // Index of the heading currently under the cursor.
  int m_currentHeadingIndex = -1;

  bool m_reorderSupported = false;

  // Section number configuration.
  bool m_autoSectionNumberEnabled = true;
  QString m_sectionNumberPattern = QStringLiteral("1.1.");
};

} // namespace vnotex

#endif // OUTLINEMODEL_H
