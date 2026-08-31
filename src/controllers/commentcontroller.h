#ifndef COMMENTCONTROLLER_H
#define COMMENTCONTROLLER_H

#include <QObject>
#include <QString>
#include <QTimer>

#include <core/nodeidentifier.h>
#include <core/services/commenttypes.h>

namespace vnotex {

class CommentProvider;
class ServiceLocator;

// CommentController
//
// The ONLY thing that mutates a file's comment set. Widgets are the view layer
// and must not modify data directly (src/widgets/AGENTS.md), and the QWebChannel
// bridge is a view too — both emit intents, which land here.
//
// It is a QObject, never a QWidget, so it is testable without a GUI.
//
// Responsibilities:
//   * own the active NodeIdentifier and the in-memory CommentSet;
//   * turn add/edit/color/delete INTENTS into a new set;
//   * debounce and coalesce the writes (CommentService coalesces again on its
//     own queue, but debouncing here also avoids re-serializing the whole set
//     on every keystroke in the dock);
//   * receive asynchronous completion and surface failures WITHOUT marking the
//     buffer modified — the PDF itself genuinely never changes.
class CommentController : public QObject {
  Q_OBJECT

public:
  explicit CommentController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // Point the controller at a file and load its store. Passing an invalid
  // identifier detaches it (empty set, not editable).
  void setActiveFile(const NodeIdentifier &p_nodeId);

  // Follow a rename/move WITHOUT reloading: the in-memory set is already
  // current, and a reload would race the queued sidecar move. Any pending write
  // is re-aimed at the new identifier so the user's last edit is not written to
  // the old path.
  void retargetActiveFile(const NodeIdentifier &p_newNodeId);

  const NodeIdentifier &getActiveFile() const;

  const CommentSet &getComments() const;

  bool isEditable() const;

  // True when there is an edit that has not been durably written yet. Stays
  // true across a FAILED save, so the retry path and the close path can both
  // see it.
  bool hasUnsavedChanges() const;

  // Flush a debounced write immediately. Called on window close so the last
  // edit is not lost to the timer.
  void flushPendingSave();

public slots:
  // === Intents ===

  void addComment(const QJsonObject &p_anchor, const QString &p_color);

  void setCommentText(const QString &p_id, const QString &p_text);

  void setCommentColor(const QString &p_id, const QString &p_color);

  // Reposition an existing pdf-freetext box. The ONLY geometry mutator.
  //
  // It rewrites `page`, `x` and `y` on a COPY of the stored anchor object, so
  // `fontSize` and any key a newer build wrote survive verbatim
  // (commenttypes.h:285). It deliberately does NOT emit commentAdded, or the
  // move would re-open the inline editor on the box.
  void moveComment(const QString &p_id, int p_page, double p_x, double p_y);

  void deleteComment(const QString &p_id);

  void selectComment(const QString &p_id);

signals:
  // The set changed for any reason (load, add, edit, delete).
  void commentsChanged(const vnotex::CommentSet &p_comments);

  void selectionChanged(const QString &p_id);

  void editableChanged(bool p_editable);

  // A newly created comment, so the view can focus its editor.
  void commentAdded(const QString &p_id);

  // Human-readable failure for the view to surface (an InlineBanner or a
  // notification). NEVER accompanied by a buffer-modified flag.
  void failed(const QString &p_message);

private slots:
  void onSaveTimeout();

  void onSaveFinished(const vnotex::NodeIdentifier &p_nodeId, quint64 p_generation, bool p_ok,
                      const QString &p_error);

private:
  void scheduleSave();

  void publish();

  int indexOf(const QString &p_id) const;

  ServiceLocator &m_services;

  NodeIdentifier m_nodeId;

  CommentSet m_comments;

  bool m_editable = false;

  QString m_selectedId;

  // Debounce. Short enough to feel immediate, long enough that typing a
  // sentence in the dock is one write rather than one per character.
  QTimer *m_saveTimer = nullptr;

  // Monotonic edit counter. `m_dirtyGeneration` is the newest edit;
  // `m_savedGeneration` is the newest one CONFIRMED on disk. A save is only
  // "done" when the two meet, so a failed write leaves the file dirty and
  // retryable instead of being silently forgotten.
  quint64 m_dirtyGeneration = 0;

  quint64 m_savedGeneration = 0;

  // Set while a scheduleSave() has been handed to the service and no completion
  // for it has arrived yet, so the timer does not enqueue the same generation
  // repeatedly.
  quint64 m_inFlightGeneration = 0;
};

} // namespace vnotex

#endif // COMMENTCONTROLLER_H
