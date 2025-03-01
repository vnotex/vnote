#ifndef NODEINFO_H
#define NODEINFO_H

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>

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

// Lightweight value type for identifying and displaying a notebook node.
// Used by new architecture (NotebookExplorer2, MVC components).
// Extends NodeIdentifier with display metadata.
struct NodeInfo {
  // --- Identity (required for all operations) ---
  NodeIdentifier id;     // Unique identifier (notebookId + relativePath)
  bool isFolder = false; // True for folders, false for files

  // --- Cached metadata (for display, avoid repeated service calls) ---
  QString name;              // Display name (last path component or notebook name for root)
  QDateTime createdTimeUtc;  // Creation timestamp
  QDateTime modifiedTimeUtc; // Last modification timestamp
  int childCount = 0;        // Number of direct children (folders only)
  QStringList tags;          // Associated tags (files only)
  QString preview;           // Content preview (first N chars, files only)

  // --- Visual properties (from NodeVisual or node metadata) ---
  QString backgroundColor; // Background color (empty = default)
  QString borderColor;     // Border color (empty = default)
  QString textColor;       // Text/name color (empty = default)

  // --- Convenience accessors (delegate to id) ---
  const QString &notebookId() const { return id.notebookId; }
  const QString &relativePath() const { return id.relativePath; }
  bool isRoot() const { return id.isRoot(); }
  bool isValid() const { return id.isValid(); }
  QString parentPath() const { return id.parentPath(); }

  // Check if any visual styling is applied
  bool hasVisualStyle() const {
    return !backgroundColor.isEmpty() || !borderColor.isEmpty() || !textColor.isEmpty();
  }

  // Equality based on identity only (same as NodeIdentifier)
  bool operator==(const NodeInfo &p_other) const { return id == p_other.id; }

  bool operator!=(const NodeInfo &p_other) const { return !(*this == p_other); }
};

inline uint qHash(const NodeInfo &p_info, uint p_seed = 0) { return qHash(p_info.id, p_seed); }

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::NodeIdentifier)
Q_DECLARE_METATYPE(vnotex::NodeInfo)

#endif // NODEINFO_H
