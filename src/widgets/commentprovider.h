#ifndef COMMENTPROVIDER_H
#define COMMENTPROVIDER_H

#include <QObject>
#include <QString>

#include <core/services/commenttypes.h>

namespace vnotex {

// Per-ViewWindow handle onto that window's comments, mirroring OutlineProvider.
//
// It exists so the comment dock can be re-pointed at whatever window is current
// without knowing anything about PDFs — exactly the way MainWindow2 re-points
// the Outline dock on currentViewWindowChanged. A future Markdown implementation
// only has to hand back one of these.
//
// This is a pure DATA + SIGNALS object: it never writes the store, and it holds
// no service. The dock emits intents into it; CommentController listens and owns
// every mutation.
class CommentProvider : public QObject {
  Q_OBJECT

public:
  explicit CommentProvider(QObject *p_parent = nullptr);

  const CommentSet &getComments() const;

  // Called by the owning window/controller when the set changes.
  void setComments(const CommentSet &p_comments);

  const QString &getSelectedId() const;

  void setSelectedId(const QString &p_id);

  // Whether this window can accept edits at all (a read-only notebook, or a
  // file whose store could not be located, cannot).
  bool isEditable() const;

  void setEditable(bool p_editable);

signals:
  void commentsChanged();

  void selectionChanged();

  void editableChanged();

  // === Intents, emitted BY the view, consumed by CommentController ===

  void activateRequested(const QString &p_id);

  void textEditRequested(const QString &p_id, const QString &p_text);

  void colorChangeRequested(const QString &p_id, const QString &p_color);

  void deleteRequested(const QString &p_id);

private:
  CommentSet m_comments;

  QString m_selectedId;

  bool m_editable = true;
};

} // namespace vnotex

#endif // COMMENTPROVIDER_H
