#ifndef NODEINFO_H
#define NODEINFO_H

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>

namespace vnotex {

// Lightweight value type for identifying and displaying a notebook node.
// Used by new architecture (NotebookExplorer2, MVC components).
// Maps directly to NotebookService API: (notebookId, relativePath).
struct NodeInfo {
  // --- Identity (required for all operations) ---
  QString notebookId;    // Notebook GUID
  QString relativePath;  // Path relative to notebook root ("" for root, "folder/note.md")
  bool isFolder = false; // True for folders, false for files

  // --- Cached metadata (for display, avoid repeated service calls) ---
  QString name;              // Display name (last path component or notebook name for root)
  QDateTime createdTimeUtc;  // Creation timestamp
  QDateTime modifiedTimeUtc; // Last modification timestamp
  int childCount = 0;        // Number of direct children (folders only)
  QStringList tags;          // Associated tags (files only)

  // --- Convenience methods ---
  bool isRoot() const { return relativePath.isEmpty(); }

  bool isValid() const { return !notebookId.isEmpty(); }

  QString parentPath() const {
    int lastSlash = relativePath.lastIndexOf(QLatin1Char('/'));
    return lastSlash > 0 ? relativePath.left(lastSlash) : QString();
  }

  // Equality for QSet/QHash usage
  bool operator==(const NodeInfo &p_other) const {
    return notebookId == p_other.notebookId && relativePath == p_other.relativePath;
  }

  bool operator!=(const NodeInfo &p_other) const { return !(*this == p_other); }
};

inline uint qHash(const NodeInfo &p_info, uint p_seed = 0) {
  return qHash(p_info.notebookId, p_seed) ^ qHash(p_info.relativePath, p_seed);
}

} // namespace vnotex

Q_DECLARE_METATYPE(vnotex::NodeInfo)

#endif // NODEINFO_H
