#include "commentcontroller.h"

#include <QDateTime>
#include <QDebug>

#include <core/servicelocator.h>
#include <core/services/commentservice.h>
#include <core/services/notebookcoreservice.h>

using namespace vnotex;

namespace {
// Debounce window for a comment write. Deliberately shorter than the config
// manager's 500 ms: a comment is a user-visible artifact and a crash inside a
// long window would lose real work.
constexpr int c_saveDebounceMs = 400;
} // namespace

CommentController::CommentController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {
  m_saveTimer = new QTimer(this);
  m_saveTimer->setSingleShot(true);
  m_saveTimer->setInterval(c_saveDebounceMs);
  connect(m_saveTimer, &QTimer::timeout, this, &CommentController::onSaveTimeout);

  if (auto *service = m_services.get<CommentService>()) {
    connect(service, &CommentService::saveFinished, this, &CommentController::onSaveFinished);

    connect(service, &CommentService::saveRejectedReadOnly, this,
            [this](const NodeIdentifier &p_nodeId) {
              if (p_nodeId != m_nodeId) {
                return;
              }
              if (m_editable) {
                m_editable = false;
                emit editableChanged(false);
              }
              emit failed(tr("This notebook is read-only, so comments cannot be saved."));
            });
  }
}

void CommentController::onSaveFinished(const NodeIdentifier &p_nodeId, quint64 p_generation,
                                       bool p_ok, const QString &p_error) {
  if (p_nodeId != m_nodeId) {
    return;
  }

  if (m_inFlightGeneration == p_generation) {
    m_inFlightGeneration = 0;
  }

  if (p_ok) {
    // Never move the confirmed mark backwards: an out-of-order completion from
    // an older generation must not un-confirm a newer one.
    if (p_generation > m_savedGeneration) {
      m_savedGeneration = p_generation;
    }
    return;
  }

  // FAILED. The file stays dirty (m_savedGeneration is untouched), so
  // hasUnsavedChanges() remains true, flushPendingSave() still has work, and the
  // next edit or flush retries. Without this the user's in-memory edits would
  // vanish on close after a disk-full / permission / transient error, with only
  // a banner to show for it.
  emit failed(tr("Failed to save comments: %1").arg(p_error));
  if (hasUnsavedChanges()) {
    m_saveTimer->start();
  }
}

bool CommentController::hasUnsavedChanges() const { return m_dirtyGeneration > m_savedGeneration; }

void CommentController::setActiveFile(const NodeIdentifier &p_nodeId) {
  if (m_nodeId == p_nodeId) {
    return;
  }

  // Never carry a pending write across a file switch: it would be serialized
  // against the OLD identifier but land after the new set was published.
  flushPendingSave();
  m_flushParticipantLease.reset();

  m_nodeId = p_nodeId;
  m_selectedId.clear();
  m_comments = CommentSet();
  m_dirtyGeneration = 0;
  m_savedGeneration = 0;
  m_inFlightGeneration = 0;

  bool editable = false;
  QString loadError;

  auto *service = m_services.get<CommentService>();
  if (service && !p_nodeId.relativePath.isEmpty() && !p_nodeId.isVirtual()) {
    const auto location = service->resolveLocation(p_nodeId);
    if (location.isValid()) {
      const auto result = service->load(p_nodeId);
      if (result.isUsable()) {
        m_comments = result.m_comments;
        editable = true;
        if (!location.m_notebookId.isEmpty()) {
          if (auto *notebooks = m_services.get<NotebookCoreService>()) {
            editable = !notebooks->isNotebookReadOnly(location.m_notebookId);
          }
        }
      } else {
        // A malformed store stays READ-ONLY. Editing would write a fresh valid
        // document over a file the user could still recover by hand.
        loadError = result.m_error;
      }
    }
  }

  if (m_editable != editable) {
    m_editable = editable;
    emit editableChanged(m_editable);
  }

  // Register before publishing: a synchronous commentsChanged receiver may
  // immediately issue an edit intent, and that generation must be visible to a
  // concurrent transfer precondition.
  registerFlushParticipant();
  publish();
  emit selectionChanged(m_selectedId);

  if (!loadError.isEmpty()) {
    emit failed(tr("The comment store could not be read, so comments are read-only for this "
                   "file: %1")
                    .arg(loadError));
  }
}

void CommentController::retargetActiveFile(const NodeIdentifier &p_newNodeId) {
  if (m_nodeId == p_newNodeId || m_nodeId.relativePath.isEmpty()) {
    return;
  }

  // Deliberately NOT a reload: the in-memory set is already current, and
  // CommentService has a sidecar move queued behind any pending write, so
  // reading from disk here would race it. Re-aiming is enough.
  m_nodeId = p_newNodeId;
  m_flushParticipantLease.retarget(m_nodeId, m_dirtyGeneration);

  if (hasUnsavedChanges()) {
    // The pending edit must land at the NEW path. Restarting the timer (rather
    // than writing now) keeps the debounce, and the queued move only relocates
    // whatever already existed under the old name.
    m_inFlightGeneration = 0;
    m_saveTimer->start();
  }
}

const NodeIdentifier &CommentController::getActiveFile() const { return m_nodeId; }

const CommentSet &CommentController::getComments() const { return m_comments; }

bool CommentController::isEditable() const { return m_editable; }

int CommentController::indexOf(const QString &p_id) const { return m_comments.indexOfId(p_id); }

void CommentController::publish() { emit commentsChanged(m_comments); }

void CommentController::addComment(const QJsonObject &p_anchor, const QString &p_color) {
  if (!m_editable) {
    emit failed(tr("Comments cannot be added to this file."));
    return;
  }
  if (m_comments.m_comments.size() >= CommentSet::maxComments()) {
    emit failed(tr("This file already has the maximum number of comments (%1).")
                    .arg(CommentSet::maxComments()));
    return;
  }

  auto comment = Comment::create(p_anchor, QString(), p_color);
  if (!comment.isValid()) {
    qWarning() << "CommentController: refusing to add a structurally invalid comment";
    return;
  }

  m_comments.m_comments.append(comment);
  m_selectedId = comment.m_id;

  scheduleSave();
  publish();
  emit commentAdded(comment.m_id);
  emit selectionChanged(m_selectedId);
}

void CommentController::setCommentText(const QString &p_id, const QString &p_text) {
  if (!m_editable) {
    return;
  }
  const int idx = indexOf(p_id);
  if (idx < 0) {
    return;
  }

  const auto text = p_text.left(Comment::maxTextLength());
  if (m_comments.m_comments[idx].m_text == text) {
    return;
  }

  m_comments.m_comments[idx].m_text = text;
  m_comments.m_comments[idx].m_modifiedUtc = QDateTime::currentSecsSinceEpoch();

  scheduleSave();
  publish();
}

void CommentController::setCommentColor(const QString &p_id, const QString &p_color) {
  if (!m_editable || !CommentColor::isValid(p_color)) {
    return;
  }
  const int idx = indexOf(p_id);
  if (idx < 0 || m_comments.m_comments[idx].m_color == p_color) {
    return;
  }

  m_comments.m_comments[idx].m_color = p_color;
  m_comments.m_comments[idx].m_modifiedUtc = QDateTime::currentSecsSinceEpoch();

  scheduleSave();
  publish();
}

void CommentController::moveComment(const QString &p_id, int p_page, double p_x, double p_y) {
  if (!m_editable) {
    return;
  }
  const int idx = indexOf(p_id);
  if (idx < 0) {
    return;
  }

  const QJsonObject &oldAnchor = m_comments.m_comments[idx].m_anchor;
  if (oldAnchor.value(QStringLiteral("type")).toString() != PdfFreeTextAnchor::type()) {
    // Only free-text boxes are movable; ink and quads carry their geometry in a
    // shape this intent cannot express.
    return;
  }

  // Mutate a COPY and replace only the three geometry keys. Never rebuild via
  // PdfFreeTextAnchor::make(): that would drop fontSize's exact value and every
  // key this build does not know about.
  QJsonObject newAnchor = oldAnchor;
  newAnchor.insert(QStringLiteral("page"), p_page);
  newAnchor.insert(QStringLiteral("x"), p_x);
  newAnchor.insert(QStringLiteral("y"), p_y);

  if (newAnchor == oldAnchor) {
    // A drag that ended where it started. No save, no publish, no modifiedUtc
    // bump — and the page must not be waiting on a publish that never comes.
    return;
  }

  if (!PdfFreeTextAnchor::isValid(newAnchor)) {
    qWarning() << "CommentController: refusing a move that would invalidate the anchor";
    return;
  }

  m_comments.m_comments[idx].m_anchor = newAnchor;
  m_comments.m_comments[idx].m_modifiedUtc = QDateTime::currentSecsSinceEpoch();

  scheduleSave();
  publish();
}

void CommentController::deleteComment(const QString &p_id) {
  if (!m_editable) {
    return;
  }
  const int idx = indexOf(p_id);
  if (idx < 0) {
    return;
  }

  m_comments.m_comments.remove(idx);
  if (m_selectedId == p_id) {
    m_selectedId.clear();
    emit selectionChanged(m_selectedId);
  }

  scheduleSave();
  publish();
}

void CommentController::selectComment(const QString &p_id) {
  // Selecting is not an edit, so it neither schedules a save nor requires
  // editability. An unknown id clears the selection rather than being ignored,
  // which is what makes a click on empty space work.
  const QString resolved = indexOf(p_id) >= 0 ? p_id : QString();
  if (m_selectedId == resolved) {
    return;
  }
  m_selectedId = resolved;
  emit selectionChanged(m_selectedId);
}

void CommentController::scheduleSave() {
  ++m_dirtyGeneration;
  m_flushParticipantLease.setGeneration(m_dirtyGeneration);
  m_saveTimer->start();
}

void CommentController::registerFlushParticipant() {
  auto *service = m_services.get<CommentService>();
  if (!service || m_nodeId.relativePath.isEmpty() || m_nodeId.isVirtual()) {
    return;
  }
  m_flushParticipantLease = service->registerFlushParticipant(
      m_nodeId, [this]() { flushPendingSave(); }, m_dirtyGeneration);
}

void CommentController::onSaveTimeout() {
  if (!hasUnsavedChanges()) {
    return;
  }

  auto *service = m_services.get<CommentService>();
  if (!service || m_nodeId.relativePath.isEmpty()) {
    return;
  }

  // m_dirtyGeneration is NOT cleared here. It is only ever caught up by a
  // CONFIRMED completion, which is what keeps a failed write retryable.
  m_inFlightGeneration = m_dirtyGeneration;
  service->scheduleSave(m_nodeId, m_comments, m_dirtyGeneration);
}

void CommentController::flushPendingSave() {
  if (!hasUnsavedChanges()) {
    return;
  }
  m_saveTimer->stop();
  onSaveTimeout();
}
