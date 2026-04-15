#ifndef NODEIDENTIFIER_H
#define NODEIDENTIFIER_H

#include <QDataStream>
#include <QMetaType>
#include <QString>

namespace vnotex {

// Minimal identifier for a node in a vxcore notebook.
// Maps directly to vxcore unified node API: vxcore_node_*(context, notebook_id, node_path, ...).
// Use this when you only need to identify a node, not display it.
struct NodeIdentifier {
  QString notebookId;   // Notebook GUID
  QString relativePath; // Path relative to notebook root ("" for root, "folder/note.md")

  // --- Convenience methods ---
  bool isRoot() const { return relativePath.isEmpty(); }

  bool isValid() const { return !notebookId.isEmpty(); }

  QString parentPath() const {
    int lastSlash = relativePath.lastIndexOf(QLatin1Char('/'));
    return lastSlash > 0 ? relativePath.left(lastSlash) : QString();
  }

  // Equality for QSet/QHash usage
  bool operator==(const NodeIdentifier &p_other) const {
    return notebookId == p_other.notebookId && relativePath == p_other.relativePath;
  }

  bool operator!=(const NodeIdentifier &p_other) const { return !(*this == p_other); }
};

inline uint qHash(const NodeIdentifier &p_id, uint p_seed = 0) {
  return qHash(p_id.notebookId, p_seed) ^ qHash(p_id.relativePath, p_seed);
}

inline QDataStream &operator<<(QDataStream &p_stream, const NodeIdentifier &p_id) {
  p_stream << p_id.notebookId << p_id.relativePath;
  return p_stream;
}

inline QDataStream &operator>>(QDataStream &p_stream, NodeIdentifier &p_id) {
  p_stream >> p_id.notebookId >> p_id.relativePath;
  return p_stream;
}

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::NodeIdentifier)

#endif // NODEIDENTIFIER_H
