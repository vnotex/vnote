#ifndef TAGSERVICE_H
#define TAGSERVICE_H

#include <QJsonArray>
#include <QString>
#include <QStringList>

#include <core/services/tagcoreservice.h>

namespace vnotex {

class HookManager;

// Hook-aware wrapper around TagCoreService.
// Manages tag lifecycle (create, delete, move) and file-tag operations
// with vnote.tag.* and vnote.file.* hooks.
// Read-only operations (listTags, searchByTags, createTagPath) are pass-through.
class TagService : private TagCoreService {
  Q_OBJECT

public:
  // Constructor receives VxCore context handle, HookManager.
  explicit TagService(VxCoreContextHandle p_context, HookManager *p_hookMgr,
                      QObject *p_parent = nullptr);
  ~TagService() override;

  // Expose QObject base for signal connections from outside.
  // Needed because TagService privately inherits QObject (via TagCoreService),
  // so external code cannot use connect() with TagService* directly.
  QObject *asQObject();

  // ============ Tag Lifecycle (with hooks) ============

  // Create a tag in a notebook.
  // Fires TagBeforeCreate (cancellable) and TagAfterCreate.
  bool createTag(const QString &p_notebookId, const QString &p_tagName);

  // Delete a tag from a notebook.
  // Fires TagBeforeDelete (cancellable) and TagAfterDelete.
  bool deleteTag(const QString &p_notebookId, const QString &p_tagName);

  // Move (reparent) a tag within a notebook.
  // Fires TagBeforeMove (cancellable) and TagAfterMove.
  bool moveTag(const QString &p_notebookId, const QString &p_tagName, const QString &p_parentTag);

  // ============ File-Tag Operations (with hooks) ============

  // Apply a tag to a file.
  // Fires FileBeforeTag (cancellable) and FileAfterTag.
  bool tagFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_tagName);

  // Remove a tag from a file.
  // Fires FileBeforeUntag (cancellable) and FileAfterUntag.
  bool untagFile(const QString &p_notebookId, const QString &p_filePath, const QString &p_tagName);

  // Update all tags for a file (bulk operation).
  // Fires FileBeforeTag (cancellable, operation="update_tags") and FileAfterTag.
  bool updateFileTags(const QString &p_notebookId, const QString &p_filePath,
                      const QString &p_tagsJson);

  // ============ Pass-through (no hooks) ============

  // Create a tag path (intermediate tags as needed). No hooks.
  bool createTagPath(const QString &p_notebookId, const QString &p_tagPath);

  // List all tags in a notebook. No hooks.
  QJsonArray listTags(const QString &p_notebookId) const;

  // Search files by tags. No hooks.
  QJsonArray searchByTags(const QString &p_notebookId, const QStringList &p_tags);

  // Find files by tags using efficient database lookup. No hooks.
  // p_op: "AND" to match files with ALL tags, "OR" to match files with ANY tag.
  QJsonArray findFilesByTags(const QString &p_notebookId, const QStringList &p_tags,
                             const QString &p_op = QString(QLatin1String("AND")));

private:
  HookManager *m_hookMgr = nullptr;
};

} // namespace vnotex

#endif // TAGSERVICE_H
